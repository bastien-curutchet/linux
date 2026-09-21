// SPDX-License-Identifier: GPL-2.0+

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/core.h>
#include <linux/mfd/uefc-common.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

/* Host Controller Register */

/* Device Present Status Register */

/* Sample Point Adjust Register */
/* Longest DQS input delay; the conservative starting point for a channel */

static const struct mfd_cell nand_cell = {
	.name = "nand",
	.of_compatible = "mxic,uefc-nand",
};

static const struct mfd_cell xspi_cell = {
	.name = "xspi",
	.of_compatible = "mxic,uefc-xspi",
};

static const struct mfd_cell memory_cell = {
	.name = "mmc",
	.of_compatible = "mxic,uefc-mmc",
};

static const struct regmap_config uefc_regmap_config = {
	.fast_io = true,
	.max_register = 0x160,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.disable_locking = true,
};

/**
 * uefc_write_register - Write a value to a single register (Cat. A, B, C or E)
 *
 * @uefc:	UEFC core handler
 * @reg:	Register to write to
 * @val:	Value to be written
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_write_register(struct uefc_common *uefc, unsigned int reg, u32 val)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_write(uefc->regmap, reg, val);
	spin_unlock_irqrestore(&uefc->reg_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(uefc_write_register);

/**
 * uefc_read_register - Read a value from a single register (Cat. A, B, C or E)
 *
 * @uefc:	UEFC core handler
 * @reg:	Register to read from
 * @val:	Pointer to store read value
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_read_register(struct uefc_common *uefc, unsigned int reg, u32 *val)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_read(uefc->regmap, reg, val);
	spin_unlock_irqrestore(&uefc->reg_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(uefc_read_register);

/**
 * uefc_update_register -  Perform a read/modify/write cycle on a register (Cat. A, B, C or E)
 *
 * @uefc:	UEFC core handler
 * @reg:	Register to update
 * @mask:	Bitmask to change
 * @val:	New value for bitmask
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_update_register(struct uefc_common *uefc, unsigned int reg, u32 mask, u32 val)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, reg, mask, val);
	spin_unlock_irqrestore(&uefc->reg_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(uefc_update_register);

/**
 * uefc_write_d_register - Write a value to a single Cat. D register
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel ID to which the register is linked
 * @port:	Port number to which the register is linked
 * @reg:	Register to write to
 * @val:	Value to be written
 *
 * Selects the relevant port / channel in the Host Controller register
 * before updating the Cat. D register.
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_write_d_register(struct uefc_common *uefc, enum uefc_channel_id chan, u8 port,
			  unsigned int reg, u32 val)
{
	u32 hc_mask = HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL_MASK;
	unsigned long flags;
	u32 hc_val;
	int ret;

	hc_val = (chan == CHANNEL_B) ? HC_CTRL_CH_SEL : 0;
	hc_val |= HC_CTRL_PORT_SEL(port);

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, HC_CTRL, hc_mask, hc_val);
	if (ret < 0)
		goto out;
	ret = regmap_write(uefc->regmap, reg, val);

out:
	spin_unlock_irqrestore(&uefc->reg_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(uefc_write_d_register);

/**
 * uefc_read_d_register - Read a value from a single Cat. D register
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel ID to which the register is linked
 * @port:	Port number to which the register is linked
 * @reg:	Register to write to
 * @val:	Value to be written
 *
 * Selects the relevant port / channel in the Host Controller register
 * before reading the Cat. D register.
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_read_d_register(struct uefc_common *uefc, enum uefc_channel_id chan, u8 port,
			 unsigned int reg, u32 *val)
{
	u32 hc_mask = HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL_MASK;
	unsigned long flags;
	u32 hc_val;
	int ret;

	hc_val = (chan == CHANNEL_B) ? HC_CTRL_CH_SEL : 0;
	hc_val |= HC_CTRL_PORT_SEL(port);

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, HC_CTRL, hc_mask, hc_val);
	if (ret < 0)
		goto out;
	ret = regmap_read(uefc->regmap, reg, val);

out:
	spin_unlock_irqrestore(&uefc->reg_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(uefc_read_d_register);

/**
 * uefc_update_d_register -  Perform a read/modify/write cycle on a Cat. D register
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel ID to which the register is linked
 * @port:	Port number to which the register is linked
 * @reg:	Register to update
 * @mask:	Bitmask to change
 * @val:	New value for bitmask
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_update_d_register(struct uefc_common *uefc, enum uefc_channel_id chan, u8 port,
			   unsigned int reg, u32 mask, u32 val)
{
	u32 hc_mask = HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL_MASK;
	unsigned long flags;
	u32 hc_val;
	int ret;

	hc_val = (chan == CHANNEL_B) ? HC_CTRL_CH_SEL : 0;
	hc_val |= HC_CTRL_PORT_SEL(port);

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, HC_CTRL, hc_mask, hc_val);
	if (ret < 0)
		goto out;
	ret = regmap_update_bits(uefc->regmap, reg, mask, val);

out:
	spin_unlock_irqrestore(&uefc->reg_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(uefc_update_d_register);

/**
 * uefc_init_channel - Initialize a channel
 *
 * @uefc:	UEFC core handler
 * @id:		Channel to initialize
 * @res:	Pointer to the initialized channel
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_init_channel(struct uefc_common *uefc, enum uefc_channel_id id,
		      struct uefc_channel **res)
{
	struct uefc_channel *chan;
	unsigned long ss_rate;
	unsigned long ratio;
	int ret;

	chan = &uefc->chan[id];
	if (atomic_cmpxchg_relaxed(&chan->used,
				   UEFC_CHAN_UNUSED, UEFC_CHAN_USED) != UEFC_CHAN_UNUSED)
		return -EBUSY;

	chan->rx_clk = devm_clk_get(uefc->dev, id == CHANNEL_A ? "rx_a" : "rx_b");
	if (IS_ERR(chan->rx_clk)) {
		ret = PTR_ERR(chan->rx_clk);
		goto err_unuse;
	}

	chan->tx_clk = devm_clk_get(uefc->dev, id == CHANNEL_A ? "tx_a" : "tx_b");
	if (IS_ERR(chan->tx_clk)) {
		ret = PTR_ERR(chan->tx_clk);
		goto err_unuse;
	}

	ret = clk_prepare_enable(chan->rx_clk);
	if (ret)
		goto err_unuse;

	ret = clk_prepare_enable(chan->tx_clk);
	if (ret) {
		clk_disable_unprepare(chan->rx_clk);
		goto err_unuse;
	}

	/*
	 * The controller has to be told how much faster the channel's receive
	 * clock runs than the device side clock: the spec defines the RX_SS
	 * field as F(CLK_RX_x_I)/F(CLK_SS_I), encoded as the ratio minus one.
	 * Both rates are described in the device tree, so derive it rather than
	 * assuming a particular clock tree.
	 */
	ratio = clk_get_rate(chan->rx_clk);
	ss_rate = clk_get_rate(uefc->ss_clk);
	if (!ss_rate || !ratio || ratio % ss_rate) {
		dev_err(uefc->dev,
			"rx clock (%lu Hz) must be a multiple of the ss clock (%lu Hz)\n",
			ratio, ss_rate);
		ret = -EINVAL;
		goto err_disable;
	}

	ratio /= ss_rate;
	if (ratio > CLK_CTRL_RX_SS_MAX) {
		dev_err(uefc->dev, "rx/ss clock ratio %lu is above the %u supported\n",
			ratio, CLK_CTRL_RX_SS_MAX);
		ret = -EINVAL;
		goto err_disable;
	}

	ret = uefc_update_register(uefc, CLK_CTRL,
				   id == CHANNEL_A ? CLK_CTRL_RX_SS_A_MASK :
						     CLK_CTRL_RX_SS_B_MASK,
				   id == CHANNEL_A ? CLK_CTRL_RX_SS_A(ratio - 1) :
						     CLK_CTRL_RX_SS_B(ratio - 1));
	if (ret)
		goto err_disable;

	/*
	 * Apply the default PHY sampling configuration: select the DDR/SDR
	 * sample points, run the device clock at the slowest divider and clear
	 * the per-SIO input/output delay lines so the controller starts from a
	 * known, conservative timing. The child drivers raise the clock once they
	 * know what the attached device supports.
	 */
	ret = uefc_write_d_register(uefc, id, 0, SAMPLE_ADJ,
				    SAMPLE_ADJ_POINT_SEL_DDR_DEFAULT |
				    SAMPLE_ADJ_POINT_SEL_SDR_DEFAULT);
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, DEV_CTRL, DEV_CTRL_SCLK_SEL_SLOWEST);
	if (ret)
		goto err_disable;

	ret = uefc_update_d_register(uefc, id, 0, SAMPLE_ADJ,
				     SAMPLE_ADJ_DQS_IDLY_DOPI_MASK,
				     SAMPLE_ADJ_DQS_IDLY_DOPI_DEFAULT);
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, SIO_IDLY_1, 0);
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, SIO_IDLY_2, 0);
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, SIO_ODLY_1, 0);
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, SIO_ODLY_2, 0);
	if (ret)
		goto err_disable;

	*res = chan;

	return 0;

