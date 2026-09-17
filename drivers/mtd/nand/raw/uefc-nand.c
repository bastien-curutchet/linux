// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/uefc-common.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "internals.h"

/* Transfer Mode register */

/* Transfer Control Register */

/*
 * ADMA2 descriptor (64-bit): [ data address : 32 | length : 16 | attr : 16 ]
 * Each transfer descriptor sets VALID | TRAN; the final descriptor of a
 * list additionally sets END.
 */

/* Boot System Address Register */

/* Device Present Status Register */

/* An ONFI page address is two column plus three row cycles */
#define UEFC_NAND_PAGE_ADDR_CYCLES	5

/*
 * Padding clocked out while reading: the TXD write is what generates the
 * shift events, so a read still has to feed the TX FIFO.
 */
#define UEFC_NAND_TX_DUMMY		0xffffffff

/* Interval between status polls while the device is busy */
#define UEFC_NAND_STATUS_POLL_MIN_US	10
#define UEFC_NAND_STATUS_POLL_MAX_US	20

/* One ADMA2 descriptor for the page data, one for the OOB area. */
#define UEFC_NAND_ADMA_DESC_NB		2
#define UEFC_NAND_ADMA_TABLE_SIZE	(UEFC_NAND_ADMA_DESC_NB * sizeof(u64))

/**
 * struct uefc_nand - UEFC raw NAND channel
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel this NAND controller drives
 * @controller:	Base NAND controller structure
 * @chip:	NAND chip attached to the channel
 * @adma_desc:	ADMA2 descriptor table
 * @adma_desc_dma: DMA address of the descriptor table
 * @data_buf:	Bounce buffer for the page data
 * @data_buf_dma: DMA address of the page data bounce buffer
 * @oob_buf:	Bounce buffer for the OOB area
 * @oob_buf_dma: DMA address of the OOB bounce buffer
 *
 * The buffers are allocated once, after nand_scan() has established the page
 * and OOB sizes, and reused by every transfer. They are shared by the whole
 * channel: page reads and writes hold the host controller's transfer lock for
 * their entire DMA sequence, and a channel drives a single chip, so no two
 * transfers can be in flight at once.
 */
struct uefc_nand {
	struct uefc_common *uefc;
	struct uefc_channel *chan;
	struct nand_controller controller;
	struct nand_chip chip;

	u64 *adma_desc;
	dma_addr_t adma_desc_dma;
	void *data_buf;
	dma_addr_t data_buf_dma;
	void *oob_buf;
	dma_addr_t oob_buf_dma;
};

struct xfer_info {
	const struct nand_op_cmd_instr *cmd;
	const struct nand_op_addr_instr *addr;
	const struct nand_op_data_instr *data;
	bool read;
	const struct nand_op_waitrdy_instr *waitrdy;
};

#define UEFC_NAND_WAITRDY_TIMEOUT_MS	1000

/*
 * Wait for the device to become ready after a read-type operation (READ PARAM,
 * GET FEATURES, page read).
 *
 * The controller samples the NAND R/B# line into the read-only RYBYB_LEVEL bit
 * of its General Purpose Inputs and Outputs Register, but the line is not fed
 * to the interrupt block: nothing is raised when the device goes ready. The
 * only way to observe the transition is to poll that bit.
 */
