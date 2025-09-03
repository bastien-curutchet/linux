// SPDX-License-Identifier: GPL-2.0

#include <linux/mfd/uefc-common.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>


struct uefc_nand {
	struct uefc_common *uefc;
	struct uefc_channel *chan;
};

static int uefc_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uefc_nand *nand;
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
	ret = uefc_read_register(nand->uefc, 0x5C, &version);
	if (ret)
		return ret;

	pr_info("%s - version %x\n", __func__, version);

	ret = uefc_init_channel(nand->uefc, reg, &nand->chan);
	if (ret)
		return ret;

	dev_set_drvdata(dev, nand);

	return 0;
}

static void uefc_nand_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uefc_nand *nand;

	nand = dev_get_drvdata(dev);

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
