// SPDX-License-Identifier: GPL-2.0-only
/*
 * CCIF subset of the MT6878 infra AO gates.
 * Copyright (c) 2023 MediaTek Inc.
 */
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs ifrao1 = {
	.set_ofs = 0x88,
	.clr_ofs = 0x8c,
	.sta_ofs = 0x94,
};

static const struct mtk_gate_regs ifrao3 = {
	.set_ofs = 0xc0,
	.clr_ofs = 0xc4,
	.sta_ofs = 0xc8,
};

static const struct mtk_gate_regs ifrao2 = {
	.set_ofs = 0xa4,
	.clr_ofs = 0xa8,
	.sta_ofs = 0xac,
};

static const struct mtk_gate_regs ifrao4 = {
	.set_ofs = 0xe0,
	.clr_ofs = 0xe4,
	.sta_ofs = 0xe8,
};

/* Preserve the bootloader's gate settings until the CCCI driver takes ownership. */
#define INFRA_GATE(_id, _name, _parent, _bank, _bit) { \
	.id = (_id), \
	.name = (_name), \
	.parent_name = (_parent), \
	.regs = &(_bank), \
	.shift = (_bit), \
	.ops = &mtk_clk_gate_ops_setclr, \
	.flags = CLK_IGNORE_UNUSED, \
}

static const struct mtk_gate ccif_gates[] = {
	INFRA_GATE(0, "ifrao_ccif_ap", "axi_ck", ifrao1, 23),
	INFRA_GATE(1, "ifrao_ccif_md", "axi_ck", ifrao1, 26),
	INFRA_GATE(2, "ifrao_ccif1_ap", "axi_ck", ifrao1, 12),
	INFRA_GATE(3, "ifrao_ccif1_md", "axi_ck", ifrao1, 13),
	INFRA_GATE(4, "ifrao_ccif4_md", "axi_ck", ifrao3, 29),
	INFRA_GATE(5, "ifrao_ccif5_md", "axi_ck", ifrao3, 10),
	INFRA_GATE(6, "ifrao_dpmaif_main", "dpmaif_main_ck", ifrao3, 26),
	INFRA_GATE(7, "ifrao_cldmabclk", "axi_ck", ifrao2, 3),
	INFRA_GATE(8, "ifrao_dpmaif_26m", "f26m_ck", ifrao4, 17),
};

static const struct mtk_clk_desc ccif_desc = {
	.clks = ccif_gates,
	.num_clks = ARRAY_SIZE(ccif_gates),
};

static const struct of_device_id ccif_matches[] = {
	{ .compatible = "mediatek,mt6878-ccif-clocks", .data = &ccif_desc },
	{ }
};
MODULE_DEVICE_TABLE(of, ccif_matches);

static struct platform_driver ccif_driver = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt6878-ccif",
		.of_match_table = ccif_matches,
	},
};
module_platform_driver(ccif_driver);

MODULE_DESCRIPTION("MediaTek MT6878 CCIF clock gates");
MODULE_LICENSE("GPL");
