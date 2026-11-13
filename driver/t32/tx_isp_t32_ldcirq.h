/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LDC offline device: open / release of the misc device and the LDC interrupt service routine.
 *
 *   t32_ldc_offline_open      ldc_offline_open 0x1042c: init the frame-done completion of the LDC device
 *                             (device + 40), then enable the LDC interrupt; returns 0
 *   t32_ldc_offline_release   ldc_offline_release 0x10504: disable the LDC interrupt, then complete the
 *                             frame-done completion (wakes a waiter); returns 0
 *   t32_ldc_isr               ldc_interrupt_service_routine 0x10110: lib_tisp_ldc_intp_handle(); bit 0 of its
 *                             result = frame done -> complete the completion; always returns 1 (IRQ_HANDLED)
 *
 * The vendor keeps the LDC device in t32_data_d9948 and its interrupt object in t32_data_d9944; here both are
 * fields of the hook context.  The OS calls are hooks (struct t32_ldcirq_hooks).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_ldcirq_oracle).
 */
#ifndef TX_ISP_T32_LDCIRQ_H
#define TX_ISP_T32_LDCIRQ_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_LDC_DEV_DONE	40	/* frame-done completion inside the LDC device */
#define T32_LDC_INTP_DONE	0x1U	/* lib_tisp_ldc_intp_handle: frame done */
#define T32_IRQ_HANDLED		1U

struct t32_ldcirq_hooks {
	void *ctx;
	unsigned char *dev;	/* t32_data_d9948: the LDC device */
	void *irq;		/* t32_data_d9944: the LDC interrupt object */
	void (*init_completion)(void *ctx, void *done);	/* private_init_completion */
	void (*complete)(void *ctx, void *done);	/* private_complete */
	void (*enable_irq)(void *ctx, void *irq);	/* tx_ldc_enable_irq */
	void (*disable_irq)(void *ctx, void *irq);	/* tx_ldc_disable_irq */
	u32 (*intp_handle)(void *ctx);			/* lib_tisp_ldc_intp_handle */
};

static inline u32 t32_ldc_offline_open(const struct t32_ldcirq_hooks *h)
{
	h->init_completion(h->ctx, h->dev + T32_LDC_DEV_DONE);
	h->enable_irq(h->ctx, h->irq);
	return 0;
}

static inline u32 t32_ldc_offline_release(const struct t32_ldcirq_hooks *h)
{
	h->disable_irq(h->ctx, h->irq);
	h->complete(h->ctx, h->dev + T32_LDC_DEV_DONE);
	return 0;
}

static inline u32 t32_ldc_isr(const struct t32_ldcirq_hooks *h)
{
	if (h->intp_handle(h->ctx) & T32_LDC_INTP_DONE)
		h->complete(h->ctx, h->dev + T32_LDC_DEV_DONE);
	return T32_IRQ_HANDLED;
}

#endif
