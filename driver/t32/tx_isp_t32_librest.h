/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 library functions without a block of their own (one block per vendor function).
 *
 * - tisp_ae_deinit: free the per channel AE buffers.
 * - tisp_intp_static_clear: clear the interrupt statistics snapshot of lib_tisp_debug_info.
 *
 * OS calls are hooks.  Pointers the vendor keeps in its tables are 32-bit
 * words (MIPS32 kernel), read and cleared in place.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (one oracle per block, named at each block).
 */
#ifndef TX_ISP_T32_LIBREST_H
#define TX_ISP_T32_LIBREST_H

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

static inline u8 *t32_lr_ptr(u32 addr) { return (u8 *)(uintptr_t)addr; }
static inline u32 t32_lr_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline void t32_lr_wr32(u8 *p, u32 v) { memcpy(p, &v, 4); }

/* ---- tisp_ae_deinit (vendor 0x319a8), oracle t32_aedein_oracle ---- */

/* t32_data_e1598[ch]: AE statistics block, +4 its buffer (kmalloc) */
#define T32_AED_ST_BUF		4
/* the vendor clears these two words after freeing the buffer at +4 (which stays set) */
#define T32_AED_ST_CLR0		16
#define T32_AED_ST_CLR1		20
/* t32_data_e15a8[ch]: AE context, +4 its tuning block (kmalloc) */
#define T32_AED_CTX_TUNE	4

struct t32_aed {
	void (*kfree)(void *p);
	u32 *state_p;	/* t32_data_e15d8[4]: AE state block P (kmalloc) */
	u32 *state_q;	/* t32_data_e15c8[4]: AE state block Q (kmalloc) */
	u32 *stats;	/* t32_data_e1598[4] */
	u32 *ctx;	/* t32_data_e15a8[4] */
};

/*
 * tisp_ae_deinit.  The vendor has no channel check (ch indexes the four
 * entry tables unbounded) and returns no value (v0 is whatever the last
 * call / address load left; lib_tisp_deinit ignores it).
 */
static inline void t32_ae_deinit(const struct t32_aed *a, u32 ch)
{
	if (a->state_p[ch]) {
		a->kfree(t32_lr_ptr(a->state_p[ch]));
		a->state_p[ch] = 0;
	}
	if (a->state_q[ch]) {
		a->kfree(t32_lr_ptr(a->state_q[ch]));
		a->state_q[ch] = 0;
	}
	if (a->stats[ch]) {
		u8 *s = t32_lr_ptr(a->stats[ch]);
		u32 buf = t32_lr_rd32(s + T32_AED_ST_BUF);

		if (buf) {
			a->kfree(t32_lr_ptr(buf));
			t32_lr_wr32(s + T32_AED_ST_CLR0, 0);
			t32_lr_wr32(s + T32_AED_ST_CLR1, 0);
		}
	}
	if (a->stats[ch]) {
		a->kfree(t32_lr_ptr(a->stats[ch]));
		a->stats[ch] = 0;
	}
	if (a->ctx[ch]) {
		u8 *c = t32_lr_ptr(a->ctx[ch]);
		u32 tune = t32_lr_rd32(c + T32_AED_CTX_TUNE);

		if (tune) {
			a->kfree(t32_lr_ptr(tune));
			t32_lr_wr32(c + T32_AED_CTX_TUNE, 0);
		}
	}
	if (a->ctx[ch]) {
		a->kfree(t32_lr_ptr(a->ctx[ch]));
		a->ctx[ch] = 0;
	}
}

/* ---- tisp_intp_static_clear (vendor 0xacd50), oracle t32_intpclr_oracle ---- */

/*
 * The interrupt statistics snapshot that lib_tisp_debug_info prints
 * (vendor .bss t32_data_e3cf0..e3ef8).
 */
#define T32_IPS_BLK_SIZE	128
#define T32_IPS_PAIR_SIZE	8

struct t32_ips {
	void *(*memcpy)(void *dst, const void *src, size_t n);
	u32 *count;	/* t32_data_e3ef8 */
	u8 *blk_e78;	/* t32_data_e3e78[128] */
	u8 *blk_df8;	/* t32_data_e3df8[128] */
	u8 *pair_df0;	/* t32_data_e3df0[8] */
	u8 *blk_d70;	/* t32_data_e3d70[128] */
	u8 *blk_cf0;	/* t32_data_e3cf0[128] */
};

/*
 * tisp_intp_static_clear.  Vendor UB, kept: the clears are memcpy calls
 * with a NULL source (memset was meant), so on the device the first copy
 * reads address 0 and oopses; the vendor has no caller of this function.
 * Returns what the last memcpy returns (the vendor tail call).
 */
static inline void *t32_intp_static_clear(const struct t32_ips *s)
{
	*s->count = 0;
	s->memcpy(s->blk_e78, NULL, T32_IPS_BLK_SIZE);
	s->memcpy(s->blk_df8, NULL, T32_IPS_BLK_SIZE);
	s->memcpy(s->pair_df0, NULL, T32_IPS_PAIR_SIZE);
	s->memcpy(s->blk_d70, NULL, T32_IPS_BLK_SIZE);
	return s->memcpy(s->blk_cf0, NULL, T32_IPS_BLK_SIZE);
}

#endif
