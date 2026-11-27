// SPDX-License-Identifier: GPL-2.0
/*
 * CMF Phone 1: boot-time translated scanout.
 *
 * Runs, back to back, the sequence verified by hand on 22 Nov 2026
 * (boot a5633679, Stage 51 + translated-scanout t2):
 *
 *   A. checks with no side effects, and the Linux identity table for the LK
 *      framebuffer pool (Linux memory only);
 *   B. clear GCE tokens 0x2b4/0x2b5 (vendor cmdq_init_cpu() clears all
 *      tokens at probe), then display IOMMU SECURE_BANK_INIT
 *      (0xC2000514, x1 = 0x400);
 *   C. CMDQ_ENABLE_DISP_VA (0xC2000518, x1 = 1), set 0x2b4 once from the CPU
 *      if needed, and wait for 0x2b5;
 *   D. register the PROTECTED regions, attach bank 0 under GenieZone Type 2
 *      (Stage 48 r3: never write PT_BASE again), TYPE2_INV, check the
 *      GenieZone mirror, and set MMU_EN on larb0 port 2 (OVL0_2L RDMA0);
 *   E. watch bank 0 faults for watch_ms; on a fault, return RDMA0 to bypass.
 *
 * After C succeeds the screen is black until D completes, so C and D run with
 * no logging pauses. Failure before C leaves the LK handoff untouched. A
 * failure after C leaves the screen black for this boot; reboot to recover.
 *
 * The result is the last "cmf-dispiommu:" line: PASS, SKIP (nothing changed)
 * or FAIL. Bank 0 keeps this table for the boot, so the module stays loaded
 * and frees nothing.
 */
#include <linux/arm-smccc.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/io-pgtable.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/irqflags.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>

#include "cmf_disp_iommu.h"

#define MMINFRA_CFG_BASE	0x1e800000
#define MMINFRA_CG0_STA		0x100
#define  CG_GCE_D		BIT(0)

#define BANK0_BASE		0x1e802000
#define REG_PT_BASE		0x000
#define REG_INVALIDATE		0x020
#define  F_INV_RANGE		BIT(0)
#define  F_ALL_INVLD		BIT(1)
#define REG_INV_SEL		0x02c
#define  F_INVLD_EN		(BIT(0) | BIT(1))
#define REG_CTRL		0x110
#define  F_SYNC_INVLDT_EN	BIT(3)
#define REG_CPE_DONE		0x12c
#define REG_FAULT_ST0		0x130
#define REG_FAULT_ST1		0x134
#define REG_MMU0_FAULT_VA	0x13c
#define REG_MMU0_INT_ID		0x150
#define L2_FAULTS		(BIT(0) | BIT(1) | BIT(5) | BIT(6))
#define MAIN_FAULTS		(GENMASK(5, 0) | GENMASK(12, 7))
#define FW_PT_BASE		0xc0000007

#define LARB0_BASE		0x1401e000
#define LARB_NONSEC_CON(p)	(0x380 + (p) * 4)
#define  F_MMU_EN		BIT(0)
#define RDMA0_PORT		2

#define OVL0_BASE		0x14002000
#define OVL_L0_PITCH		0x044
#define OVL_L0_ADDR		0xf40

#define GCED_BASE		0x1e980000
#define GCE_SYNC_TOKEN_ID	0x60
#define GCE_SYNC_TOKEN_VAL	0x64
#define GCE_SYNC_TOKEN_UPD	0x68
#define  TOKEN_UPD_SET		BIT(16)
#define T29_ENABLE		(0x100 + 29 * 0x80 + 0x04)
#define TOKEN_START		0x2b4
#define TOKEN_END		0x2b5

