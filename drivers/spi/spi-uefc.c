// SPDX-License-Identifier: GPL-2.0+

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/uefc-common.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/sizes.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>

/* Transfer Mode register */
/* share with MAPRD, MAPWR */
/* Transfer Control Register */

/*
 * Bounce buffer used by SDMA transfers, allocated once at probe from the DMA
 * pool the MFD parent carries. Operations are not capped to this size: a data
 * payload that does not fit simply goes through the I/O mode path instead.
 */
#define UEFC_SPI_DMA_BUF_SIZE		SZ_64K

/* The port select field addresses this many devices on a channel */
#define UEFC_SPI_MAX_CS			0xFF

/*
 * Padding clocked out while reading: the TXD write is what generates the
 * shift events, so a read still has to feed the TX FIFO.
 */
#define UEFC_SPI_TX_DUMMY		0xffffffff

/**
 * struct uefc_spi - UEFC xSPI channel
 *
 * @uefc:	UEFC core handler
 * @chan:	Channels this controller drives
 * @host:	SPI controller
 * @dma_buf:	Bounce buffer for SDMA transfers
 * @dma_buf_addr: DMA address of the bounce buffer
 *
 * The bounce buffer is shared by every transfer on the controller; exec_op()
 * holds the host controller's transfer lock for the whole operation, so only
 * one can use it at a time.
 */
struct uefc_spi {
	struct uefc_common *uefc;
	struct uefc_channel *chan[UEFC_CHANNELS_NB];
	struct spi_controller *host;
	void *dma_buf;
	dma_addr_t dma_buf_addr;
};

/*
 * The controller transfers data through DMA when the length is a multiple of
 * the 4-byte FIFO word; shorter or unaligned transfers fall back to PIO.
 */
static bool uefc_spi_use_dma(unsigned int data_len)
{
	return data_len && IS_ALIGNED(data_len, 4) &&
	       data_len <= UEFC_SPI_DMA_BUF_SIZE;
}

static u32 uefc_spi_mem_prep_op_cfg(const struct spi_mem_op *op,
				    unsigned int data_len)
{
	u32 cfg = OP_CMD_CNT(op->cmd.nbytes) |
		  OP_CMD_BUSW(fls(op->cmd.buswidth) - 1) |
		  OP_CMD_DTR(op->cmd.dtr);

	if (op->addr.nbytes)
		cfg |= OP_ADDR_CNT(op->addr.nbytes) |
		       OP_ADDR_BUSW(fls(op->addr.buswidth) - 1) |
		       OP_ADDR_DTR(op->addr.dtr);

	if (op->dummy.nbytes)
		cfg |= OP_DMY_CNT(op->dummy.nbytes);

	/* The direct mapping data.nbytes field is not populated. */
	if (data_len) {
		cfg |= OP_DATA_BUSW(fls(op->data.buswidth) - 1) |
		       OP_DATA_DTR(op->data.dtr);
		if (op->data.dir == SPI_MEM_DATA_IN)
			cfg |= OP_DD_RD;
	}

	if (uefc_spi_use_dma(data_len))
		cfg |= TFR_MODE_DMA_EN | TFR_MODE_CSB_DONT_CARE |
		       TFR_MODE_DMA_KEEP_CSB;

	return cfg;
}

/*
 * Move a payload through the controller in I/O mode, one FIFO word at a time.
 *
 * The TX and RX FIFOs are linked: reading the TX FIFO is what induces a write
 * into the RX FIFO, so every byte shifted out shifts one byte in. Both
 * directions therefore drive both FIFOs - a read pads the TX FIFO with dummy
 * bytes because the TXD write is what generates the shift events, and a write
 * still drains RXD because the RX FIFO fills up behind it and the transfer
 * stalls once it is full. Hence the single loop for both directions.
 */
