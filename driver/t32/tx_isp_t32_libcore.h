/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library core reset.
 *
 *   t32_lib_core_reset  lib_tisp_core_reset 0xb6fd8: tisp_reset(core), lib_tisp_intp_clear.  Single-camera mode
 *                       (core word 1196 == 1): __ispcore_single_cam_check_frame_drop(0).  Otherwise clear the
 *                       sensor index (f334c) and the flag bytes f3358[0..3] / f335c, then for MSCA channels 0..2
 *                       of that sensor (state = core word 1132 + (sensor * 3 + chn) * 284):
 *                         channel off (state +208 == 0) or __ispcore_get_frame_drop(state) != 0: channel disable;
 *                         else the first buffer of the channel's done list (+220), if any, is written again
 *                         (FIFO clear, channel enable, FIFO write of its two addresses); with an empty done list
 *                         the first buffer of the free list (+212) of the owner state (+116) is written and
 *                         moved to the tail of the done list (list_move_tail, LIST_POISON1/2 in between);
 *                         neither: channel disable.
 *                       Both modes then tisp_ldc_reg_reflash(0) and return tisp_rcfg_cfg_trig(0).
 *
 * Vendor behaviour kept: the owner index (+116) is not range-checked.  OS calls and block functions are hooks.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libcore_oracle).
 */
#ifndef TX_ISP_T32_LIBCORE_H
#define TX_ISP_T32_LIBCORE_H

#ifndef __KERNEL__
#include <stdint.h>
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

#define T32_CORE_MSCA_CHN	3U
#define T32_CORE_POISON1	((struct t32_core_list *)0x100)
#define T32_CORE_POISON2	((struct t32_core_list *)0x200)

struct t32_core_list {
	struct t32_core_list *next, *prev;
};

/* frame buffer node on the free / done lists */
struct t32_core_fbuf {
	struct t32_core_list list;
	u32 addr0;			/* +8 */
	u32 addr1;			/* +12 */
};

/* per sensor / MSCA channel state (vendor stride 284 bytes) */
struct t32_core_chn {
	u32 owner;			/* +116: state whose free list feeds this channel */
	u32 enabled;			/* +208 */
	struct t32_core_list free;	/* +212 */
	struct t32_core_list done;	/* +220 */
};

/* vendor .bss bytes */
struct t32_core_glob {
	u8 sensor;			/* f334c */
	u8 flags[4];			/* f3358 */
	u8 flag5c;			/* f335c */
};

struct t32_core_hooks {
	void *ctx;
	void (*reset)(void *ctx, void *core);				/* tisp_reset */
	void (*intp_clear)(void *ctx);					/* lib_tisp_intp_clear */
	void (*single_cam_check)(void *ctx, u32 a0);			/* __ispcore_single_cam_check_frame_drop */
	u32 (*get_frame_drop)(void *ctx, struct t32_core_chn *st);	/* __ispcore_get_frame_drop */
	void (*fifo_clear)(void *ctx, u32 chn);				/* tisp_msca_addr_fifo_clear_reg_byindex */
	void (*chx_en)(void *ctx, u32 sensor, u32 chn, u32 en);		/* tisp_msca_chx_en */
	void (*fifo_write)(void *ctx, u32 sensor, u32 chn, u32 a0, u32 a1);	/* tisp_msca_addr_fifo_write */
	void (*ldc_reflash)(void *ctx, u32 a0);				/* tisp_ldc_reg_reflash */
	u32 (*rcfg_trig)(void *ctx, u32 a0);				/* tisp_rcfg_cfg_trig */
};

static inline void t32_core_write_buf(const struct t32_core_hooks *h, const struct t32_core_glob *g, u32 chn,
				      const struct t32_core_fbuf *b)
{
	h->fifo_clear(h->ctx, chn);
	h->chx_en(h->ctx, g->sensor, chn, 1);
	h->fifo_write(h->ctx, g->sensor, chn, b->addr0, b->addr1);
}

static inline u32 t32_lib_core_reset(const struct t32_core_hooks *h, struct t32_core_glob *g, void *core,
				     u32 single_cam, struct t32_core_chn *st_base)
{
	struct t32_core_chn *st;
	struct t32_core_list *done, *n;
	u32 chn;

	h->reset(h->ctx, core);
	h->intp_clear(h->ctx);
	if (single_cam == 1) {
		h->single_cam_check(h->ctx, 0);
	} else {
		g->flag5c = 0;
		g->flags[0] = g->flags[1] = g->flags[2] = g->flags[3] = 0;
		g->sensor = 0;
		for (chn = 0; chn < T32_CORE_MSCA_CHN; chn++) {
			st = &st_base[g->sensor * T32_CORE_MSCA_CHN + chn];
			if (!st->enabled || h->get_frame_drop(h->ctx, st)) {
				h->chx_en(h->ctx, g->sensor, chn, 0);
				continue;
			}
			done = st->done.next == &st->done ? NULL : st->done.next;
			n = st_base[st->owner].free.next;
			if (done) {
				t32_core_write_buf(h, g, chn, (const struct t32_core_fbuf *)done);
			} else if (n != &st_base[st->owner].free && n) {
				t32_core_write_buf(h, g, chn, (const struct t32_core_fbuf *)n);
				/* list_move_tail(n, &st->done) */
				n->next->prev = n->prev;
				n->prev->next = n->next;
				n->next = T32_CORE_POISON1;
				n->prev = T32_CORE_POISON2;
				n->prev = st->done.prev;
				n->next = &st->done;
				st->done.prev->next = n;
				st->done.prev = n;
			} else {
				h->chx_en(h->ctx, g->sensor, chn, 0);
			}
		}
	}
	h->ldc_reflash(h->ctx, 0);
	return h->rcfg_trig(h->ctx, 0);
}

#endif
