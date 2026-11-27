// SPDX-License-Identifier: GPL-2.0-only
/* CMF Phone 1 modem resources inherited from LK. */
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#include "cmf-md-sequence.h"

struct cmf_md_domain {
	struct generic_pm_domain genpd;
	struct cmf_md_maps maps;
	struct task_struct *caller;
	bool requested_on;
	int error;
	struct device *dev;
};

static int cmf_md_transition(struct generic_pm_domain *genpd, bool on)
{
	struct cmf_md_domain *md = container_of(genpd, struct cmf_md_domain, genpd);

	if (READ_ONCE(md->caller) != current || md->requested_on != on)
		return -EBUSY;
	if (!md->error) {
		md->error = cmf_md_switch(&md->maps, on);
		if (md->error)
			dev_err(md->dev, "MD power %s failed: %d; reboot to recover\n",
				on ? "on" : "off", md->error);
	}
	return md->error;
}

static int cmf_md_on(struct generic_pm_domain *genpd)
{
	return cmf_md_transition(genpd, true);
}

static int cmf_md_off(struct generic_pm_domain *genpd)
{
	return cmf_md_transition(genpd, false);
}

/* The caller serializes commands with CCCI's FSM mutex. Only its synchronous
 * runtime-PM operation may switch MD; probing and genpd idle work cannot.
 */
int cmf_md_runtime_switch(struct device *dev, bool on);
int cmf_md_runtime_switch(struct device *dev, bool on)
{
	struct generic_pm_domain *genpd;
	struct cmf_md_domain *md;
	unsigned int value;
	int ret;

	if (!dev->pm_domain)
		return -ENODEV;
	genpd = pd_to_genpd(dev->pm_domain);
	if (genpd->power_off != cmf_md_off)
		return -ENODEV;
	md = container_of(genpd, struct cmf_md_domain, genpd);
	if (md->error)
		return md->error;
	md->requested_on = on;
	WRITE_ONCE(md->caller, current);
	ret = on ? pm_runtime_resume_and_get(dev) : pm_runtime_put_sync(dev);
	WRITE_ONCE(md->caller, NULL);
	if (ret < 0 || md->error)
		return ret < 0 ? ret : md->error;
	ret = regmap_read(md->maps.spm, 0xe00, &value);
	if (!ret && !!(value & BIT(30)) != on)
		ret = -EIO;
	if (ret) {
		md->error = ret;
		if (on)
			pm_runtime_put_noidle(dev);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(cmf_md_runtime_switch);

/* Only the CMF start selectors are valid writes in this trial. */
static const unsigned int start_sel[] = { 0x70, 0x80, 0x40 };

static int cmf_md_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	if (sel != start_sel[rdev_get_id(rdev)])
		return -EINVAL;
	return regulator_set_voltage_sel_regmap(rdev, sel);
}

static const struct regulator_ops cmf_md_switchable_rail_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
	.set_voltage_sel = cmf_md_set_voltage_sel,
};

static const struct regulator_ops cmf_md_rail_ops = {
	.is_enabled = regulator_is_enabled_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
};

/* Addresses and linear ranges from CMF's Android MT6363 regulator driver. */
#define CMF_MD_RAIL(_id, _name, _enreg, _bit, _vsel, _mask, _min, _count) { \
	.id = _id, .name = _name, .of_match = _name, .owner = THIS_MODULE, \
	.ops = &cmf_md_rail_ops, .type = REGULATOR_VOLTAGE, \
	.enable_reg = _enreg, .enable_mask = BIT(_bit), \
	.vsel_reg = _vsel, .vsel_mask = _mask, \
	.min_uV = _min, .uV_step = 6250, .n_voltages = _count, \
}

static const struct regulator_desc rails[] = {
	CMF_MD_RAIL(0, "vbuck1", 0x240, 1, 0x24d, 0xff, 0, 192),
	CMF_MD_RAIL(1, "vbuck2", 0x240, 2, 0x24e, 0xff, 0, 192),
	CMF_MD_RAIL(2, "vsram-modem", 0x243, 6, 0x25a, 0x7f, 400000, 128),
};

static void cmf_md_remove_domain(void *domain)
{
	pm_genpd_remove(domain);
}

static void cmf_md_remove_provider(void *node)
{
	of_genpd_del_provider(node);
}