static int uefc_spi_io_mode_xfer(struct uefc_spi *mxic, const void *tx,
				 void *rx, unsigned int len)
{
	u32 pos = 0;

	while (pos < len) {
		u32 nbytes = len - pos;
		/* Dummy padding, overwritten below when there is data to send. */
		u32 data = UEFC_SPI_TX_DUMMY;
		u32 sts = 0;
		int ret = 0;

		if (nbytes > 4)
			nbytes = 4;

		if (tx)
			memcpy(&data, tx + pos, nbytes);

		ret = regmap_read_poll_timeout(mxic->uefc->regmap, PRES_STS, sts,
					       sts & PRES_STS_TX_NFULL, 0, USEC_PER_SEC);
		if (ret)
			return ret;

		uefc_write_register(mxic->uefc, TXD(nbytes % 4), data);
		ret = regmap_read_poll_timeout(mxic->uefc->regmap, PRES_STS, sts,
					       sts & PRES_STS_RX_NEMPT, 0, USEC_PER_SEC);
		if (ret)
			return ret;

		uefc_read_register(mxic->uefc, RXD_REG, &data);
		if (rx)
			memcpy(rx + pos, &data, nbytes);

		pos += nbytes;
	}
	return 0;
}

static bool uefc_spi_mem_supports_op(struct spi_mem *mem,
				     const struct spi_mem_op *op)
{
	if (op->data.buswidth > 8 || op->addr.buswidth > 8 ||
	    op->dummy.buswidth > 8 || op->cmd.buswidth > 8)
		return false;

	/* Driver only has a 2-byte local cmd buffer in exec_op(). */
	if (op->cmd.nbytes > 2)
		return false;

	if (op->addr.nbytes > 7)
		return false;

	return spi_mem_default_supports_op(mem, op);
}

/* Maximum time to wait for an SDMA data transfer to complete. */
#define UEFC_SPI_DMA_TIMEOUT_MS		1000

static int uefc_spi_dma_xfer(struct uefc_spi *mxic, const struct spi_mem_op *op)
{
	u32 int_sts, sdma_addr;
	unsigned long timeout;
	int ret = 0;

	if (op->data.dir == SPI_MEM_DATA_OUT)
		memcpy(mxic->dma_buf, op->data.buf.out, op->data.nbytes);
	else
		memset(mxic->dma_buf, 0xff, op->data.nbytes);

	uefc_write_register(mxic->uefc, INT_STS,
			    INT_STS_DMA_TFR_CMPLT | INT_STS_DMA_INT);
	uefc_write_register(mxic->uefc, SDMA_CNT, op->data.nbytes);
	uefc_write_register(mxic->uefc, SDMA_ADDR, lower_32_bits(mxic->dma_buf_addr));

	timeout = jiffies + msecs_to_jiffies(UEFC_SPI_DMA_TIMEOUT_MS);
	for (;;) {
		uefc_read_register(mxic->uefc, INT_STS, &int_sts);

		/* Re-arm the address on each SDMA buffer-boundary interrupt. */
		if (int_sts & INT_STS_DMA_INT) {
			uefc_write_register(mxic->uefc, INT_STS, INT_STS_DMA_INT);
			uefc_read_register(mxic->uefc, SDMA_ADDR, &sdma_addr);
			uefc_write_register(mxic->uefc, SDMA_ADDR, sdma_addr);
		}

		if (int_sts & INT_STS_DMA_TFR_CMPLT)
			break;

		if (time_after(jiffies, timeout)) {
			dev_err(mxic->uefc->dev, "SPI DMA transfer timeout\n");
			return -ETIMEDOUT;
		}

		cpu_relax();
	}

	uefc_write_register(mxic->uefc, INT_STS, INT_STS_DMA_TFR_CMPLT);

	if (op->data.dir == SPI_MEM_DATA_IN)
		memcpy(op->data.buf.in, mxic->dma_buf, op->data.nbytes);

	return ret;
}

