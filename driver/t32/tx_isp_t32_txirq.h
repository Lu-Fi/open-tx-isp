/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp interrupt glue (vendor tx_isp_request_irq / tx_isp_free_irq,
 * the two tx_isp_enable_irq / tx_isp_disable_irq pairs at 0xd0d0 / 0xd0e8
 * and 0xb977c / 0xb9800, tx_vic_enable_irq / tx_vic_disable_irq,
 * tx_ldc_enable_irq / tx_ldc_disable_irq).
 *
 * The irq info block (struct t32_tx_irq_info, 36 bytes) sits at +156 of a
 * subdevice; the vendor uses its address as the spin lock too.  irq[] and
 * enabled[] overlap from the fourth irq on (the vendor loop has no bound).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txirq_oracle): every hook call in order
 * with its arguments, the irq info / subdevice words, the return values.
 */
#ifndef TX_ISP_T32_TXIRQ_H
#define TX_ISP_T32_TXIRQ_H

#include "tx_isp_t32_txos.h"

/* struct t32_tx_irq_info (offsets) */
#define T32_IRQ_NUM(i)		(0U + 4U * (i))
#define T32_IRQ_ON(i)		(12U + 4U * (i))
#define T32_IRQ_COUNT		24U
#define T32_IRQ_ENABLE_FN	28U		/* void (*)(info, index) */
#define T32_IRQ_DISABLE_FN	32U

#define T32_SD_IRQ		156U		/* irq info inside a subdevice */
#define T32_SD_IRQ_ON(i)	(T32_SD_IRQ + T32_IRQ_ON(i))
#define T32_SD_IRQ_ENABLE	(T32_SD_IRQ + T32_IRQ_ENABLE_FN)
#define T32_SD_IRQ_DISABLE	(T32_SD_IRQ + T32_IRQ_DISABLE_FN)

#define T32_IRQF_ONESHOT	0x2000U
#define T32_EINTR		4U
#define T32_MAX_ERRNO		4095U
#define T32_IS_ERR(a)		((u32)(a) >= (u32)-T32_MAX_ERRNO)

typedef void (*t32_tx_irq_fn)(u32 info, u32 index);

/* provided by the driver glue (kernel) or by the oracle (host) */
void t32_tx_os_enable_irq(u32 irq);
void t32_tx_os_disable_irq(u32 irq);
void t32_tx_os_free_irq(u32 irq, u32 dev);
int t32_tx_os_platform_get_irq(u32 pdev, u32 index);
int t32_tx_os_request_threaded_irq(u32 irq, u32 handler, u32 thread, u32 flags, u32 name, u32 dev);
void t32_tx_os_spin_lock_init(u32 lock);
u32 t32_tx_os_spin_lock_irqsave(u32 lock);
void t32_tx_os_spin_unlock_irqrestore(u32 lock, u32 flags);
void t32_tx_os_vic_intp_enable(u32 index);
void t32_tx_os_vic_intp_disable(u32 index);
void t32_tx_isp_irq_handle(void);
void t32_tx_isp_irq_thread_handle(void);

static inline void t32_tx_irq_call(u32 fn, u32 info, u32 index)
{
	((t32_tx_irq_fn)T32_TX_P(fn))(info, index);
}

/* tx_isp_enable_irq (0xd0d0): enable_irq(info->irq[index]) */
static inline void t32_tx_isp_irq_enable(u32 info, u32 index)
{
	t32_tx_os_enable_irq(t32_tx_ld(info, T32_IRQ_NUM(index)));
}

/* tx_isp_disable_irq (0xd0e8) */
static inline void t32_tx_isp_irq_disable(u32 info, u32 index)
{
	t32_tx_os_disable_irq(t32_tx_ld(info, T32_IRQ_NUM(index)));
}

/* tx_isp_request_irq: every platform irq of pdev, requested and left disabled */
static inline int t32_tx_isp_request_irq(u32 pdev, u32 info)
{
	u32 i;
	int irq;

	if (!pdev || !info) {
		t32_tx_os_isp_printf(2, "%s[%d] the parameters are invalid!\n", "tx_isp_request_irq", 78);
		return -(int)T32_EINVAL;
	}
	for (i = 0;; i++) {
		irq = t32_tx_os_platform_get_irq(pdev, i);
		if (irq < 0) {
			t32_tx_st(info, T32_IRQ_NUM(i), 0);
			t32_tx_st(info, T32_IRQ_COUNT, i);
			return 0;
		}
		if (t32_tx_os_request_threaded_irq((u32)irq, T32_TX_A(t32_tx_isp_irq_handle),
						   T32_TX_A(t32_tx_isp_irq_thread_handle), T32_IRQF_ONESHOT,
						   t32_tx_ld(pdev, 0), info)) {
			t32_tx_os_isp_printf(2, "%s[%d] Failed to request irq(%d).\n", "tx_isp_request_irq", 96, irq);
			t32_tx_st(info, T32_IRQ_NUM(i), 0);
			return -(int)T32_EINTR;
		}
		t32_tx_os_spin_lock_init(info);
		t32_tx_st(info, T32_IRQ_NUM(i), (u32)irq);
		t32_tx_st(info, T32_IRQ_ENABLE_FN, T32_TX_A(t32_tx_isp_irq_enable));
		t32_tx_st(info, T32_IRQ_DISABLE_FN, T32_TX_A(t32_tx_isp_irq_disable));
		t32_tx_os_disable_irq((u32)irq);
		t32_tx_st(info, T32_IRQ_ON(i), 0);
	}
}

