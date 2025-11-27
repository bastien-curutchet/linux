// SPDX-License-Identifier: GPL-2.0+

#include <linux/mfd/uefc-common.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>

struct uefc_spi {
	struct uefc_common *uefc;
	struct uefc_channel *chan[UEFC_CHANNELS_NB];
	struct spi_controller *host;
};

static int uefc_transfer_one_message(struct spi_controller *ctlr, struct spi_device *spi,
				     struct spi_transfer *xfer)
{
	return -EINVAL;
}

static int uefc_prepare_message(struct spi_controller *ctlr, struct spi_message *message)
{
	return -EINVAL;
}

static int uefc_unprepare_message(struct spi_controller *ctlr, struct spi_message *message)
{
	return -EINVAL;
}

static void uefc_set_cs(struct spi_device *spi, bool enable)
{
}

static int uefc_spi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *host;
	bool parallel_mode = false;
	struct device_node *child;
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
	host->mode_bits = SPI_MODE_3;	/*todo*/
	host->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(32); /*todo*/
	/**
	 * MULTI_CS is needed to handle parallel mode
	 *
	 * The first CS given by the child will be the CS on channel A
	 * The second CS given by the child will be the CS on channel B
	 *
	 * The issue here is that the core refuses two identical CS numbers.
	 * One possible tweak would be to add 128 to the B CS:
	 *   - 0 -> 127 = A-CS
	 *   - 128 -> 255 = B-CS
	 * It divides the number of available chip selects by two (since there
	 * can be up to 256 devices per channel) but I don't think there will
	 * be real use cases with such a big number of used devices
	 */
	host->num_chipselect = 0xFF; /* HW max is 256 * 2 but core uses u8 for CS */
	host->flags = SPI_CONTROLLER_MULTI_CS;
	host->prepare_message = uefc_prepare_message;
	host->transfer_one = uefc_transfer_one_message;
	host->unprepare_message = uefc_unprepare_message;
	host->set_cs = uefc_set_cs;
	host->max_speed_hz = 25000000;	/*todo*/
	host->min_speed_hz = 25000000;	/*todo*/

	platform_set_drvdata(pdev, host);

	spi = spi_controller_get_devdata(host);
	spi->host = host;
	spi->uefc = dev_get_drvdata(dev->parent);
	ret = uefc_read_register(spi->uefc, 0x5C, &version);
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
	}

	ret = devm_spi_register_controller(dev, host);
	if (ret)
		goto release_channels;

	return 0;

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
