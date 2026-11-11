/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LCE statistics-done interrupt of the vendor tisp core:
 * tisp_lsf_interrupt_static.  For the current LCE channel (e1d78) the
 * histogram buffer (buffer set +20, 0x9900 bytes) is synced for the CPU,
 * read by tisp_lce_get_hist_data and event 14 is queued.
 * Calls go through struct t32_lsf_ops.
 *
 * Vendor behaviour (named): the event's words other than +8 are not set by
 * the vendor (stack contents); ours sends them as 0.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_lsf_oracle).
 */
#ifndef TX_ISP_T32_LSF_H
#define TX_ISP_T32_LSF_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_LSF_HIST_BYTES	0x9900
#define T32_EVENT_LCE_HIST	14
#define T32_LCE_BUFSET_BYTES	36

struct t32_lsf_ops {
	void (*dma_cache_sync)(void *dev, u32 addr, u32 size, int dir);	/* private_dma_cache_sync */
	void (*get_hist_data)(u32 ch, u32 buf);				/* tisp_lce_get_hist_data */
	void (*event_push)(u32 ch, const u32 *ev);			/* tisp_event_push, 8 words */
};

/* bufsets: e1db8 (36 bytes per channel, +20 histogram buffer) */
static inline int t32_lsf_interrupt_static(const struct t32_lsf_ops *k, u8 ch, const u8 *bufsets)
{
	u32 buf = *(const u32 *)(bufsets + T32_LCE_BUFSET_BYTES * ch + 20), ev[8];

	k->dma_cache_sync(NULL, buf, T32_LSF_HIST_BYTES, 0);
	k->get_hist_data(ch, buf);
	memset(ev, 0, sizeof(ev));
	ev[2] = T32_EVENT_LCE_HIST;
	k->event_push(ch, ev);
	return 1;
}

#endif /* TX_ISP_T32_LSF_H */