static int uefc_onfi_read_wait_ready(struct nand_chip *chip)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	u32 sts;
	int ret;

	ret = regmap_read_poll_timeout(mxic->uefc->regmap, GPIO_REG, sts,
				       sts & GPIO_REG_RYBYB_LEVEL, 0, USEC_PER_SEC);
	if (ret) {
		dev_err(mxic->uefc->dev, "nand device timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * Wait for the device to become ready after a program or erase operation.
 * The controller cannot poll the R/B# line for these operations, so poll the
 * NAND Read Status Register (0x70) instead. Each iteration issues a full
 * READ STATUS command via exec_op (nand_status_op()), which the hardware can
 * service, rather than relying on nand_soft_waitrdy().
 */
static int uefc_onfi_wait_ready(struct nand_chip *chip)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	unsigned long timeout;
	u8 status;
	int ret;

	timeout = jiffies + msecs_to_jiffies(UEFC_NAND_WAITRDY_TIMEOUT_MS);
	do {
		ret = nand_status_op(chip, &status);
		if (ret)
			return ret;

		if (status & NAND_STATUS_READY)
			return 0;

		usleep_range(UEFC_NAND_STATUS_POLL_MIN_US, UEFC_NAND_STATUS_POLL_MAX_US);
	} while (time_before(jiffies, timeout));

	dev_err(mxic->uefc->dev, "nand device timeout\n");
	return -ETIMEDOUT;
}

/*
 * Move a command, address or data payload through the controller, one FIFO
 * word at a time.
 *
 * The TX and RX FIFOs are linked: reading the TX FIFO is what induces a write
 * into the RX FIFO, so every byte shifted out - command, address, dummy or
 * data alike - shifts one byte in. Both transfer directions therefore have to
 * drive both FIFOs, which is why the loop below is the same for reads and
 * writes:
 *
 *  - A read has nothing to send, but the TXD write is what generates the shift
 *    events, so it pads the TX FIFO with dummy bytes instead. Drop the padding
 *    (or the TX_NFULL poll that paces it) and nothing is ever shifted into the
 *    RX FIFO, leaving the RX_NEMPT poll below to time out.
 *  - A write has nothing to collect, but the RX FIFO fills up behind it all
 *    the same and the transfer stalls once it is full, so RXD is drained even
 *    when there is no caller buffer to copy it into.
 *
 * This mirrors the TX/RX interleaving sequence in the controller
 * documentation: poll TX not full, write TXD, poll RX not empty, read RXD.
 */
static int uefc_onfi_data_xfer(struct uefc_nand *mxic, const void *txbuf,
			       void *rxbuf, unsigned int len)
{
	unsigned int pos = 0;

	while (pos < len) {
		unsigned int nbytes = len - pos;
		/* Dummy padding, overwritten below when there is data to send. */
		u32 data = UEFC_NAND_TX_DUMMY;
		u32 sts;
		int ret;

		if (nbytes > 4)
			nbytes = 4;

		if (txbuf)
			memcpy(&data, txbuf + pos, nbytes);

		ret = regmap_read_poll_timeout(mxic->uefc->regmap, PRES_STS, sts,
					       sts & PRES_STS_TX_NFULL, 0, USEC_PER_SEC);
		if (ret) {
			dev_err(mxic->uefc->dev,
				"TX FIFO stall @pos %u/%u (%s), PRES_STS %#x\n",
				pos, len, txbuf ? "tx" : "rx", sts);
			return ret;
		}

		uefc_write_register(mxic->uefc, TXD(nbytes % 4), data);
		ret = regmap_read_poll_timeout(mxic->uefc->regmap, PRES_STS, sts,
					       sts & PRES_STS_RX_NEMPT, 0, USEC_PER_SEC);
		if (ret) {
			dev_err(mxic->uefc->dev,
				"RX FIFO stall @pos %u/%u (%s), PRES_STS %#x\n",
				pos, len, txbuf ? "tx" : "rx", sts);
			return ret;
		}

		uefc_read_register(mxic->uefc, RXD_REG, &data);
		if (rxbuf)
			memcpy(rxbuf + pos, &data, nbytes);

		pos += nbytes;
	}

	return 0;
}

static unsigned int uefc_onfi_prep_cad(const struct nand_operation *op,
				       struct xfer_info *cad)
{
	const struct nand_op_instr *instr = NULL;
	unsigned int op_id, cad_count = 0;
	bool first_cad = true;

	for (op_id = 0; op_id < op->ninstrs; op_id++) {
		instr = &op->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			if (first_cad)
				first_cad = false;
			else
				cad_count++;
			cad[cad_count].cmd = &instr->ctx.cmd;
			break;

		case NAND_OP_ADDR_INSTR:
			cad[cad_count].addr = &instr->ctx.addr;
			break;

		case NAND_OP_DATA_IN_INSTR:
			cad[cad_count].data = &instr->ctx.data;
			cad[cad_count].read = true;
			break;

		case NAND_OP_DATA_OUT_INSTR:
			cad[cad_count].data = &instr->ctx.data;
			cad[cad_count].read = false;
			break;

		case NAND_OP_WAITRDY_INSTR:
			cad[cad_count].waitrdy = &instr->ctx.waitrdy;
			break;
		}
	}
	cad_count++;
	return cad_count;
}