#define MTK_IOMMU_SECURE_CONTROL	0xC2000514
#define SECBANK_INIT_DISP		0x400
#define MTK_SIP_CMDQ_CONTROL		0xC2000518
#define CMDQ_ENABLE_DISP_VA		1
#define HYP_TYPE2_EN		0xBB00FFA4
#define HYP_REG_REGION		0xBB00FFA5
#define HYP_IOVA_TO_PHYS	0xBB00FFA6
#define HYP_TYPE2_INV		0xBB00FFA7
#define HYP_TABLE		0
#define TLB_ADDR(a)		((lower_32_bits(a) & GENMASK(31, 12)) | upper_32_bits(a))

/* mblock-15-framebuffer in the live DT. */
static unsigned long pool_base = 0xfe0d0000;
module_param(pool_base, ulong, 0400);
static unsigned long pool_size = 0x01f2f000;
module_param(pool_size, ulong, 0400);
static int watch_ms = 2000;
module_param(watch_ms, int, 0400);
static int dry_run;
module_param(dry_run, int, 0400);
MODULE_PARM_DESC(dry_run, "1: phase A only, change nothing");

static const struct {
	u64 base, size;
} regions[] = {
	{ 0x108000000ULL, SZ_64M },		/* CCU0 */
	{ 0x10c000000ULL, SZ_64M },		/* CCU1 */
	{ 0x110000000ULL, 0x60000000ULL },	/* VDO_UP */
};

static void __iomem *cfg, *bank, *larb, *ovl, *gce;
static struct io_pgtable_ops *ops;
static bool attached;			/* set once RDMA0 is translated (PASS) */
static DEFINE_MUTEX(map_lock);

static unsigned long smc(unsigned long id, unsigned long a1, unsigned long a2,
			 unsigned long a3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(id, a1, a2, a3, 0, 0, 0, 0, &res);
	return res.a0;
}

static void snapshot(const char *when)
{
	pr_info("cmf-dispiommu: %s pt=%#x ctrl=%#x st0=%#x st1=%#x va=%#x id=%#x port2=%#x ovl=%#x\n",
		when, readl(bank + REG_PT_BASE), readl(bank + REG_CTRL),
		readl(bank + REG_FAULT_ST0), readl(bank + REG_FAULT_ST1),
		readl(bank + REG_MMU0_FAULT_VA), readl(bank + REG_MMU0_INT_ID),
		readl(larb + LARB_NONSEC_CON(RDMA0_PORT)), readl(ovl + OVL_L0_ADDR));
}

static bool faults(void)
{
	return (readl(bank + REG_FAULT_ST0) & L2_FAULTS) ||
	       (readl(bank + REG_FAULT_ST1) & MAIN_FAULTS);
}

static u32 token(u16 id)
{
	unsigned long flags;
	u32 saved, val;

	local_irq_save(flags);
	saved = readl(gce + GCE_SYNC_TOKEN_ID);
	writel(id, gce + GCE_SYNC_TOKEN_ID);
	val = readl(gce + GCE_SYNC_TOKEN_VAL);
	writel(saved, gce + GCE_SYNC_TOKEN_ID);
	local_irq_restore(flags);
	return val;
}

/* Microseconds until 0x2b5 is set, or -1 after ms. */
static s64 wait_end(int ms)
{
	ktime_t t0 = ktime_get();

	do {
		if (token(TOKEN_END))
			return ktime_us_delta(ktime_get(), t0);
		udelay(50);
	} while (ktime_ms_delta(ktime_get(), t0) < ms);
	return -1;
}

static int invalidate(void)
{
	u32 done;
	int ret;

	writel(0, bank + REG_CPE_DONE);
	if (readl(bank + REG_CPE_DONE))
		return -EIO;
	writel(F_INVLD_EN, bank + REG_INV_SEL);
	writel(F_ALL_INVLD, bank + REG_INVALIDATE);
	ret = readl_poll_timeout_atomic(bank + REG_CPE_DONE, done, done, 10, 1000);
	if (ret)
		return ret;
	writel(0, bank + REG_CPE_DONE);
	return readl(bank + REG_CPE_DONE) ? -EIO : 0;
}

