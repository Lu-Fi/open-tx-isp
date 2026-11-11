/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 block glue, remaining functions (one block per vendor group).
 *
 * - tisp_awb_deinit: free the per channel AWB buffers.
 *
 * OS calls are hooks.  Pointers the vendor keeps in its tables are 32-bit
 * words (MIPS32 kernel), read and cleared in place.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (one oracle per block, named at each block).
 */
#ifndef TX_ISP_T32_BLKREST_H
#define TX_ISP_T32_BLKREST_H

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
#endif
#ifndef T32_HOST_U16
#define T32_HOST_U16
typedef uint16_t u16;
#endif
#endif

static inline u8 *t32_br_ptr(u32 addr) { return (u8 *)(uintptr_t)addr; }
static inline u32 t32_br_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline void t32_br_wr32(u8 *p, u32 v) { memcpy(p, &v, 4); }

/* ---- tisp_awb_deinit (vendor 0x3b89c), oracle t32_awbdein_oracle ---- */

/* t32_data_e16b4[ch]: AWB work block (kmalloc) */
#define T32_AWBD_WK_BUF1	4	/* kmalloc */
#define T32_AWBD_WK_BUF2	8	/* kmalloc */
#define T32_AWBD_WK_BUF3	12	/* kmalloc */
#define T32_AWBD_WK_VBUF	28	/* vmalloc */
/* t32_data_e1694[ch]: AWB state (vmalloc) */
#define T32_AWBD_ST_BUF		4	/* kmalloc */

struct t32_awbd_ops {
	void (*kfree)(void *p);
	void (*vfree)(void *p);
};

struct t32_awbd {
	const struct t32_awbd_ops *ops;
	u32 *zone;	/* t32_data_e16a4[4]: zone buffer (kmalloc) */
	u32 *work;	/* t32_data_e16b4[4] */
	u32 *state;	/* t32_data_e1694[4] */
};

/* free *slot through fn when it is set, then clear it */
static inline void t32_awbd_free(u8 *slot, void (*fn)(void *p))
{
	u32 p = t32_br_rd32(slot);

	if (p) {
		fn(t32_br_ptr(p));
		t32_br_wr32(slot, 0);
	}
}

/* tisp_awb_deinit: always 0 */
static inline int t32_awb_deinit(struct t32_awbd *a, u32 ch)
{
	if (a->zone[ch]) {
		a->ops->kfree(t32_br_ptr(a->zone[ch]));
		a->zone[ch] = 0;
	}
	if (a->work[ch]) {
		u8 *w = t32_br_ptr(a->work[ch]);

		t32_awbd_free(w + T32_AWBD_WK_VBUF, a->ops->vfree);
		t32_awbd_free(w + T32_AWBD_WK_BUF1, a->ops->kfree);
		t32_awbd_free(w + T32_AWBD_WK_BUF2, a->ops->kfree);
		t32_awbd_free(w + T32_AWBD_WK_BUF3, a->ops->kfree);
		a->ops->kfree(w);
		a->work[ch] = 0;
	}
	if (a->state[ch]) {
		u8 *s = t32_br_ptr(a->state[ch]);

		t32_awbd_free(s + T32_AWBD_ST_BUF, a->ops->kfree);
		a->ops->vfree(s);
		a->state[ch] = 0;
	}
	return 0;
}

#endif /* TX_ISP_T32_BLKREST_H */