static u32 uefc_onfi_prep_op_cfg(struct xfer_info *cad)
{
	u32 cfg = OP_CMD_BUSW(OP_BUSW_8) |
		  OP_ADDR_BUSW(OP_BUSW_8) |
		  OP_DATA_BUSW(OP_BUSW_8) |
		  TFR_MODE_CSB_DONT_CARE;

	if (cad->cmd) {
		cfg |= OP_CMD_CNT(1) |
		       OP_CMD_DTR(0);
	}

	if (cad->addr) {
		cfg |= OP_ADDR_CNT(cad->addr->naddrs) |
		       OP_ADDR_DTR(0);
	}

	if (cad->data) {
		cfg |= OP_DATA_DTR(0);
		if (cad->read)
			cfg |= OP_DD_RD;
	}

	return cfg;
}

static u32 uefc_onfi_prep_rd_cfg(void)
{
	u32 cfg = OP_CMD_BUSW(OP_BUSW_8) |
		  OP_ADDR_BUSW(OP_BUSW_8) |
		  OP_DATA_BUSW(OP_BUSW_8) |
		  TFR_MODE_CSB_DONT_CARE;

	cfg |= OP_CMD_CNT(1) | OP_CMD_DTR(0);

	cfg |= OP_DATA_DTR(0) | OP_DD_RD;

	return cfg;
}

static u32 uefc_onfi_prep_wr_cfg(void)
{
	u32 cfg = OP_CMD_BUSW(OP_BUSW_8) |
		  OP_ADDR_BUSW(OP_BUSW_8) |
		  OP_DATA_BUSW(OP_BUSW_8) |
		  TFR_MODE_CSB_DONT_CARE;

	cfg |= OP_CMD_CNT(1) | OP_CMD_DTR(0);

	cfg |= OP_ADDR_CNT(UEFC_NAND_PAGE_ADDR_CYCLES) | OP_ADDR_DTR(0);

	cfg |= OP_DATA_DTR(0);

	return cfg;
}

