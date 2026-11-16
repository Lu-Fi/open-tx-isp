/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AE API accessors: tisp_ae_api_get_scene_par / set_scene_par / get_scene_luma / get_stable /
 * get_luma, tisp_ae_get_at_list / set_at_list / get_ev_list / set_ev_list.
 * A = stAeInfo[ch] (word 1744 points to the tuning block T), R = ae_cache[ch], hist = stAeHistBuff[ch]
 * (word 1 points to the histogram buffers), sn = stat_num[ch].word 1 (buffer index, 2 KiB each).
 * Runtime helpers (cache sync, fixed point division / multiplication) are reached through the ops.
 */
#ifndef TX_ISP_T33_R13F_H
#define TX_ISP_T33_R13F_H

#include "tx_isp_t33_blk.h"

struct t33_aeapi_ops {
	void (*cache_sync)(u32 dev, u32 addr, u32 len, u32 dir);
	u32 (*div32)(u32 sh, u32 a, u32 b);
	u32 (*mult2_32)(u32 sh, u32 a, u32 b);
};

static inline u32 t33_aeapi_rd16(const u8 *p)
{
	return p[0] | p[1] << 8;
}

static inline void t33_aeapi_wr16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static inline void t33_aeapi_wr32(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
	p[off + 2] = (u8)(v >> 16);
	p[off + 3] = (u8)(v >> 24);
}

static inline u8 *t33_aeapi_tun(const u8 *A)
{
	return (u8 *)(unsigned long)t33_le32(A + 1744);
}

static inline const u8 *t33_aeapi_hbuf(const u8 *hist)
{
	return (const u8 *)(unsigned long)t33_le32(hist + 4);
}

/* zone count * statistics scale factors */
static inline u32 t33_aeapi_den(const u8 *A, const u8 *T)
{
	return t33_aeapi_rd16(T + 2770) * t33_aeapi_rd16(T + 2774) * t33_aeapi_rd16(A + 7100) *
	       t33_aeapi_rd16(A + 7130);
}

/* tisp_ae_api_get_luma */
static inline void t33_ae_api_get_luma(const u8 *A, const u8 *hist, u32 sn, u8 *out)
{
	const u8 *T = t33_aeapi_tun(A);
	const u8 *p = t33_aeapi_hbuf(hist) + (sn << 11) + 8;
	u32 sum = 0, i;

	for (i = 0; i < 256; i++)
		sum += i * t33_le32(p + 4 * i);
	*out = (u8)(sum / t33_aeapi_den(A, T));
}

/* tisp_ae_api_get_scene_luma */
static inline void t33_ae_api_get_scene_luma(const struct t33_aeapi_ops *o, const u8 *A, u8 *R,
					     const u8 *hist, u32 sn, u8 *out)
{
	const u8 *T = t33_aeapi_tun(A);
	u32 sh = t33_le32(T + 1736) & 0xffff, sx = sh & 31;
	u32 tot = t33_aeapi_den(A, T), cnt, a1, d, m, t6, idx, sum = 0, k, i;
	const u8 *blk = t33_aeapi_hbuf(hist) + (sn << 11);
	u8 lum = 0;

	o->cache_sync(0, (u32)(unsigned long)blk, 2048, 0);
	cnt = ((t33_le32(blk + 8) >> 1) & 0x3fffffff) + (t33_le32(blk + 1028) & 0x3fffffff);
	a1 = cnt < tot ? tot - cnt : 1;
	idx = (t33_le32(T + 1656) & 0xffff) + 1;
	d = o->div32(sh, a1 << sx, tot << sx);
	m = o->mult2_32(sh, t33_le32(R + idx * 8), d);
	t6 = m ? m : 1;
	t33_ae_api_get_luma(A, hist, sn, &lum);
	if (!lum)
		lum = 1;
	for (i = 0; i < 14; i++)
		t33_aeapi_wr32(R, 1200 + 4 * i, t33_le32(R + 1204 + 4 * i));
	d = o->div32(sh, t6, (u32)lum << sx);
	t33_aeapi_wr32(R, 1256, d >> 2);
	for (k = 0; k < 8; k++)
		sum += (k + 1) * t33_le32(R + 1200 + 4 * k);
	t33_aeapi_wr32(out, 0, sum / 37);
}

/* tisp_ae_api_get_scene_par */
static inline void t33_ae_api_get_scene_par(const struct t33_aeapi_ops *o, const u8 *A, u8 *R,
					    const u8 *hist, u32 sn, u8 *out)
{
	const u8 *T = t33_aeapi_tun(A);
	u32 mode = t33_le32(T + 1684);
	u8 lum = 0;

	if (mode == 0) {
		t33_aeapi_wr32(out, 0, 1);
		out[4] = 0;
		t33_aeapi_wr32(out, 8, 1);
		out[12] = 0;
	} else if (mode == 1) {
		t33_aeapi_wr32(out, 0, 3);
		t33_aeapi_wr32(out, 8, 3);
		out[4] = (u8)(T[2942] - 1);
		out[12] = (u8)(T[2940] - 1);
	} else if (mode == 2) {
		t33_aeapi_wr32(out, 0, 2);
		t33_aeapi_wr32(out, 8, 2);
		out[4] = (u8)(t33_le32(T + 1708) - 1);
		out[12] = (u8)(t33_le32(T + 1712) - 1);
	}
	t33_aeapi_wr32(out, 20, t33_aeapi_rd16(T + 2960));
	t33_aeapi_wr32(out, 16, t33_aeapi_rd16(T + 2960) == 128 ? 0 : 1);
	t33_aeapi_wr32(out, 24, t33_aeapi_rd16(A + 2846) != 1);
	t33_aeapi_wr32(out, 28, t33_le32(T + 368));
	t33_ae_api_get_luma(A, hist, sn, &lum);
	t33_aeapi_wr32(out, 32, lum);
	t33_ae_api_get_scene_luma(o, A, R, hist, sn, out + 36);
}

