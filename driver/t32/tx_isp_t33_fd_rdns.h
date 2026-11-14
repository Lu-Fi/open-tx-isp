/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 RDNS block frame-done handler (vendor tisp_rdns_t33_frame_done_interrupt).
 * The caller declares tisp_reg_write_t33 / tisp_rcfg_module_cfg_trig / tisp_rcfg_get_dma_state.
 */
#ifndef TX_ISP_T33_FD_RDNS_H
#define TX_ISP_T33_FD_RDNS_H

#include "tx_isp_t33_blk.h"

#define T33_RDNS_MOD	3

static inline u32 t33_rdns_w(const u8 *i, u32 off)
{
	return t33_le32(i + off);
}

/* four 6-bit fields at b, b+4, b+8, b+12 packed to bytes 0..3 */
static inline u32 t33_rdns_pack4(const u8 *i, u32 b)
{
	return (t33_rdns_w(i, b) & 0x3f) |
	       (t33_rdns_w(i, b + 4) & 0x3f) << 8 |
	       (t33_rdns_w(i, b + 8) & 0x3f) << 16 |
	       (t33_rdns_w(i, b + 12) & 0x3f) << 24;
}

/* two 12-bit fields at b, b+4 */
static inline u32 t33_rdns_pack2(const u8 *i, u32 b)
{
	return (t33_rdns_w(i, b) & 0xfff) | (t33_rdns_w(i, b + 4) & 0xfff) << 16;
}

static inline void t33_rdns_wr(u32 ch, u32 cfg, u32 slot, u32 addr, u32 val)
{
	tisp_reg_write_t33(ch, T33_RDNS_MOD, cfg ? slot : 0x20000 + addr, val);
}

/* tisp_rdns_t33_frame_done_interrupt */
static inline int t33_rdns_frame_done(u32 chan, const u32 *initpar, u8 *refresh, u8 cfg,
				      u8 *const *info)
{
	const u8 *i;
	u32 ch = chan & 0xff, k;

	if (initpar[226] != 1)
		return 1;
	if (*refresh == 1) {
		tisp_rcfg_module_cfg_trig(ch, 3);
		*refresh = 0;
		return 1;
	}
	if (*refresh != 2)
		return 1;
	if (tisp_rcfg_get_dma_state(3)) {
		*refresh = 2;
		return 1;
	}
	i = info[ch];
	t33_rdns_wr(ch, cfg, 8, 24756, (u32)i[788] << 16 | i[784]);
	t33_rdns_wr(ch, cfg, 9, 24760, (t33_rdns_w(i, 796) & 0xf) << 16 | (t33_rdns_w(i, 792) & 0xf));
	t33_rdns_wr(ch, cfg, 10, 24764, (u32)i[808] << 16 | i[804]);
	t33_rdns_wr(ch, cfg, 11, 24768, (t33_rdns_w(i, 816) & 0x1f) << 16 | (t33_rdns_w(i, 812) & 0x1f));
	t33_rdns_wr(ch, cfg, 12, 24772, (t33_rdns_w(i, 820) & 0x3f) << 16 | (t33_rdns_w(i, 800) & 0x1f));
	t33_rdns_wr(ch, cfg, 13, 24776, (t33_rdns_w(i, 828) & 0xf) << 16 | (t33_rdns_w(i, 824) & 0xf));
	t33_rdns_wr(ch, cfg, 4, 24644,
		    (t33_rdns_w(i, 404) & 7) << 8 | (t33_rdns_w(i, 408) & 3) << 16 |
		    (t33_rdns_w(i, 400) & 7) | (t33_rdns_w(i, 412) & 3) << 24);
	for (k = 0; k < 8; k++)
		t33_rdns_wr(ch, cfg, 15 + k, 24648, t33_rdns_pack4(i, 416 + 16 * k));
	for (k = 0; k < 8; k++)
		t33_rdns_wr(ch, cfg, 24 + k, 24680, t33_rdns_pack4(i, 544 + 16 * k));
	for (k = 0; k < 6; k++)
		t33_rdns_wr(ch, cfg, 33 + k, 24712, t33_rdns_pack2(i, 672 + 8 * k));
	t33_rdns_wr(ch, cfg, 3, 24640,
		    (t33_rdns_w(i, 396) & 3) << 16 | (t33_rdns_w(i, 720) & 1) << 24 |
		    (t33_rdns_w(i, 392) & 7));
	t33_rdns_wr(ch, cfg, 6, 24736, (t33_rdns_w(i, 728) & 0x1fff) << 16 | (t33_rdns_w(i, 724) & 0x1ff));
	for (k = 0; k < 3; k++) {
		u32 o = 732 + 16 * k;

		t33_rdns_wr(ch, cfg, 40 + k, 24740,
			    i[o] | t33_rdns_w(i, o + 12) << 24 |
			    ((t33_rdns_w(i, o + 4) << 8) & 0xffff) | (u32)i[o + 8] << 16);
	}
	t33_rdns_wr(ch, cfg, 43, 24740, i[780]);
	t33_rdns_wr(ch, cfg, 1, 24608, 1);
	*refresh = 1;
	return 1;
}

#endif /* TX_ISP_T33_FD_RDNS_H */