static int uefc_onfi_exec_op(struct nand_chip *chip,
			     const struct nand_operation *op, bool check_only)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	struct xfer_info cad[MAX_CMDS] = {};
	unsigned int cad_id, cad_count = 0;
	int ret = 0;

	if (check_only)
		return 0;

	mutex_lock(&mxic->uefc->tfr_lock);
	/*
	 * Select our channel and force DATA_ORDER back to 0 (normal byte order).
	 * HC_CTRL is shared with the xSPI child, which toggles DATA_ORDER per op;
	 * leaving it masked out here would inherit that bit from a concurrent SPI
	 * transfer and byte-swap the NAND data.
	 */
	if (mxic->chan->id == CHANNEL_A)
		ret = uefc_update_register(mxic->uefc, HC_CTRL,
					   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER, HC_CTRL_CH_SEL_A);
	else
		ret = uefc_update_register(mxic->uefc, HC_CTRL,
					   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER, HC_CTRL_CH_SEL_B);
	if (ret)
		return ret;

	ret = uefc_write_register(mxic->uefc, TFR_CTRL, TFR_CTRL_IO_START);
	if (ret)
		return ret;

	ret = uefc_write_register(mxic->uefc, TFR_CTRL, TFR_CTRL_DEV_ACT);
	if (ret)
		return ret;

	cad_count = uefc_onfi_prep_cad(op, cad);
	for (cad_id = 0; cad_id < cad_count; cad_id++) {
		ret = uefc_write_register(mxic->uefc, TFR_MODE,
					  uefc_onfi_prep_op_cfg(&cad[cad_id]));
		if (ret)
			return ret;

		if (cad[cad_id].cmd) {
			uefc_write_register(mxic->uefc, TFR_CTRL, TFR_CTRL_HC_ACT);
			ret = uefc_onfi_data_xfer(mxic, &cad[cad_id].cmd->opcode, NULL, 1);
			if (ret)
				break;
		}

		if (cad[cad_id].addr) {
			ret = uefc_onfi_data_xfer(mxic, cad[cad_id].addr->addrs, NULL,
						  cad[cad_id].addr->naddrs);
			if (ret)
				break;
		}

		/* A read operation waits on the hardware R/B# line. */
		if (cad[cad_id].waitrdy && cad[cad_id].data && cad[cad_id].read) {
			ret = uefc_onfi_read_wait_ready(chip);
			if (ret)
				break;
		}

		if (cad[cad_id].data) {
			if (cad[cad_id].read)
				ret = uefc_onfi_data_xfer(mxic, NULL,
							  cad[cad_id].data->buf.in,
							  cad[cad_id].data->len);
			else
				ret = uefc_onfi_data_xfer(mxic,
							  cad[cad_id].data->buf.out, NULL,
							  cad[cad_id].data->len);
			if (ret)
				break;
		}

		/*
		 * Program and erase operations cannot use the R/B# line, so
		 * poll the status register instead. Drop the transfer lock
		 * while waiting since the poll re-enters exec_op.
		 */
		if (cad[cad_id].waitrdy &&
		    ((cad[cad_id].data && !cad[cad_id].read) || !cad[cad_id].data)) {
			ret = uefc_set_io_mode(mxic->uefc, false);
			if (ret)
				return ret;

			mutex_unlock(&mxic->uefc->tfr_lock);
			ret = uefc_onfi_wait_ready(chip);
			if (ret)
				goto out;

			mutex_lock(&mxic->uefc->tfr_lock);
			ret = uefc_update_register(mxic->uefc, HC_CTRL,
						   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER,
						   mxic->chan->id == CHANNEL_A ?
						   HC_CTRL_CH_SEL_A : HC_CTRL_CH_SEL_B);
			if (ret)
				return ret;

			ret = uefc_set_io_mode(mxic->uefc, true);
			if (ret)
				return ret;
		}
	}
	ret = uefc_set_io_mode(mxic->uefc, false);
	if (ret)
		return ret;
	mutex_unlock(&mxic->uefc->tfr_lock);
out:
	return ret;
}

static int uefc_nand_page_read_begin(struct nand_chip *chip, int page)
{
	u8 addrs[5] = {0};
	struct nand_op_instr instrs[] = {
		NAND_OP_CMD(NAND_CMD_READ0, 0),
		NAND_OP_ADDR(4, addrs, 0),
	};
	struct nand_operation op = NAND_OPERATION(chip->cur_cs, instrs);

	addrs[2] = page;
	addrs[3] = page >> 8;

	if (chip->options & NAND_ROW_ADDR_3) {
		addrs[4] = page >> 16;
		instrs[1].ctx.addr.naddrs++;
	}

	return nand_exec_op(chip, &op);
}

/* Maximum time to wait for an ADMA2 page transfer to complete. */
#define UEFC_NAND_DMA_TIMEOUT_US	100000

static int uefc_nand_wait_dma_done(struct uefc_nand *mxic)
{
	u32 int_sts, err_sts;
	int ret;

	ret = regmap_read_poll_timeout(mxic->uefc->regmap, INT_STS, int_sts,
				       int_sts & (INT_STS_DMA_TFR_CMPLT | INT_STS_ERR_INT),
				       0, UEFC_NAND_DMA_TIMEOUT_US);
	if (ret) {
		dev_err(mxic->uefc->dev, "DMA transfer timeout\n");
		return ret;
	}

	/*
	 * ERR_INT is a read-only summary of the Error Interrupt Status
	 * Register, so the details are only worth fetching - and clearing -
	 * once it reports that something went wrong.
	 */
	if (int_sts & INT_STS_ERR_INT) {
		ret = uefc_read_register(mxic->uefc, ERR_INT_STS, &err_sts);
		if (ret)
			return ret;

		dev_err(mxic->uefc->dev, "DMA transfer error, ERR_INT_STS %#x\n",
			err_sts);
		uefc_write_register(mxic->uefc, ERR_INT_STS, err_sts);

		return -EIO;
	}

	return uefc_write_register(mxic->uefc, INT_STS, INT_STS_DMA_TFR_CMPLT);
}