err_disable:
	clk_disable_unprepare(chan->tx_clk);
	clk_disable_unprepare(chan->rx_clk);
err_unuse:
	/*
	 * Hand the channel back so a later probe attempt (e.g. after a clock
	 * provider shows up and -EPROBE_DEFER is retried) is not rejected with
	 * -EBUSY by the reservation above.
	 */
	atomic_set(&chan->used, UEFC_CHAN_UNUSED);

	return ret;
}
EXPORT_SYMBOL_GPL(uefc_init_channel);

/**
 * uefc_release_channel - Release a channel
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel to release
 */
void uefc_release_channel(struct uefc_common *uefc, struct uefc_channel *chan)
{
	if (chan) {
		if (atomic_xchg_relaxed(&chan->used, UEFC_CHAN_UNUSED) != UEFC_CHAN_USED)
			dev_warn(uefc->dev, "Release already unused channel %s",
				 chan->id == CHANNEL_A ? "A" : "B");

		/* Channel clocks are owned by the channel reservation. */
		if (!IS_ERR_OR_NULL(chan->tx_clk))
			clk_disable_unprepare(chan->tx_clk);
		if (!IS_ERR_OR_NULL(chan->rx_clk))
			clk_disable_unprepare(chan->rx_clk);
	}
}
EXPORT_SYMBOL_GPL(uefc_release_channel);

