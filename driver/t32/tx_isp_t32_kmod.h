/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 module entry / exit and the spin-lock wrapper of the vendor module:
 * init_module, cleanup_module, __private_spin_lock_irqsave.
 *
 * Flow from the vendor disassembly (0x0 .init.text, 0x0 .exit.text, 0x4b4):
 *  - init_module: tx_isp_init(), then tx_isp_sinfo_init() only when that
 *    returned 0; the module returns tx_isp_init's value;
 *  - cleanup_module: tx_isp_sinfo_exit(), then tx_isp_exit();
 *  - __private_spin_lock_irqsave: local_irq_save into *flags (always: the
 *    vendor has no NULL test, Matt Davis' C adds one) and preempt_count + 1;
 *    the lock word itself is not touched (uniprocessor kernel).
 * The calls go through struct t32_kmod_ops.
 *
 * Checked against the vendor flow (Matt Davis' recovered C, PR #15, built
 * with fix_op8_kwrap.sh; flags != NULL only, see above) by tests/t32_oracle
 * (t32_kmod_oracle).
 */
#ifndef TX_ISP_T32_KMOD_H
#define TX_ISP_T32_KMOD_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

struct t32_kmod_ops {
	int (*isp_init)(void);		/* tx_isp_init */
	int (*sinfo_init)(void);	/* tx_isp_sinfo_init */
	void (*isp_exit)(void);		/* tx_isp_exit */
	void (*sinfo_exit)(void);	/* tx_isp_sinfo_exit */
	unsigned long (*irq_save)(void);	/* local_irq_save */
	void (*preempt_disable)(void);
};

static inline int t32_init_module(const struct t32_kmod_ops *k)
{
	int ret = k->isp_init();

	if (ret == 0)
		k->sinfo_init();
	return ret;
}

static inline void t32_cleanup_module(const struct t32_kmod_ops *k)
{
	k->sinfo_exit();
	k->isp_exit();
}

static inline void t32_private_spin_lock_irqsave(const struct t32_kmod_ops *k, void *lock, unsigned long *flags)
{
	(void)lock;
	*flags = k->irq_save();
	k->preempt_disable();
}

#endif /* TX_ISP_T32_KMOD_H */