static int uefc_spi_mem_exec_op(struct spi_mem *mem, const struct spi_mem_op *op)
{
	struct uefc_spi *mxic = spi_controller_get_devdata(mem->spi->controller);
	u8 addr[8], cmd[2];
	int i, ret = 0;

	mutex_lock(&mxic->uefc->tfr_lock);

	ret = uefc_update_register(mxic->uefc, HC_CTRL, HC_CTRL_DATA_ORDER,
				   op->data.swap16 ? 0 : HC_CTRL_DATA_ORDER);
	if (ret)
		goto out_unlock;

	ret = uefc_update_register(mxic->uefc, HC_CTRL, HC_CTRL_CH_SEL,
				   mxic->chan[0]->id == CHANNEL_A ?
				   HC_CTRL_CH_SEL_A : HC_CTRL_CH_SEL_B);
	if (ret)
		goto out_unlock;

	/*
	 * DQS_EN tells the controller that the device drives DQS back during a
	 * read, so that the incoming data is sampled on that strobe. Only a DTR
	 * read gets one: on a write the controller drives the bus itself, and
	 * an STR device does not strobe at all. Clear it otherwise, or a DTR
	 * read would leave the controller waiting on a strobe that the next
	 * operation never sends.
	 */
	if (op->data.nbytes && op->data.dir == SPI_MEM_DATA_IN) {
		ret = uefc_update_d_register(mxic->uefc, mxic->chan[0]->id, 0,
					     DEV_CTRL, DEV_CTRL_DQS_EN,
					     op->data.dtr ? DEV_CTRL_DQS_EN : 0);
		if (ret)
			goto out_unlock;
	}

	/*
	 * TFR_MODE is a Cat. A register shared with the NAND and eMMC child
	 * drivers, so write it whole rather than masking: a read-modify-write
	 * both inherits whatever the previous protocol left behind and, since
	 * the DMA bits sit outside the OP_* field range, silently drops the
	 * DMA_EN / CSB_DONT_CARE / DMA_KEEP_CSB that prep_op_cfg() just set,
	 * leaving SDMA unarmed until the transfer times out.
	 */
	ret = uefc_write_register(mxic->uefc, TFR_MODE,
				  uefc_spi_mem_prep_op_cfg(op, op->data.nbytes));
	if (ret)
		goto out_unlock;

	ret = uefc_set_io_mode(mxic->uefc, true);
	if (ret)
		goto out_unlock;

	for (i = 0; i < op->cmd.nbytes; i++)
		cmd[i] = op->cmd.opcode >> (8 * (op->cmd.nbytes - i - 1));

	ret = uefc_spi_io_mode_xfer(mxic, cmd, NULL, op->cmd.nbytes);
	if (ret)
		goto out;

	if (op->addr.nbytes) {
		for (i = 0; i < op->addr.nbytes; i++)
			addr[i] = op->addr.val >> (8 * (op->addr.nbytes - i - 1));

		ret = uefc_spi_io_mode_xfer(mxic, addr, NULL, op->addr.nbytes);
		if (ret)
			goto out;
	}

	if (op->dummy.nbytes) {
		if (op->dummy.buswidth < op->data.buswidth)
			ret = uefc_spi_io_mode_xfer(mxic, NULL, NULL,
						    op->dummy.nbytes *
						    (op->data.buswidth / op->dummy.buswidth));
		else if (op->dummy.buswidth > op->data.buswidth)
			ret = uefc_spi_io_mode_xfer(mxic, NULL, NULL,
						    op->dummy.nbytes /
						    (op->dummy.buswidth / op->data.buswidth));
		else
			ret = uefc_spi_io_mode_xfer(mxic, NULL, NULL, op->dummy.nbytes);
		if (ret)
			goto out;
	}

	if (op->data.nbytes) {
		if (uefc_spi_use_dma(op->data.nbytes))
			ret = uefc_spi_dma_xfer(mxic, op);
		else
			ret = uefc_spi_io_mode_xfer(mxic,
						    op->data.dir == SPI_MEM_DATA_OUT ?
						    op->data.buf.out : NULL,
						    op->data.dir == SPI_MEM_DATA_IN ?
						    op->data.buf.in : NULL,
						    op->data.nbytes);
	}

out:
	uefc_set_io_mode(mxic->uefc, false);
out_unlock:
	mutex_unlock(&mxic->uefc->tfr_lock);

	return ret;
}