static int uefc_nand_page_read(struct mtd_info *mtd, struct nand_chip *chip,
			       void *buf, void *oob_buf, int oob_len, int page)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	u8 cmd = NAND_CMD_READSTART;
	int i = 0;
	int ret;

	if (!buf && !oob_buf)
		return 0;

	if (buf)
		mxic->adma_desc[i++] = ADMA2_DESC(mxic->data_buf_dma, mtd->writesize);

	if (oob_buf)
		mxic->adma_desc[i++] = ADMA2_DESC(mxic->oob_buf_dma, mtd->oobsize);

	mxic->adma_desc[i - 1] |= ADMA2_DESC_ATTR_END;

	ret = uefc_nand_page_read_begin(chip, page);
	if (ret)
		return ret;

	mutex_lock(&mxic->uefc->tfr_lock);

	ret = uefc_update_register(mxic->uefc, HC_CTRL,
				   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER,
				   mxic->chan->id == CHANNEL_A ?
				   HC_CTRL_CH_SEL_A : HC_CTRL_CH_SEL_B);
	if (ret)
		goto out_unlock;

	ret = uefc_write_register(mxic->uefc, INT_STS,
				  INT_STS_DMA_TFR_CMPLT | INT_STS_DMA_INT);
	if (ret)
		goto out_unlock;

	ret = uefc_write_register(mxic->uefc, TFR_MODE, uefc_onfi_prep_rd_cfg());
	if (ret)
		goto out_unlock;

	ret = uefc_set_io_mode(mxic->uefc, true);
	if (ret)
		goto out_unlock;

	ret = uefc_onfi_data_xfer(mxic, &cmd, NULL, 1);
	if (ret)
		goto out;

	ret = uefc_onfi_read_wait_ready(chip);
	if (ret)
		goto out;

	ret = uefc_write_register(mxic->uefc, ADMA2_ADDR,
				  lower_32_bits(mxic->adma_desc_dma));
	if (ret)
		goto out;

	ret = uefc_nand_wait_dma_done(mxic);
	if (ret)
		goto out;

	if (buf)
		memcpy(buf, mxic->data_buf, mtd->writesize);

	if (oob_buf)
		memcpy(oob_buf, mxic->oob_buf, mtd->oobsize);

out:
	uefc_set_io_mode(mxic->uefc, false);
out_unlock:
	mutex_unlock(&mxic->uefc->tfr_lock);

	return ret;
}