static int sync_range(unsigned long iova, size_t size)
{
	unsigned long r = smc(HYP_TYPE2_INV, TLB_ADDR(iova), TLB_ADDR(iova + size - 1), HYP_TABLE);

	return r ? -EIO : invalidate();
}

static u64 ns_lookup(unsigned long iova)
{
	unsigned long a0 = smc(HYP_IOVA_TO_PHYS, lower_32_bits((u64)iova),
			       upper_32_bits((u64)iova), HYP_TABLE);
	u32 ns = a0 & 0xffffffff;

	return ((u64)FIELD_GET(GENMASK(2, 0), ns) << 32) | (ns & GENMASK(31, 12));
}

static void set_port(bool translated)
{
	u32 v = readl(larb + LARB_NONSEC_CON(RDMA0_PORT));

	writel(translated ? v | F_MMU_EN : v & ~F_MMU_EN, larb + LARB_NONSEC_CON(RDMA0_PORT));
}

static int map_identity(unsigned long iova, phys_addr_t pa, size_t size)
{
	size_t mapped, off;
	int ret;

	for (off = 0; off < size; off += SZ_4K) {
		mapped = 0;
		ret = ops->map_pages(ops, iova + off, pa + off, SZ_4K, 1,
				     IOMMU_READ | IOMMU_WRITE, GFP_KERNEL, &mapped);
		if (ret || mapped != SZ_4K)
			return ret ? ret : -EIO;
	}
	return 0;
}

