// SPDX-License-Identifier: GPL-2.0+

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/core.h>
#include <linux/mfd/uefc-common.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define UEFC_REG_HOST_CTRL		0x00
#define UEFC_HC_CH_SEL			BIT(11)
#define UEFC_HC_PORT_SEL		0x8
#define UEFC_REG_CAPABILITIES		0x58
#define UEFC_CAP_DUAL_CHAN		BIT(31)
#define UEFC_CAP_XSPI			BIT(30)
#define UEFC_CAP_ONFI			BIT(29)
#define UEFC_CAP_EMMC			BIT(28)
#define UEFC_CAP_INTERFACES		(UEFC_CAP_XSPI | UEFC_CAP_ONFI | UEFC_CAP_EMMC)
#define UEFC_CAP_MAPPING		BIT(27)
#define UEFC_CAP_CACHE			BIT(26)
#define UEFC_CAP_ATOMIC			BIT(25)
#define UEFC_CAP_DMA_SLAVE		BIT(24)
#define UEFC_CAP_DMA_MASTER		BIT(23)
#define UEFC_REG_HOST_CTRL_VERSION	0x5C
#define UEFC_HCV_CHAN_XSPI		0x1
#define UEFC_HCV_CHAN_ONFI		0x2
#define UEFC_HCV_CHAN_EMMC		0x3
#define UEFC_HCV_CHAN_PROTO_MASK	(UEFC_HCV_CHAN_XSPI \
					| UEFC_HCV_CHAN_ONFI \
					| UEFC_HCV_CHAN_EMMC)
#define UEFC_HCV_CHAN_A_SHIFT		4
#define UEFC_HCV_CHAN_B_SHIFT		9
#define UEFC_HCV_CHAN_A_PROTO_MASK	(UEFC_HCV_CHAN_PROTO_MASK << UEFC_HCV_CHAN_A_SHIFT)
#define UEFC_HCV_CHAN_B_PROTO_MASK	(UEFC_HCV_CHAN_PROTO_MASK << UEFC_HCV_CHAN_B_SHIFT)
#define UEFC_HCV_CHAN_B_MASK		0x600
#define UEFC_REG_RTL_VERSION		0x60

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
	.of_compatible = "mxic,uefc-memory",
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
	u32 hc_mask = UEFC_HC_CH_SEL | UEFC_HC_PORT_SEL;
	unsigned long flags;
	u32 hc_val;
	int ret;

	hc_val = (chan == CHANNEL_B) ? UEFC_HC_CH_SEL : 0;
	hc_val |= (port & UEFC_HC_PORT_SEL);

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, UEFC_REG_HOST_CTRL, hc_mask, hc_val);
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
	u32 hc_mask = UEFC_HC_CH_SEL | UEFC_HC_PORT_SEL;
	unsigned long flags;
	u32 hc_val;
	int ret;

	hc_val = (chan == CHANNEL_B) ? UEFC_HC_CH_SEL : 0;
	hc_val |= (port & UEFC_HC_PORT_SEL);

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, UEFC_REG_HOST_CTRL, hc_mask, hc_val);
	if (ret < 0)
		goto out;
	ret = regmap_read(uefc->regmap, reg, val);
	spin_unlock_irqrestore(&uefc->reg_lock, flags);

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
	u32 hc_mask = UEFC_HC_CH_SEL | UEFC_HC_PORT_SEL;
	unsigned long flags;
	u32 hc_val;
	int ret;

	hc_val = (chan == CHANNEL_B) ? UEFC_HC_CH_SEL : 0;
	hc_val |= (port & UEFC_HC_PORT_SEL);

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, UEFC_REG_HOST_CTRL, hc_mask, hc_val);
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
 * @chan_b:	Pointer to the initialized channel
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_init_channel(struct uefc_common *uefc, enum uefc_channel_id id,
			     struct uefc_channel **res)
{
	struct uefc_channel *chan;
	int ret;

	chan = &uefc->chan[id];
	if (atomic_cmpxchg_relaxed(&chan->used,
				   UEFC_CHAN_UNUSED, UEFC_CHAN_USED) != UEFC_CHAN_UNUSED)
		return -EBUSY;

	chan->rx_clk = devm_clk_get(uefc->dev, id == CHANNEL_A ? "rx_a" : "rx_b");
	if (IS_ERR(chan->rx_clk))
		return PTR_ERR(chan->rx_clk);