/*
 * SCLK is CLK_SS_I divided by 2 * (SCLK_SEL + 1), so the controller can only
 * produce 16 discrete frequencies. Pick the fastest one that does not exceed
 * what the device asked for: round the divider up, then convert it to the
 * field encoding. Both roundings go towards a larger divider, so the rate
 * programmed here is never above spi->max_speed_hz.
 *
 * The core has already clamped spi->max_speed_hz down to ctlr->max_speed_hz by
 * the time we are called, and defaulted it to that same value if the node gave
 * no rate, so it is guaranteed non-zero here. It is never raised to
 * ctlr->min_speed_hz though: a device asking for less than CLK_SS_I / 32 runs
 * out of divider and is rejected below. The core's own min_speed_hz check sits
 * in __spi_validate(), i.e. at transfer time, which is too late to stop the
 * device from registering.
 */
static int uefc_spi_setup(struct spi_device *spi)
{
	struct uefc_spi *mxic = spi_controller_get_devdata(spi->controller);
	unsigned long ss_rate = clk_get_rate(mxic->uefc->ss_clk);
	unsigned int div, sel;
	int ret, i;

	if (!ss_rate)
		return -EINVAL;

	/*
	 * A node without spi-max-frequency arrives with max_speed_hz already
	 * defaulted to the controller maximum, i.e. flat out, and is then
	 * indistinguishable from one that asked for that rate explicitly. Go
	 * back to the node so the board at least says so out loud.
	 */
	if (!of_property_present(spi->dev.of_node, "spi-max-frequency"))
		dev_warn(&spi->dev, "no spi-max-frequency, running at %u Hz\n",
			 spi->max_speed_hz);

	div = DIV_ROUND_UP(ss_rate, spi->max_speed_hz);
	sel = DIV_ROUND_UP(div, 2) - 1;
	if (sel > DEV_CTRL_SCLK_SEL_MAX)
		return -EINVAL;

	dev_dbg(&spi->dev, "requested %u Hz, SCLK_SEL %u -> %lu Hz\n",
		spi->max_speed_hz, sel, ss_rate / (2 * (sel + 1)));

	for (i = 0; i < UEFC_CHANNELS_NB; i++) {
		if (!mxic->chan[i])
			continue;

		ret = uefc_update_d_register(mxic->uefc, mxic->chan[i]->id, 0,
					     DEV_CTRL, DEV_CTRL_SCLK_SEL_MASK,
					     DEV_CTRL_SCLK_SEL(sel));
		if (ret)
			return ret;
	}

	return 0;
}

static const struct spi_controller_mem_ops uefc_spi_mem_ops = {
	.supports_op = uefc_spi_mem_supports_op,
	.exec_op = uefc_spi_mem_exec_op,
};

static const struct spi_controller_mem_caps uefc_spi_mem_caps = {
	.dtr = true,
	.ecc = true,
	.swap16 = true,
};