static const char *run(struct device *dev)
{
	struct io_pgtable_cfg pcfg = {
		.quirks = IO_PGTABLE_QUIRK_ARM_NS | IO_PGTABLE_QUIRK_NO_PERMS |
			  IO_PGTABLE_QUIRK_ARM_MTK_EXT | IO_PGTABLE_QUIRK_ARM_MTK_TTBR_EXT,
		.pgsize_bitmap = SZ_4K,
		.ias = 34,
		.oas = 35,
		.coherent_walk = true,
		.iommu_dev = dev,
	};
	unsigned long r[ARRAY_SIZE(regions)];
	unsigned long secbank, dispva;
	u32 addr, pitch, live, ctrl, t2b4, t2b5;
	s64 us, us_trig = -1;
	bool triggered = false;
	unsigned int i;
	int ret;

	/* A. Checks and the Linux table; nothing outside Linux memory changes. */
	if (smc(HYP_TYPE2_EN, 0, 0, 0) != 1)
		return "SKIP: GenieZone Type 2 not reported";
	if (readl(cfg + MMINFRA_CG0_STA) & CG_GCE_D)
		return "SKIP: GCE-D core clock gated";
	snapshot("before");
	addr = readl(ovl + OVL_L0_ADDR);
	pitch = readl(ovl + OVL_L0_PITCH) & 0xffff;
	if (pool_base + pool_size > SZ_4G || (pool_base | pool_size) & (SZ_4K - 1) ||
	    addr < pool_base || addr >= pool_base + pool_size || !pitch)
		return "SKIP: scanout address outside the framebuffer pool";
	if (readl(bank + REG_PT_BASE) != FW_PT_BASE)
		return "SKIP: bank 0 not on the firmware table";
	if (faults() || (readl(larb + LARB_NONSEC_CON(RDMA0_PORT)) & F_MMU_EN) ||
	    (readl(bank + REG_INVALIDATE) & (F_ALL_INVLD | F_INV_RANGE)))
		return "SKIP: bank 0 fault, busy invalidate or RDMA0 already translated";
	if (readl(gce + T29_ENABLE))
		return "SKIP: thread 29 already enabled";
	ops = alloc_io_pgtable_ops(ARM_V7S, &pcfg, NULL);
	if (!ops)
		return "SKIP: page table allocation failed";
	ret = map_identity(pool_base, pool_base, pool_size);
	if (ret)
		return "SKIP: identity map failed";
	pr_info("cmf-dispiommu: table ttbr=%#x, %lu pages, scanout %#x\n",
		pcfg.arm_v7s_cfg.ttbr, pool_size >> 12, addr);
	if (dry_run)
		return "SKIP: dry run, phase A passed";

	/* B. Token reset and secure bank init. Screen unaffected. */
	writel(TOKEN_START, gce + GCE_SYNC_TOKEN_UPD);
	writel(TOKEN_END, gce + GCE_SYNC_TOKEN_UPD);
	secbank = smc(MTK_IOMMU_SECURE_CONTROL, SECBANK_INIT_DISP, 0, 0);
	pr_info("cmf-dispiommu: SECURE_BANK_INIT a0=%#lx\n", secbank);
	if (secbank)
		return "SKIP: SECURE_BANK_INIT failed; display unchanged";

	/* C + D back to back: no logging pauses until RDMA0 is translated. */
	dispva = smc(MTK_SIP_CMDQ_CONTROL, CMDQ_ENABLE_DISP_VA, 0, 0);
	us = wait_end(20);
	if (us < 0 && !token(TOKEN_START)) {
		writel(TOKEN_UPD_SET | TOKEN_START, gce + GCE_SYNC_TOKEN_UPD);
		triggered = true;
		us_trig = wait_end(100);
	}
	t2b4 = token(TOKEN_START);
	t2b5 = token(TOKEN_END);
	if (!t2b5) {
		pr_info("cmf-dispiommu: DISP_VA a0=%#lx triggered=%d 0x2b4=%#x 0x2b5=%#x\n",
			dispva, triggered, t2b4, t2b5);
		return "FAIL: display-VA program did not complete; RDMA0 left in bypass";
	}
	for (i = 0; i < ARRAY_SIZE(regions); i++)
		r[i] = smc(HYP_REG_REGION, regions[i].base >> 12, regions[i].size >> 12,
			   HYP_TABLE);
	ctrl = readl(bank + REG_CTRL);
	writel(ctrl | F_SYNC_INVLDT_EN, bank + REG_CTRL);
	writel(pcfg.arm_v7s_cfg.ttbr, bank + REG_PT_BASE);	/* the only PT_BASE write */
	live = readl(bank + REG_PT_BASE);
	ret = live ? sync_range(pool_base, pool_size) : -EIO;
	if (!ret && !faults() &&
	    ns_lookup(pool_base) == pool_base &&
	    ns_lookup(addr & ~0xfffU) == (addr & ~0xfffU) &&
	    ns_lookup(pool_base + pool_size - SZ_4K) == pool_base + pool_size - SZ_4K)
		set_port(true);
	else
		ret = ret ? ret : -EIO;

	/* Log what happened, now that the picture is back (or not). */
	pr_info("cmf-dispiommu: DISP_VA a0=%#lx 0x2b5 after %lld us%s%lld us, 0x2b4=%#x\n",
		dispva, us, triggered ? ", trigger " : ", trigger -", us_trig, t2b4);
	pr_info("cmf-dispiommu: regions %#lx %#lx %#lx, ttbr %#x -> %#x, sync %d\n",
		r[0], r[1], r[2], pcfg.arm_v7s_cfg.ttbr, live, ret);
	snapshot("translated");
	if (ret || r[0] || r[1] || r[2])
		return "FAIL: bank 0 attach or GenieZone mirror check failed; screen black until reboot";

	/* E. Watch for faults. */
	for (i = 0; i < watch_ms; i += 50) {
		if (faults()) {
			set_port(false);
			snapshot("fault");
			return "FAIL: bank 0 fault; RDMA0 returned to bypass (screen black until reboot)";
		}
		msleep(50);
	}
	snapshot("final");
	attached = true;
	return "PASS: RDMA0 translated through bank 0";
}

