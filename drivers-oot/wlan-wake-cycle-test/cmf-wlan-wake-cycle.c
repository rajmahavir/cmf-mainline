// SPDX-License-Identifier: GPL-2.0-only
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>

static unsigned int wlan_irqs[2];
static unsigned int irq_count;
static unsigned int disarmed[ARRAY_SIZE(wlan_irqs)];
module_param_array(wlan_irqs, uint, &irq_count, 0400);

static int restore_wake(const char *val, const struct kernel_param *kp)
{
	unsigned int i;
	int ret, error = 0;

	for (i = 0; i < irq_count; i++) {
		while (disarmed[i]) {
			ret = enable_irq_wake(wlan_irqs[i]);
			if (ret) {
				error = ret;
				break;
			}
			disarmed[i]--;
		}
	}
	return error;
}

static const struct kernel_param_ops restore_wake_ops = {
	.set = restore_wake,
};
module_param_cb(restore, &restore_wake_ops, NULL, 0200);

static int quiesce_wake(const char *val, const struct kernel_param *kp)
{
	struct irq_data *data;
	unsigned int i;
	int ret;

	if (irq_count != ARRAY_SIZE(wlan_irqs) || wlan_irqs[0] == wlan_irqs[1])
		return -EINVAL;

	for (i = 0; i < irq_count; i++) {
		data = irq_get_irq_data(wlan_irqs[i]);
		if (!data) {
			ret = -EINVAL;
			goto restore;
		}
		while (irqd_is_wakeup_set(data) && disarmed[i] < 8) {
			ret = disable_irq_wake(wlan_irqs[i]);
			if (ret)
				goto restore;
			disarmed[i]++;
		}
		if (irqd_is_wakeup_set(data)) {
			ret = -EBUSY;
			goto restore;
		}
	}
	return 0;

restore:
	restore_wake(NULL, NULL);
	return ret;
}

static const struct kernel_param_ops quiesce_wake_ops = {
	.set = quiesce_wake,
};
module_param_cb(quiesce, &quiesce_wake_ops, NULL, 0200);

static int __init cmf_wlan_wake_cycle_init(void)
{
	return quiesce_wake(NULL, NULL);
}
module_init(cmf_wlan_wake_cycle_init);
MODULE_DESCRIPTION("Temporarily disarm WLAN IRQ wake for a CMF s2idle test");
MODULE_LICENSE("GPL");