/* tx_isp_free_irq */
static inline void t32_tx_isp_free_irq(u32 info)
{
	int i;

	if (!info)
		return;
	for (i = 0; i < (int)t32_tx_ld(info, T32_IRQ_COUNT); i++) {
		u32 irq = t32_tx_ld(info, T32_IRQ_NUM(i));

		if (irq) {
			t32_tx_os_free_irq(irq, info);
			t32_tx_st(info, T32_IRQ_NUM(i), 0);
		}
	}
}

/* tx_isp_enable_irq (0xb977c, core) and tx_ldc_enable_irq: irq 0 of the subdevice */
static inline void t32_tx_sd_irq_enable(u32 sd)
{
	u32 flags = t32_tx_os_spin_lock_irqsave(sd + T32_SD_IRQ), fn = t32_tx_ld(sd, T32_SD_IRQ_ENABLE);

	if (fn && !t32_tx_ld(sd, T32_SD_IRQ_ON(0))) {
		t32_tx_irq_call(fn, sd + T32_SD_IRQ, 0);
		t32_tx_st(sd, T32_SD_IRQ_ON(0), 1);
	}
	t32_tx_os_spin_unlock_irqrestore(sd + T32_SD_IRQ, flags);
}

/* tx_isp_disable_irq (0xb9800, core): only from the "enabled" state 1 */
static inline void t32_tx_core_irq_disable(u32 sd)
{
	u32 flags = t32_tx_os_spin_lock_irqsave(sd + T32_SD_IRQ), fn = t32_tx_ld(sd, T32_SD_IRQ_DISABLE);

	if (fn && t32_tx_ld(sd, T32_SD_IRQ_ON(0)) == 1) {
		t32_tx_irq_call(fn, sd + T32_SD_IRQ, 0);
		t32_tx_st(sd, T32_SD_IRQ_ON(0), 0);
	}
	t32_tx_os_spin_unlock_irqrestore(sd + T32_SD_IRQ, flags);
}

static inline void t32_tx_ldc_irq_enable(u32 sd)
{
	if (!sd || T32_IS_ERR(sd))
		return;
	t32_tx_sd_irq_enable(sd);
}

/* tx_ldc_disable_irq: from any non-zero state */
static inline void t32_tx_ldc_irq_disable(u32 sd)
{
	u32 flags, fn;

	if (!sd || T32_IS_ERR(sd))
		return;
	flags = t32_tx_os_spin_lock_irqsave(sd + T32_SD_IRQ);
	fn = t32_tx_ld(sd, T32_SD_IRQ_DISABLE);
	if (fn && t32_tx_ld(sd, T32_SD_IRQ_ON(0))) {
		t32_tx_irq_call(fn, sd + T32_SD_IRQ, 0);
		t32_tx_st(sd, T32_SD_IRQ_ON(0), 0);
	}
	t32_tx_os_spin_unlock_irqrestore(sd + T32_SD_IRQ, flags);
}

/*
 * tx_vic_enable_irq(index) on the VIC subdevice (vendor global d9348, an
 * ERR_PTR counts as none).  Vendor flow: an index > 0 only gets its
 * "enabled" flag; index <= 0 runs the enable callback and the VIC
 * interrupt enable first.
 */
static inline void t32_tx_vic_irq_enable(u32 vic, u32 index)
{
	u32 flags, fn, on;

	if (!vic || T32_IS_ERR(vic))
		return;
	flags = t32_tx_os_spin_lock_irqsave(vic + T32_SD_IRQ);
	fn = t32_tx_ld(vic, T32_SD_IRQ_ENABLE);
	on = vic + T32_SD_IRQ_ON(index);
	if (fn && !t32_tx_ld(on, 0)) {
		if ((int)index <= 0) {
			t32_tx_irq_call(fn, vic + T32_SD_IRQ, index);
			t32_tx_os_vic_intp_enable(index & 0xff);
		}
		t32_tx_st(on, 0, 1);
	}
	t32_tx_os_spin_unlock_irqrestore(vic + T32_SD_IRQ, flags);
}

/* tx_vic_disable_irq(index) */
static inline void t32_tx_vic_irq_disable(u32 vic, u32 index)
{
	u32 flags, fn, on;

	if (!vic || T32_IS_ERR(vic))
		return;
	flags = t32_tx_os_spin_lock_irqsave(vic + T32_SD_IRQ);
	fn = t32_tx_ld(vic, T32_SD_IRQ_DISABLE);
	on = vic + T32_SD_IRQ_ON(index);
	if (fn && t32_tx_ld(on, 0)) {
		t32_tx_os_vic_intp_disable(index & 0xff);
		t32_tx_irq_call(fn, vic + T32_SD_IRQ, index);
		t32_tx_st(on, 0, 0);
	}
	t32_tx_os_spin_unlock_irqrestore(vic + T32_SD_IRQ, flags);
}

#endif