static int uefc_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *host;
	bool parallel_mode = false;
	struct device_node *child;
	unsigned long ss_rate;
	struct uefc_spi *spi;
	u32 version;
	u32 reg;
	int ret;
	int i;

	host = spi_alloc_host(dev, sizeof(*spi));
	if (!host)
		return -ENOMEM;

	host->bus_num = -1;
	host->dev.of_node = dev->of_node;
	host->mode_bits = SPI_CPOL | SPI_CPHA |
			  SPI_RX_DUAL | SPI_TX_DUAL |
			  SPI_RX_QUAD | SPI_TX_QUAD |
			  SPI_RX_OCTAL | SPI_TX_OCTAL;
	host->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(32);
	/*
	 * MULTI_CS is needed to handle parallel mode: the first CS given by the
	 * child is the CS on channel A, the second is the CS on channel B.
	 *
	 * The core refuses two identical CS numbers, so the number of usable
	 * chip selects is capped at the u8 CS space the SPI core exposes rather
	 * than the controller's full 256-per-channel range.
	 */
	host->num_chipselect = UEFC_SPI_MAX_CS;
	host->flags = SPI_CONTROLLER_MULTI_CS;
	host->setup = uefc_spi_setup;
	host->mem_ops = &uefc_spi_mem_ops;
	host->mem_caps = &uefc_spi_mem_caps;

	platform_set_drvdata(pdev, host);
	spi = spi_controller_get_devdata(host);
	spi->host = host;
	spi->uefc = dev_get_drvdata(dev->parent);

	ret = uefc_read_register(spi->uefc, HC_VER, &version);
	if (ret)
		return ret;

	for_each_child_of_node(dev->of_node, child)
		if (of_property_present(child, "parallel-memories"))
			parallel_mode = true;

	if (parallel_mode) {
		for (i = 0; i < UEFC_CHANNELS_NB; i++) {
			ret = uefc_init_channel(spi->uefc, i, &spi->chan[i]);
			if (ret)
				goto release_channels;
		}
	} else {
		ret = of_property_read_u32(dev->of_node, "reg", &reg);
		if (ret)
			return ret;

		if (reg > UEFC_CHANNELS_NB)
			return -EINVAL;

		ret = uefc_init_channel(spi->uefc, reg, &spi->chan[0]);
		if (ret)
			return ret;

		ret = uefc_update_d_register(spi->uefc, spi->chan[0]->id, 0, DEV_CTRL,
					     DEV_CTRL_TYPE_MASK, DEV_CTRL_TYPE_SPI);
		if (ret)
			goto release_channels;
	}

	/*
	 * The bus frequency range the SCLK_SEL divider spans: CLK_SS_I / 2 at
	 * the fastest, CLK_SS_I / 32 at the slowest. ->setup() picks the actual
	 * divider from what each device asks for.
	 */
	ss_rate = clk_get_rate(spi->uefc->ss_clk);
	if (!ss_rate) {
		ret = dev_err_probe(dev, -EINVAL, "ss clock has no rate\n");
		goto release_channels;
	}

	host->max_speed_hz = ss_rate / 2;
	host->min_speed_hz = ss_rate / (2 * (DEV_CTRL_SCLK_SEL_MAX + 1));

	spi->dma_buf = dma_alloc_coherent(spi->uefc->dev, UEFC_SPI_DMA_BUF_SIZE,
					  &spi->dma_buf_addr, GFP_KERNEL);
	if (!spi->dma_buf) {
		ret = -ENOMEM;
		goto release_channels;
	}

	ret = devm_spi_register_controller(dev, host);
	if (ret)
		goto free_dma_buf;

	return 0;

free_dma_buf:
	dma_free_coherent(spi->uefc->dev, UEFC_SPI_DMA_BUF_SIZE, spi->dma_buf,
			  spi->dma_buf_addr);
release_channels:
	for (i = 0; i < UEFC_CHANNELS_NB; i++)
		if (spi->chan[i])
			uefc_release_channel(spi->uefc, spi->chan[i]);

	return ret;
}

static void uefc_spi_remove(struct platform_device *pdev)
{
	struct spi_controller *host;
	struct uefc_spi *spi;
	int i;

	host = platform_get_drvdata(pdev);
	spi = spi_controller_get_devdata(host);

	dma_free_coherent(spi->uefc->dev, UEFC_SPI_DMA_BUF_SIZE, spi->dma_buf,
			  spi->dma_buf_addr);

	for (i = 0; i < UEFC_CHANNELS_NB; i++)
		if (spi->chan[i])
			uefc_release_channel(spi->uefc, spi->chan[i]);
}

static const struct of_device_id uefc_spi_of_match[] = {
	{ .compatible = "mxic,uefc-xspi", },
	{ },
};
MODULE_DEVICE_TABLE(of, uefc_spi_of_match);

static struct platform_driver uefc_spi_driver = {
	.probe	= uefc_spi_probe,
	.remove = uefc_spi_remove,
	.driver	= {
		.name		= "uefc-spi",
		.of_match_table	= of_match_ptr(uefc_spi_of_match),
	},
};

module_platform_driver(uefc_spi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UEFC's SPI driver");