int uefc_set_io_mode(struct uefc_common *uefc, bool enable)
{
	int ret;
	u32 sts;

	if (enable) {
		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_IO_START);
		if (ret)
			return ret;

		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_HC_ACT);
		if (ret)
			return ret;

		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_DEV_ACT);
	} else {
		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_DEV_DIS);
		if (ret)
			return ret;

		/*
		 * Per the UEFC I/O Mode programming flow, the TX FIFO must be
		 * empty before disabling I/O mode: Disable Device Transfer,
		 * poll TX FIFO until empty, then End I/O Mode. DMA completion
		 * only signals that the data reached the FIFO, so ending I/O
		 * mode right away can cut off bytes still being clocked out to
		 * the device and wedge the protocol FSM (next command stalls).
		 */
		ret = regmap_read_poll_timeout(uefc->regmap, PRES_STS, sts,
					       sts & PRES_STS_TX_EMPT,
					       10, 10 * USEC_PER_MSEC);
		if (ret) {
			/*
			 * Report the drain failure, but still end I/O mode:
			 * leaving the host controller in I/O mode guarantees
			 * that every subsequent operation fails, which is worse
			 * than the truncated transfer we are already stuck with.
			 */
			dev_warn(uefc->dev,
				 "TX FIFO not empty on I/O mode exit, PRES_STS %#x\n",
				 sts);
			uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_IO_END);
			return ret;
		}

		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_IO_END);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(uefc_set_io_mode);

/**
 * uefc_hw_init - Apply the controller-wide configuration
 *
 * @uefc:	UEFC core handler
 *
 * Implements the controller-scoped part of the Set Controller Configuration
 * Sequence from the UEFC specification (v2.12, Programming Guide,
 * Initialization). Only registers that are global to the controller are
 * touched here: the per-channel PHY timing (Sample Point Adjust and the SIO
 * delay lines) is applied by uefc_init_channel(), and the Set Device
 * Configuration Sequence is left to the child drivers.
 *
 * Host Controller Register
 *	SIO_SHIFTER is set to 10b, which shifts SIO half a CLK_TX_I cycle
 *	after SCLK. Devices require SIO to meet a setup/hold window around
 *	the SCLK edge, and this field is the adjustment the controller
 *	provides to satisfy it.
 *
 * Clock Control Register
 *	RX_SS_A and RX_SS_B give the ratio between each channel's receive
 *	clock and CLK_SS_I. The field encodes the ratio minus one, so the 1
 *	programmed here means CLK_RX_x_I runs at twice CLK_SS_I. Both must
 *	match the board clock tree before any device operation.
 *
 * Normal and Error Interrupt Status Registers
 *	The event bits are RW1C, so writing the mask back clears events left
 *	over from a previous boot or reset.
 *
 * Normal and Error Interrupt Status Enable Registers
 *	Gate whether an event is recorded in the status register at all; a
 *	masked event never becomes visible.
 *
 * Normal and Error Interrupt Signal Enable Registers
 *	Gate whether a recorded event is allowed to assert the interrupt line.
 *
 * Note that the closing write to INT_STS_EN replaces the mask programmed
 * earlier in the sequence rather than adding to it, so it alone determines
 * which normal interrupt statuses end up enabled.
 *
 * Returns 0 on success, a negative errno otherwise
 */
