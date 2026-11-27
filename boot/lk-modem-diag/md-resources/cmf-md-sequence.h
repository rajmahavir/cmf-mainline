// SPDX-License-Identifier: GPL-2.0-only
/* MT6878 modem sequence from CMF's mtk-scpsys-mt6878.c and mtk-scpsys.c. */
#ifndef CMF_MD_SEQUENCE_H
#define CMF_MD_SEQUENCE_H

struct cmf_md_maps {
	struct regmap *spm, *infra, *emi;
};

static int cmf_md_protect(struct regmap *map, unsigned int base,
			  unsigned int mask, bool protect)
{
	unsigned int value;
	int ret;

	/* EN, SET, CLR, STA are consecutive words. */
	ret = regmap_write(map, base + (protect ? 4 : 8), mask);
	if (ret)
		return ret;
	ret = regmap_read_poll_timeout(map, base, value,
				       (value & mask) == (protect ? mask : 0),
				       10, 1000000);
	if (ret)
		return ret;
	return regmap_read_poll_timeout(map, base + 12, value,
					(value & mask) == (protect ? mask : 0),
					10, 1000000);
}

static int cmf_md_switch(struct cmf_md_maps *maps, bool on)
{
	unsigned int value;
	int ret;

	if (on) {
		ret = regmap_clear_bits(maps->spm, 0xf24, 3);
	} else {
		ret = cmf_md_protect(maps->infra, 0xc50, BIT(9), true);
		if (!ret)
			ret = cmf_md_protect(maps->infra, 0xc40, BIT(11), true);
		if (!ret)
			ret = cmf_md_protect(maps->emi, 0x80, BIT(6) | BIT(7), true);
	}
	if (ret)
		return ret;

	ret = regmap_update_bits(maps->spm, 0xe00, BIT(2), on ? BIT(2) : 0);
	if (ret)
		return ret;
	ret = regmap_read_poll_timeout(maps->spm, 0xe00, value,
				       !!(value & BIT(30)) == on, 10, 1000000);
	if (ret)
		return ret;

	/* CMF asserts external buck isolation only after MD ACK drops. */
	if (!on)
		return regmap_set_bits(maps->spm, 0xf24, 3);

	ret = cmf_md_protect(maps->emi, 0x80, BIT(6) | BIT(7), false);
	if (!ret)
		ret = cmf_md_protect(maps->infra, 0xc40, BIT(11), false);
	if (!ret)
		ret = cmf_md_protect(maps->infra, 0xc50, BIT(9), false);
	return ret;
}

#endif