static int uefc_nand_page_write(struct mtd_info *mtd, struct nand_chip *chip,
				const u8 *buf, void *oob_buf, int oob_len, int page)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	u8 cmd = NAND_CMD_SEQIN;
	u8 addrs[5] = {0};
	int i = 0;
	int ret;

	if (!buf && !oob_buf)
		return 0;

	if (buf) {
		memcpy(mxic->data_buf, buf, mtd->writesize);
		mxic->adma_desc[i++] = ADMA2_DESC(mxic->data_buf_dma, mtd->writesize);
	}

	if (oob_buf) {
		memcpy(mxic->oob_buf, oob_buf, mtd->oobsize);
		mxic->adma_desc[i++] = ADMA2_DESC(mxic->oob_buf_dma, mtd->oobsize);
	}

	mxic->adma_desc[i - 1] |= ADMA2_DESC_ATTR_END;

	addrs[2] = page;
	addrs[3] = page >> 8;

	if (chip->options & NAND_ROW_ADDR_3)
		addrs[4] = page >> 16;

	mutex_lock(&mxic->uefc->tfr_lock);

	ret = uefc_update_register(mxic->uefc, HC_CTRL,
				   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER,
				   mxic->chan->id == CHANNEL_A ?
				   HC_CTRL_CH_SEL_A : HC_CTRL_CH_SEL_B);
	if (ret)
		goto out_unlock;

	ret = uefc_write_register(mxic->uefc, TFR_MODE, uefc_onfi_prep_wr_cfg());
	if (ret)
		goto out_unlock;

	ret = uefc_set_io_mode(mxic->uefc, true);
	if (ret)
		goto out_unlock;

	ret = uefc_onfi_data_xfer(mxic, &cmd, NULL, 1);
	if (ret)
		goto out;

	ret = uefc_onfi_data_xfer(mxic, addrs, NULL, sizeof(addrs));
	if (ret)
		goto out;

	ret = uefc_write_register(mxic->uefc, ADMA2_ADDR,
				  lower_32_bits(mxic->adma_desc_dma));
	if (ret)
		goto out;

	ret = uefc_nand_wait_dma_done(mxic);
	if (ret)
		goto out;

	/*
	 * The page program is committed outside the transfer lock:
	 * nand_prog_page_end_op() goes back through exec_op(), which takes the
	 * lock itself.
	 */
	uefc_set_io_mode(mxic->uefc, false);
	mutex_unlock(&mxic->uefc->tfr_lock);

	return nand_prog_page_end_op(chip);

out:
	uefc_set_io_mode(mxic->uefc, false);
out_unlock:
	mutex_unlock(&mxic->uefc->tfr_lock);

	return ret;
}

static int uefc_nand_read_page_raw(struct nand_chip *chip, u8 *buf,
				   int oob_required, int page)
{
	void *oob_buf = oob_required ? chip->oob_poi : NULL;
	struct mtd_info *mtd = nand_to_mtd(chip);

	return uefc_nand_page_read(mtd, chip, buf, oob_buf, mtd->oobsize, page);
}

static int uefc_nand_read_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	return uefc_nand_page_read(mtd, chip, NULL, chip->oob_poi,
				   mtd->oobsize, page);
}

static int uefc_nand_write_page_raw(struct nand_chip *chip, const uint8_t *buf,
				    int oob_required, int page)
{
	void *oob_buf = oob_required ? chip->oob_poi : NULL;
	struct mtd_info *mtd = nand_to_mtd(chip);

	return uefc_nand_page_write(mtd, chip, buf, oob_buf, mtd->oobsize, page);
}

static int uefc_nand_write_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	return uefc_nand_page_write(mtd, chip, NULL, chip->oob_poi,
				   mtd->oobsize, page);
}

static const struct nand_controller_ops uefc_nand_controller_ops = {
	.exec_op = uefc_onfi_exec_op,
};

/*
 * Allocate the DMA buffers reused by every page transfer. Must run after
 * nand_scan(), which is what establishes the page and OOB sizes.
 */
static int uefc_nand_alloc_dma_buffers(struct uefc_nand *nand)
{
	struct mtd_info *mtd = nand_to_mtd(&nand->chip);
	struct device *dev = nand->uefc->dev;

	nand->adma_desc = dma_alloc_coherent(dev, UEFC_NAND_ADMA_TABLE_SIZE,
					     &nand->adma_desc_dma, GFP_KERNEL);
	if (!nand->adma_desc)
		return -ENOMEM;

	nand->data_buf = dma_alloc_coherent(dev, mtd->writesize,
					    &nand->data_buf_dma, GFP_KERNEL);
	if (!nand->data_buf)
		goto err_free_desc;

	nand->oob_buf = dma_alloc_coherent(dev, mtd->oobsize,
					   &nand->oob_buf_dma, GFP_KERNEL);
	if (!nand->oob_buf)
		goto err_free_data;

	return 0;

err_free_data:
	dma_free_coherent(dev, mtd->writesize, nand->data_buf, nand->data_buf_dma);
err_free_desc:
	dma_free_coherent(dev, UEFC_NAND_ADMA_TABLE_SIZE, nand->adma_desc,
			  nand->adma_desc_dma);

	return -ENOMEM;
}

