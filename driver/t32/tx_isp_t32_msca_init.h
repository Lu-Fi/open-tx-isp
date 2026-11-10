/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA channel init: readable C of tisp_msca_init(ch, p).
 *
 * p is the channel's init block: p[0] = width, p[4] = height, p[900] = pipeline count.  The
 * function allocates the 2636 byte parameter block (set 0..2 streaming bytes at +14..+16 = 1,
 * everything else 0) and the 304 byte context (par pointer value = tuning base + 0xef50, all six
 * 25-tap coefficient arrays preset to the flat box 2048 / 6 = 341), records the size, resets
 * the OSD info word, programs the MSCA registers (in shadow mode first the per-set size /
 * crop defaults), stamps the shadow-RAM flag, kicks 0xf0010 and hooks the frame-done irq (33).
 *
 * Differences to the vendor, both on allocation failure: the vendor stores the NULL block, prints
 * an error (isp_printf line 1285) and carries on into memset(NULL); it never checks the context
 * allocation.  The port stops with -1 in both cases (the second silently).  The direct-register
 * alternatives of the vendor's shadow-only pre-block are dead code and not kept.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscak_oracle).
 */
#ifndef TX_ISP_T32_MSCA_INIT_H
#define TX_ISP_T32_MSCA_INIT_H

#include "tx_isp_t32_msca_io.h"
#include "tx_isp_t32_msca_api.h"

#define T32_MSCA_INIT_SETS_SZ	2636
#define T32_MSCA_INIT_CTX_SZ	304

struct t32_msca_init_ops {
	u8 *(*kmalloc)(u32 size);
	void (*lock_init)(void);
	void (*error)(u32 line);		/* isp_printf(2, ...) */
	void (*irq_func_set)(u32 ch, u32 irq);	/* hooks tisp_msca_irq_frame_done */
};

struct t32_msca_init_st {
	u8 *sets[4];		/* t32_data_e2768 */
	u8 *ctx[4];		/* t32_data_e28ac */
	u16 height[4];		/* t32_data_e28a4 */
	u16 width[4];		/* t32_data_e289c */
	u32 osd_info;		/* t32_data_e28bc */
	u32 par_base[4];	/* t32_data_dd0a4 */
};

static inline int t32_msca_init(const struct t32_msca_init_ops *ops, struct t32_ms_io *io,
				struct t32_msca_init_st *st, u32 ch, const u8 *p)
{
	u32 w = t32_msa_r32(p), h = t32_msa_r32(p + 4), wh = w << 16 | h;
	u8 *sb, *cx;
	unsigned int i, s;

	ch &= 0xff;
	ops->lock_init();
	sb = ops->kmalloc(T32_MSCA_INIT_SETS_SZ);
	st->sets[ch] = sb;
	if (!sb) {
		ops->error(1285);
		return -1;
	}
	for (i = 0; i < T32_MSCA_INIT_SETS_SZ; i++)
		sb[i] = 0;
	sb[14] = sb[15] = sb[16] = 1;
	st->height[ch] = h;
	st->width[ch] = w;
	st->osd_info = 0;
	cx = ops->kmalloc(T32_MSCA_INIT_CTX_SZ);
	st->ctx[ch] = cx;
	if (!cx)
		return -1;
	for (i = 0; i < T32_MSCA_INIT_CTX_SZ; i++)
		cx[i] = 0;
	t32_msa_w32(cx, st->par_base[ch] + 0xef50);
	for (i = 0; i < 25; i++)
		for (s = 0; s < 6; s++)
			t32_msa_w16(cx + 4 + 2 * i + 50 * s, 341);

	if (io->shadow) {
		for (s = 0; s < 3; s++) {
			u32 x = 49 + 32 * s, r = 0xf0124 + 0x100 * s;

			t32_ms_tw(io, ch, x - 9, r - 36, 0);
			t32_ms_tw(io, ch, x - 8, r - 32, wh);
			t32_ms_tw(io, ch, x - 7, r - 28, 0);
			t32_ms_tw(io, ch, x - 6, r - 24, wh);
			t32_ms_tw(io, ch, x + 19, r + 76, 0x384012c);
			t32_ms_tw(io, ch, x + 20, r + 80, 0x80008080);
			t32_ms_tw(io, ch, x - 5, r - 20, 0x4000);
			t32_ms_tw(io, ch, x - 4, r - 16, 0x4000);
			t32_ms_tw(io, ch, x + 1, r + 4, 0);
			t32_ms_tw(io, ch, x, r, 0xffffffffu);
		}
	}
	t32_ms_tw(io, ch, 70, 0xf0178, w);
	t32_ms_tw(io, ch, 102, 0xf0278, w);
	t32_ms_tw(io, ch, 134, 0xf0378, w);
	t32_ms_tw(io, ch, 137, 0xf0404, wh);
	t32_ms_tw(io, ch, 37, 0xf0090, 0);
	t32_ms_tw(io, ch, 38, 0xf0094, wh);
	t32_ms_tw(io, ch, 20, 0xf004c, 0);
	t32_ms_tw(io, ch, 17, 0xf0040, 0);
	if (t32_msa_r32(p + 900) != 1) {
		for (s = 0; s < 3; s++) {
			u32 x = 67 + 32 * s, r = 0xf016c + 0x100 * s;

			t32_ms_tw(io, ch, x - 10, r - 40, 1);
			t32_ms_tw(io, ch, x - 2, r - 8, 1);
			t32_ms_tw(io, ch, x - 8, r - 32, 1);
			t32_ms_tw(io, ch, x, r, 1);
		}
	}
	t32_msca_set_shdram_flag(io, ch, 2, 0, 0);
	t32_ms_sw(io, 0xf0004, 0x134da11);
	t32_ms_tw(io, ch, 13, 0xf0010, 1);
	ops->irq_func_set(ch, 33);
	return 0;
}

#endif /* TX_ISP_T32_MSCA_INIT_H */