static int cmf_md_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regulator_config config = { .dev = dev };
	struct cmf_md_domain *md;
	struct generic_pm_domain *domain;
	bool switchable = of_device_is_compatible(dev->of_node,
					"cmf,mt6878-switchable-modem-resources");
	struct regmap *spm;
	unsigned int value;
	int i, ret;

	config.regmap = dev_get_regmap(dev->parent, NULL);
	if (!config.regmap)
		return -EPROBE_DEFER;

	ret = regmap_read(config.regmap, 0x000b, &value);
	if (ret || value != 0x63)
		return dev_err_probe(dev, ret ?: -ENODEV, "MT6363 chip ID\n");

	spm = syscon_regmap_lookup_by_phandle(dev->of_node, "mediatek,scpsys");
	if (IS_ERR(spm))
		return dev_err_probe(dev, PTR_ERR(spm), "SPM regmap\n");
	ret = regmap_read(spm, 0xe00, &value);
	if (ret || !(value & BIT(30)))
		return dev_err_probe(dev, ret ?: -ENODEV, "missing inherited MD power ACK\n");

	/* Check all inherited selectors before regulator core applies constraints. */
	if (switchable) {
		for (i = 0; i < ARRAY_SIZE(rails); i++) {
			ret = regmap_read(config.regmap, rails[i].enable_reg, &value);
			if (ret || !(value & rails[i].enable_mask))
				return dev_err_probe(dev, ret ?: -ENODEV, "%s disabled\n", rails[i].name);
			ret = regmap_read(config.regmap, rails[i].vsel_reg, &value);
			if (ret || (value & rails[i].vsel_mask) != start_sel[i])
				return dev_err_probe(dev, ret ?: -ERANGE, "%s selector\n", rails[i].name);
		}
	}

	for (i = 0; i < ARRAY_SIZE(rails); i++) {
		struct device_node *node __free(device_node) =
			of_get_child_by_name(dev->of_node, rails[i].name);
		struct regulator_desc *desc;
		struct regulator_dev *rdev;

		if (!of_device_is_available(node))
			return -EINVAL;
		desc = devm_kmemdup(dev, &rails[i], sizeof(*desc), GFP_KERNEL);
		if (!desc)
			return -ENOMEM;
		if (switchable)
			desc->ops = &cmf_md_switchable_rail_ops;
		rdev = devm_regulator_register(dev, desc, &config);
		if (IS_ERR(rdev))
			return dev_err_probe(dev, PTR_ERR(rdev), "%s\n", rails[i].name);
	}

	md = devm_kzalloc(dev, sizeof(*md), GFP_KERNEL);
	if (!md)
		return -ENOMEM;
	domain = &md->genpd;
	domain->name = "cmf-md-lk";
	domain->flags = GENPD_FLAG_ALWAYS_ON | GENPD_FLAG_ACTIVE_WAKEUP;
	if (switchable) {
		md->maps.spm = spm;
		md->dev = dev;
		md->maps.infra = syscon_regmap_lookup_by_phandle(dev->of_node, "mediatek,infracfg");
		if (IS_ERR(md->maps.infra))
			return PTR_ERR(md->maps.infra);
		md->maps.emi = syscon_regmap_lookup_by_phandle(dev->of_node, "mediatek,nemicfg");
		if (IS_ERR(md->maps.emi))
			return PTR_ERR(md->maps.emi);
		domain->name = "cmf-md";
		domain->flags = GENPD_FLAG_ACTIVE_WAKEUP;
		domain->power_on = cmf_md_on;
		domain->power_off = cmf_md_off;
	}
	ret = pm_genpd_init(domain, NULL, false);
	if (ret)
		return ret;
	ret = devm_add_action_or_reset(dev, cmf_md_remove_domain, domain);
	if (ret)
		return ret;

	ret = of_genpd_add_provider_simple(dev->of_node, domain);
	if (ret)
		return ret;
	return devm_add_action_or_reset(dev, cmf_md_remove_provider, dev->of_node);
}

static const struct of_device_id cmf_md_match[] = {
	{ .compatible = "cmf,mt6878-lk-modem-resources" },
	{ .compatible = "cmf,mt6878-switchable-modem-resources" },
	{}
};
MODULE_DEVICE_TABLE(of, cmf_md_match);

/* Static boot tree, with the attached provider retained until reboot. */
static struct platform_driver cmf_md_driver = {
	.probe = cmf_md_probe,
	.driver = {
		.name = "cmf-md-resources",
		.of_match_table = cmf_md_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(cmf_md_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CMF Phone 1 inherited modem resource inspection");