static void uefc_nand_free_dma_buffers(struct uefc_nand *nand)
{
	struct mtd_info *mtd = nand_to_mtd(&nand->chip);
	struct device *dev = nand->uefc->dev;

	dma_free_coherent(dev, mtd->oobsize, nand->oob_buf, nand->oob_buf_dma);
	dma_free_coherent(dev, mtd->writesize, nand->data_buf, nand->data_buf_dma);
	dma_free_coherent(dev, UEFC_NAND_ADMA_TABLE_SIZE, nand->adma_desc,
			  nand->adma_desc_dma);
}

static int uefc_nand_probe(struct platform_device *pdev)
{
	struct device_node *nand_np, *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct nand_chip *nand_chip;
	struct uefc_nand *nand;
	struct mtd_info *mtd;
	u32 version;
	u32 reg;
	int ret;

	ret = of_property_read_u32(dev->of_node, "reg", &reg);
	if (ret)
		return ret;

	if (reg > UEFC_CHANNELS_NB)
		return -EINVAL;

	nand = devm_kzalloc(dev, sizeof(*nand), GFP_KERNEL);
	if (!nand)
		return -ENOMEM;

	nand->uefc = dev_get_drvdata(dev->parent);
	ret = uefc_read_register(nand->uefc, HC_VER, &version);
	if (ret)
		return ret;

	dev_dbg(dev, "UEFC host controller version %x\n", version);

	ret = uefc_init_channel(nand->uefc, reg, &nand->chan);
	if (ret)
		return ret;

	ret = uefc_update_d_register(nand->uefc, nand->chan->id, 0, DEV_CTRL,
				     DEV_CTRL_TYPE_MASK | DEV_CTRL_SCLK_SEL_MASK,
				     DEV_CTRL_TYPE_RAWNAND_ONFI | DEV_CTRL_SCLK_SEL_DEFAULT);
	if (ret)
		return ret;

	nand_chip = &nand->chip;
	mtd = nand_to_mtd(nand_chip);
	mtd->dev.parent = dev;

	for_each_child_of_node(np, nand_np)
		nand_set_flash_node(nand_chip, nand_np);

	nand_set_controller_data(nand_chip, nand);
	nand->controller.ops = &uefc_nand_controller_ops;
	nand_controller_init(&nand->controller);
	nand_chip->controller = &nand->controller;

	uefc_write_register(nand->uefc, TFR_CTRL, TFR_CTRL_HC_ACT);

	nand_chip->ecc.read_page_raw = uefc_nand_read_page_raw;
	nand_chip->ecc.read_oob = uefc_nand_read_oob;
	nand_chip->ecc.write_page_raw = uefc_nand_write_page_raw;
	nand_chip->ecc.write_oob = uefc_nand_write_oob;

	ret = nand_scan(nand_chip, 1);
	if (ret)
		goto fail;

	ret = uefc_nand_alloc_dma_buffers(nand);
	if (ret)
		goto fail_cleanup;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret)
		goto fail_free_dma;

	dev_set_drvdata(dev, nand);

	return 0;

fail_free_dma:
	uefc_nand_free_dma_buffers(nand);
fail_cleanup:
	nand_cleanup(nand_chip);
fail:
	uefc_release_channel(nand->uefc, nand->chan);
	return ret;
}

static void uefc_nand_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uefc_nand *nand;

	nand = dev_get_drvdata(dev);

	uefc_nand_free_dma_buffers(nand);
	uefc_release_channel(nand->uefc, nand->chan);
}

static const struct of_device_id uefc_nand_of_match[] = {
	{ .compatible = "mxic,uefc-nand", },
	{ /* end node */ },
};

MODULE_DEVICE_TABLE(of, uefc_nand_of_match);
static struct platform_driver uefc_nand_driver = {
	.probe		= uefc_nand_probe,
	.remove		= uefc_nand_remove,
	.driver		= {
		.name	= "uefc-nand",
		.of_match_table = of_match_ptr(uefc_nand_of_match),
	},
};

module_platform_driver(uefc_nand_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NAND driver for the UEFC");