static int uefc_hw_init(struct uefc_common *uefc)
{
	int ret;

	ret = uefc_update_register(uefc, HC_CTRL, HC_CTRL_SIO_SHIFTER_MASK,
				   HC_CTRL_SIO_SHIFTER_HALF_CYCLE);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, INT_STS, INT_STS_ALL_CLR);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, INT_STS_EN, INT_STS_EN_ALL_EN);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, INT_STS_SIG_EN, INT_STS_SIG_EN_ALL_EN);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, ERR_INT_STS, ERR_INT_STS_ALL_CLR);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, ERR_INT_STS_EN, ERR_INT_STS_EN_ALL_EN);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, ERR_INT_STS_SIG_EN, ERR_INT_STS_SIG_EN_ALL_EN);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, INT_STS_EN, INT_STS_DMA | INT_STS_EN_DMA_TFR_CMPLT);

	return ret;
}

static int uefc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uefc_common *uefc;
	struct gpio_desc *reset;
	struct resource *res;
	bool added = false;
	void __iomem *regs;
	u32 host_version;
	u32 proto[2];
	int ret;
	int i;

	uefc = devm_kzalloc(dev, sizeof(*uefc), GFP_KERNEL);
	if (!uefc)
		return -ENOMEM;

	uefc->dev = dev;
	spin_lock_init(&uefc->reg_lock);
	mutex_init(&uefc->tfr_lock);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs), "Failed to ioremap resource");

	uefc->regmap = devm_regmap_init_mmio(dev, regs, &uefc_regmap_config);
	if (IS_ERR(uefc->regmap))
		return dev_err_probe(dev, PTR_ERR(uefc->regmap), "Failed to init regmap");

	uefc->sys_clk = devm_clk_get(dev, "sys");
	if (IS_ERR(uefc->sys_clk))
		return dev_err_probe(dev, PTR_ERR(uefc->sys_clk), "Failed to get sys clk");

	uefc->ss_clk = devm_clk_get(dev, "ss");
	if (IS_ERR(uefc->ss_clk))
		return dev_err_probe(dev, PTR_ERR(uefc->ss_clk), "Failed to get ss clk");

	reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset), "Failed to get reset");
	if (reset) {
		gpiod_set_value(reset, 1);
		msleep(20);
		gpiod_set_value(reset, 0);
	}

	/* The direct-mapped flash window is optional. */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dirmap");
	if (res) {
		uefc->dirmap.addr = devm_ioremap_resource(dev, res);
		if (IS_ERR(uefc->dirmap.addr))
			return dev_err_probe(dev, PTR_ERR(uefc->dirmap.addr),
					     "Failed to map dirmap resource");
		uefc->dirmap.raw_addr = res->start;
		uefc->dirmap.size = resource_size(res);
	}

	ret = regmap_read(uefc->regmap, HC_VER, &host_version);
	if (ret)
		return ret;

	ret = uefc_hw_init(uefc);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize the controller\n");

	dev_set_drvdata(dev, uefc);

	proto[0] = HC_VER_CHAN_PROTO(host_version, HC_VER_CHAN_A_SHIFT);
	proto[1] = HC_VER_CHAN_PROTO(host_version, HC_VER_CHAN_B_SHIFT);

	dev_dbg(dev, "HC_VER %#x, protocol A=%u B=%u\n", host_version, proto[0], proto[1]);

	for (i = 0; i < UEFC_CHANNELS_NB; i++) {
		const struct mfd_cell *cell;

		uefc->chan[i].id = i;

		switch (proto[i]) {
		case HC_VER_CHAN_PROTO_XSPI:
			cell = &xspi_cell;
			break;
		case HC_VER_CHAN_PROTO_ONFI:
			cell = &nand_cell;
			break;
		case HC_VER_CHAN_PROTO_EMMC:
			cell = &memory_cell;
			break;
		default:
			dev_dbg(dev, "Channel %s is disabled\n", i ? "B" : "A");
			continue;
		}

		ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, cell, 1, NULL, 0, NULL);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to add %s device\n",
					     cell->name);
		added = true;
	}

	if (!added)
		return dev_err_probe(dev, -ENODEV, "No usable channel found\n");

	return 0;
}

static const struct of_device_id uefc_of_match[] = {
	{ .compatible = "mxic,uefc", },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, uefc_of_match);

static struct platform_driver uefc_driver = {
	.probe = uefc_probe,
	.driver = {
		.name = "uefc",
		.of_match_table = uefc_of_match,
	},
};
module_platform_driver(uefc_driver);

MODULE_DESCRIPTION("Core driver for the Macronix UEFC");
MODULE_LICENSE("GPL");