/* tisp_ae_api_set_scene_par: in = 8 words (modes at 0 / 8, bytes at 4 / 12, ...) */
static inline void t33_ae_api_set_scene_par(u8 *A, const u8 *in)
{
	u8 *T = t33_aeapi_tun(A);
	u32 m = t33_le32(in);

	if (m == 1) {
		t33_aeapi_wr16(T + 2942, 1);
		t33_aeapi_wr32(T, 1704, 1);
		t33_aeapi_wr32(T, 1708, 1);
	} else if (m == 3) {
		t33_aeapi_wr32(T, 1684, 1);
		t33_aeapi_wr16(T + 2942, in[4] + 1U);
	} else if (m == 2) {
		t33_aeapi_wr32(T, 1684, 1);
		t33_aeapi_wr32(T, 1704, 1);
		t33_aeapi_wr32(T, 1708, in[4] + 1U);
	}
	m = t33_le32(in + 8);
	if (m == 1) {
		t33_aeapi_wr16(T + 2940, 1);
		t33_aeapi_wr32(T, 1712, 1);
		t33_aeapi_wr32(T, 1716, 1);
	} else if (m == 3) {
		t33_aeapi_wr32(T, 1684, 1);
		t33_aeapi_wr16(T + 2940, in[12] + 1U);
	} else if (m == 2) {
		t33_aeapi_wr32(T, 1684, 1);
		t33_aeapi_wr32(T, 1712, in[12] + 1U);
		t33_aeapi_wr32(T, 1716, 1);
	}
	if (!t33_le32(in + 16)) {
		t33_aeapi_wr16(T + 2956, 0);
	} else {
		t33_aeapi_wr16(T + 2956, 1);
		t33_aeapi_wr16(T + 2960, t33_le32(in + 20));
	}
	if (t33_le32(in + 24)) {
		t33_aeapi_wr32(T, 368, t33_le32(in + 28));
		t33_aeapi_wr32(T, 372, 0);
		t33_aeapi_wr16(A + 2846, 0);
	}
}

/* tisp_ae_api_get_stable */
static inline void t33_ae_api_get_stable(const u8 *A, u8 *out)
{
	t33_aeapi_wr32(out, 0, t33_aeapi_rd16(A + 2848));
	t33_aeapi_wr32(out, 4, t33_aeapi_rd16(A + 2850));
	t33_aeapi_wr32(out, 8, t33_aeapi_rd16(A + 2852));
}

/* tisp_ae_get_at_list */
static inline int t33_ae_get_at_list(const u8 *A, const u8 *R, u8 *out)
{
	const u8 *T = t33_aeapi_tun(A);
	u32 i;

	for (i = 0; i < 15; i++) {
		t33_aeapi_wr32(out, 8 + 8 * i, t33_aeapi_rd16(T + 2870 + 2 * i));
		t33_aeapi_wr32(out, 12 + 8 * i, 0);
	}
	t33_aeapi_wr32(out, 0, R[1737]);
	return 0;
}

/* tisp_ae_set_at_list */
static inline int t33_ae_set_at_list(const u8 *A, u8 *R, u32 mode, const u8 *list)
{
	u8 *T = t33_aeapi_tun(A);
	u32 s = mode & 0xff, i;

	if (s == 1) {
		for (i = 0; i < 15; i++)
			t33_aeapi_wr16(T + 2870 + 2 * i, t33_le32(list + 8 * i));
	} else {
		memcpy(T + 2870, R + 1664, 30);
	}
	R[1737] = (u8)s;
	R[1285] = 1;
	return 0;
}

/* tisp_ae_get_ev_list */
static inline int t33_ae_get_ev_list(const u8 *A, const u8 *R, u8 *out)
{
	const u8 *T = t33_aeapi_tun(A);
	u32 i;

	for (i = 0; i < 15; i++) {
		t33_aeapi_wr32(out, 8 + 8 * i, t33_le32(T + 1496 + 8 * i));
		t33_aeapi_wr32(out, 12 + 8 * i, t33_le32(T + 1500 + 8 * i));
	}
	t33_aeapi_wr32(out, 0, R[1736]);
	return 0;
}

/* tisp_ae_set_ev_list */
static inline int t33_ae_set_ev_list(const u8 *A, u8 *R, u32 mode, const u8 *list)
{
	u8 *T = t33_aeapi_tun(A);
	u32 s = mode & 0xff, i;

	if (s == 1) {
		for (i = 0; i < 15; i++) {
			t33_aeapi_wr32(T, 1496 + 8 * i, t33_le32(list + 8 * i));
			t33_aeapi_wr32(T, 1500 + 8 * i, t33_le32(list + 8 * i + 4));
		}
	} else {
		memcpy(T + 1496, R + 1544, 120);
	}
	R[1736] = (u8)s;
	R[1285] = 1;
	return 0;
}

#endif /* TX_ISP_T33_R13F_H */