/*
 * Map/unmap API for Linux-owned scanout buffers in the same bank 0 table.
 * Only valid after PASS. Ranges must be page aligned, below 4 GiB (OVL
 * addresses are 32 bit) and must not overlap the LK pool identity map.
 * Each call ends with the GenieZone TYPE2_INV and the bank 0 TLB sync.
 */
static bool range_ok(unsigned long iova, size_t size)
{
	return attached && size && !((iova | size) & (SZ_4K - 1)) &&
	       iova + size <= SZ_4G && iova + size > iova &&
	       (iova + size <= pool_base || iova >= pool_base + pool_size);
}

bool cmf_dispiommu_attached(void)
{
	return attached;
}
EXPORT_SYMBOL_GPL(cmf_dispiommu_attached);

/* Map npages scattered pages, in order, starting at iova. */
int cmf_dispiommu_map_pages(unsigned long iova, struct page **pages, unsigned int npages)
{
	size_t mapped;
	unsigned int i;
	int ret = 0;

	if (!range_ok(iova, (size_t)npages * SZ_4K))
		return -EINVAL;
	mutex_lock(&map_lock);
	for (i = 0; i < npages; i++) {
		mapped = 0;
		ret = ops->map_pages(ops, iova + (unsigned long)i * SZ_4K, page_to_phys(pages[i]),
				     SZ_4K, 1, IOMMU_READ, GFP_KERNEL, &mapped);
		if (ret || mapped != SZ_4K) {
			ret = ret ? ret : -EIO;
			break;
		}
	}
	if (!ret)
		ret = sync_range(iova, (size_t)npages * SZ_4K);
	mutex_unlock(&map_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cmf_dispiommu_map_pages);

int cmf_dispiommu_unmap(unsigned long iova, size_t size)
{
	struct iommu_iotlb_gather gather;
	size_t off;
	int ret = 0;

	if (!range_ok(iova, size))
		return -EINVAL;
	mutex_lock(&map_lock);
	iommu_iotlb_gather_init(&gather);
	for (off = 0; off < size; off += SZ_4K)
		if (ops->unmap_pages(ops, iova + off, SZ_4K, 1, &gather) != SZ_4K)
			ret = -EIO;
	if (!ret)
		ret = sync_range(iova, size);
	mutex_unlock(&map_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cmf_dispiommu_unmap);

/* GenieZone's view of one IOVA in table 0 (normal side), for checks. */
u64 cmf_dispiommu_lookup(unsigned long iova)
{
	return ns_lookup(iova);
}
EXPORT_SYMBOL_GPL(cmf_dispiommu_lookup);

bool cmf_dispiommu_faults(void)
{
	return attached && faults();
}
EXPORT_SYMBOL_GPL(cmf_dispiommu_faults);

static int __init dispiommu_init(void)
{
	struct platform_device *pdev;
	const char *result;

	cfg = ioremap(MMINFRA_CFG_BASE, SZ_4K);
	bank = ioremap(BANK0_BASE, SZ_4K);
	larb = ioremap(LARB0_BASE, SZ_4K);
	ovl = ioremap(OVL0_BASE, SZ_4K);
	gce = ioremap(GCED_BASE, 0x4000);
	if (!cfg || !bank || !larb || !ovl || !gce) {
		pr_err("cmf-dispiommu: SKIP: ioremap failed\n");
		return 0;
	}
	pdev = platform_device_register_simple("cmf-disp-iommu-boot", -1, NULL, 0);
	if (IS_ERR(pdev)) {
		pr_err("cmf-dispiommu: SKIP: device registration failed\n");
		return 0;
	}
	result = run(&pdev->dev);
	pr_info("cmf-dispiommu: %s\n", result);
	/* Stay loaded: bank 0 may hold this table for the rest of the boot. */
	return 0;
}
module_init(dispiommu_init);

MODULE_DESCRIPTION("CMF boot-time translated scanout (secure bank, display-VA, bank 0)");
MODULE_LICENSE("GPL");