	chan->tx_clk = devm_clk_get(uefc->dev, id == CHANNEL_A ? "tx_a" : "tx_b");
	if (IS_ERR(chan->tx_clk))
		return PTR_ERR(chan->tx_clk);

	ret = clk_prepare_enable(chan->rx_clk);
	if (ret)
		return ret;
	ret = clk_prepare_enable(chan->tx_clk);
	if (ret)
		return ret;

	*res = chan;

	return 0;
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
			dev_warn(uefc->dev, "Release already unused channel A");
	}
}
EXPORT_SYMBOL_GPL(uefc_release_channel);

static irqreturn_t uefc_status_handler(int irq, void *dev_id)
{
	pr_info("%s - %d \n", __func__, irq);

	return IRQ_NONE;
}

static irqreturn_t uefc_error_handler(int irq, void *dev_id)
{

	pr_info("%s - %d \n", __func__, irq);

	return IRQ_NONE;
}

static int uefc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uefc_common *uefc;
	struct gpio_desc *reset;
	void __iomem *regs;
	u32 host_version;
	u32 cap_register;
	int irq_status;
	u32 proto[2];
	int irq_err;
	int ret;
	int i;

	uefc = devm_kzalloc(dev, sizeof(*uefc), GFP_KERNEL);
	if (!uefc)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs), "Failed to ioremap resource");

	uefc->regmap = devm_regmap_init_mmio(dev, regs, &uefc_regmap_config);
	if (IS_ERR(uefc->regmap))
		return dev_err_probe(dev, PTR_ERR(uefc->regmap), "Failed to init regmap");

	irq_status = platform_get_irq_byname_optional(pdev, "status");
	if (irq_status < 0 && irq_status != -ENXIO)
		return dev_err_probe(dev, irq_status, "Failed to get status IRQ");
	if (irq_status > 0) {
		ret = devm_request_threaded_irq(dev, irq_status,  NULL, uefc_status_handler,
						IRQF_ONESHOT | IRQF_SHARED, dev_name(dev), uefc);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to setup IRQ status handler");
	}

	irq_err = platform_get_irq_byname_optional(pdev, "error");
	if (irq_err < 0 && irq_err != -ENXIO)
		return dev_err_probe(dev, irq_err, "Failed to get error IRQ");
	if (irq_err > 0) {
		ret = devm_request_threaded_irq(dev, irq_err,  NULL, uefc_error_handler,
						IRQF_ONESHOT | IRQF_SHARED, dev_name(dev), uefc);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to setup IRQ error handler");
	}

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

	ret = regmap_read(uefc->regmap, UEFC_REG_HOST_CTRL_VERSION, &host_version);
	if (ret)
		return ret;

	ret = regmap_read(uefc->regmap, UEFC_REG_CAPABILITIES, &cap_register);
	if (ret)
		return ret;

	spin_lock_init(&uefc->reg_lock);
	dev_set_drvdata(dev, uefc);

	ret = -ENODEV;
	proto[0] = (host_version & UEFC_HCV_CHAN_A_PROTO_MASK) >> UEFC_HCV_CHAN_A_SHIFT;
	proto[1] = (host_version & UEFC_HCV_CHAN_B_PROTO_MASK) >> UEFC_HCV_CHAN_B_SHIFT;
	for (i = 0; i < UEFC_CHANNELS_NB; i++) {
		uefc->chan[i].id = i;

		switch (proto[i]) {
		case UEFC_HCV_CHAN_XSPI:
			dev_info(dev, "Channel %s is XSPI\n", i ? "B" : "A");
			ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, &xspi_cell,
						   1, NULL, 0, NULL);
			break;
		case UEFC_HCV_CHAN_ONFI:
			dev_info(dev, "Channel %s is ONFI\n", i ? "B" : "A");
			ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, &nand_cell,
						   1, NULL, 0, NULL);
			break;
		case UEFC_HCV_CHAN_EMMC:
			dev_info(dev, "Channel %s is EMMC\n", i ? "B" : "A");
			ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, &memory_cell,
						   1, NULL, 0, NULL);
			break;
		default:
			dev_info(dev, "Channel %s is disabled\n", i ? "B" : "A");
		}
	}

	uefc->dev = dev;

	pr_info("%s - host_version %x - caps %x\n", __func__, host_version, cap_register);

	return ret;
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

