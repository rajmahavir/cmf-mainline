// SPDX-License-Identifier: GPL-2.0-only
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/spmi.h>

#define MT6685_DCXO_DIGCLK_ELR 0x7f4

static const struct regmap_range mt6685_clock_range[] = {
	regmap_reg_range(MT6685_DCXO_DIGCLK_ELR, MT6685_DCXO_DIGCLK_ELR),
};

static const struct regmap_access_table mt6685_readable = {
	.yes_ranges = mt6685_clock_range,
	.n_yes_ranges = ARRAY_SIZE(mt6685_clock_range),
};

static bool mt6685_writeable(struct device *dev, unsigned int reg)
{
	return false;
}

static const struct regmap_config mt6685_clock_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = MT6685_DCXO_DIGCLK_ELR,
	.rd_table = &mt6685_readable,
	.writeable_reg = mt6685_writeable,
	.fast_io = true,
	.use_single_read = true,
};

static int mt6685_clock_probe(struct spmi_device *sdev)
{
	struct device *dev = &sdev->dev;
	struct regmap *map;
	unsigned int clock;
	int ret;

	map = devm_regmap_init_spmi_ext(sdev, &mt6685_clock_regmap);
	if (IS_ERR(map))
		return dev_err_probe(dev, PTR_ERR(map), "Cannot create clock regmap\n");

	ret = regmap_read(map, MT6685_DCXO_DIGCLK_ELR, &clock);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot read DCXO clock strap\n");

	ret = devm_of_platform_populate(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot populate connectivity clock device\n");

	dev_info(dev, "MT6685 DCXO strap bit: %u\n", clock & 1);
	return 0;
}

static const struct of_device_id mt6685_clock_of_match[] = {
	{ .compatible = "mediatek,mt6685-consys-regmap" },
	{ }
};
MODULE_DEVICE_TABLE(of, mt6685_clock_of_match);

static struct spmi_driver mt6685_clock_driver = {
	.probe = mt6685_clock_probe,
	.driver = {
		.name = "mt6685-consys-regmap",
		.of_match_table = mt6685_clock_of_match,
	},
};
module_spmi_driver(mt6685_clock_driver);

MODULE_DESCRIPTION("MediaTek MT6685 diagnostic connectivity clock regmap");
MODULE_LICENSE("GPL");
