/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) CLM (colour lookup), module list 19, IQ bin section 20
 * (0x40 bytes) and its payload (sec[0x3b] knots: first all u16 tables of
 * 0x836 bytes = s16 colour temperature + 1050 u16, then all byte tables of
 * 1050 bytes): tisp_clm_init -> tisp_csc_get, tisp_clm_ct_interp (forced),
 * tisp_clm_calc_para_refresh, tisp_clm_t33_write_reg + tisp_clm_t33_write_lut
 * (two LUT halves through the 0x320c4 / 0x32104 ports, 105 entries of four
 * words each).
 *
 * The CT interpolation keeps the vendor's upper threshold, which reads the
 * knot temperature at a u16 stride of 0x41a instead of 0x41b and compares
 * it unsigned (only used between two knots; on PRJ008 it reads 0, so 0 - hyst
 * wraps and every CT between the knot bands interpolates; at the init CT
 * 5000 K the bins take the last knot).  QEMU trace of the vendor module (openimp #388): list 19 word-equal.
 */
#ifndef TX_ISP_T33_CLM_H
#define TX_ISP_T33_CLM_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_CLM		19
#define T33_SEC_CLM		20
#define T33_CLM_SEC_BYTES	0x40U
#define T33_CLM_N		1050U
#define T33_CLM_KNOT16		0x836U
#define T33_CLM_KNOT8		0x41aU

static const struct t33_tcmd t33_clm_tmpl[] = {
	{ 0x10010, 1, 0 }, { 0x10040, 6, 0 }, { 0x10090, 8, 0 }, { 0x320c0, 1, 0 }, { 0x320c4, 420, 1 },
	{ 0x320c0, 1, 0 }, { 0x32100, 1, 0 }, { 0x32104, 420, 1 }, { 0x32100, 1, 0 },
};

/* tisp_clm_itp */
static inline int t33_clm_itp(int a, int b, u32 f)
{
	int x = (int)((u32)(b - a) * f);

	return (s16)(a + (s16)((x >> 12) + ((x >> 11) & 1)));
}

/* tisp_clm_calc_para_refresh: reciprocal of d with its shift */
static inline void t33_clm_recip(int d, u32 *val, u32 *shift)
{
	u32 s;

	if (d < 3) {
		*val = d == 2 ? 0x200 : 0;
		*shift = 0;
		return;
	}
	for (s = 1; s < 10 && d > (2 << s); s++)
		;
	*val = (u16)(((d >> 1) + (0x400 << s)) / d);
	*shift = s;
}

/* tisp_clm_ct_interp (forced): b8 / u16 the 1050 byte / u16 entries */
static inline int t33_clm_ct(const u8 *sec, const u8 *lut, u32 lutbytes, int ct, u8 *b8, u16 *t16)
{
	int n = sec[0x3b], i, lo = 0, j, src = 0;
	const u8 *a16 = lut, *a8 = lut + n * T33_CLM_KNOT16;

#define KCT(k)	((int)(s16)t32_tp_le16(a16 + (k) * T33_CLM_KNOT16))
#define HY(k)	((int)t32_tp_le16(sec + 0x1a + 2 * (k)))
	if (n < 1 || lutbytes < (u32)n * (T33_CLM_KNOT16 + T33_CLM_KNOT8))
		return -1;
	if (n == 1 || ct <= KCT(0)) {
		src = 0;
	} else if (ct >= KCT(n - 1)) {
		src = n - 1;
	} else {
		for (i = 1; i < n && ct > KCT(i); i++)
			;
		if (i >= n)
			return 0;
		lo = i - 1;
		if (ct <= KCT(lo) + HY(lo)) {
			src = lo;
		} else if ((u32)ct < (u32)((int)(s16)t32_tp_le16(a16 + i * 0x834) - HY(i))) {	/* unsigned, as the vendor */
			int up = KCT(i) - HY(i), base = KCT(lo) + HY(lo);
			u32 f;

			if (up <= ct)
				return -1;
			f = (u32)((ct - base) * 0x1000) / (u32)(up - base);
			for (j = 0; j < (int)T33_CLM_N; j++) {
				b8[j] = (u8)t33_clm_itp((s8)a8[lo * T33_CLM_KNOT8 + j], (s8)a8[i * T33_CLM_KNOT8 + j], f);
				t16[j] = (u16)t33_clm_itp((s16)t32_tp_le16(a16 + lo * T33_CLM_KNOT16 + 2 + 2 * j),
							  (s16)t32_tp_le16(a16 + i * T33_CLM_KNOT16 + 2 + 2 * j), f);
			}
			return 0;
		} else {
			src = i;
		}
	}
#undef KCT
#undef HY
	memcpy(b8, a8 + src * T33_CLM_KNOT8, T33_CLM_N);
	for (j = 0; j < (int)T33_CLM_N; j++)
		t16[j] = t32_tp_le16(a16 + src * T33_CLM_KNOT16 + 2 + 2 * j);
	return 0;
}

/* tisp_round_int64(x, 6), then the absolute value */
static inline u32 t33_clm_r6(int x)
{
	int r = (int)((x >> 6) + ((x >> 5) & 1));

	return (u32)(r < 0 ? -r : r);
}

/*
 * Block init into list l (capacity cap words): sec section 20, lut / lutbytes
 * its payload, csc the colour space section (9), ct the init colour
 * temperature.  Returns the list length, -1 on a refused section.
 */
#ifdef __KERNEL__
#define T33_CLM_FN	static noinline_for_stack __maybe_unused int
#else
#define T33_CLM_FN	static inline int
#endif
T33_CLM_FN t33_clm_init(const u8 *sec, const u8 *lut, u32 lutbytes, const u8 *csc, int ct, u32 *l, u32 cap)
{
	static u8 b8[T33_CLM_N];	/* callers are serialised (stream-on) */
	static u16 t16[T33_CLM_N];
	u32 mode, v0, s0, v1, s1, r30, r31, k, j, slot;
	int m1[9], m2[9], len, h;

	if (!(csc[0x62] == 1 || (csc[0x62] == 0 && csc[0x63] < 2)))
		return -1;
	if (t33_clm_ct(sec, lut, lutbytes, ct, b8, t16) < 0)
		return -1;
	len = t33_rcfg_tmpl_init(l, cap, t33_clm_tmpl, ARRAY_SIZE(t33_clm_tmpl));
	if (len <= 0)
		return len;
	/* tisp_csc_get: mode, the two matrices */
	mode = csc[0x5b];
	for (k = 0; k < 9; k++) {
		m1[k] = t33_s32(csc + 4 + 4 * k);
		m2[k] = t33_s32(csc + 0x28 + 4 * k);
	}
	t33_clm_recip((int)t32_tp_le16(sec + 0x14) - (int)t32_tp_le16(sec + 0x12), &v0, &s0);
	t33_clm_recip((int)t32_tp_le16(sec + 0x18) - (int)t32_tp_le16(sec + 0x16), &v1, &s1);
	switch (mode) {
	case 0: r30 = 0x242; r31 = 0x2da; break;
	case 1: r30 = 0x238; r31 = 0x2b1; break;
	case 2: r30 = 0x228; r31 = 0x28a; break;
	case 3: r30 = 0x222; r31 = 0x272; break;
	case 4: r30 = 0x220; r31 = 0x2b6; break;
	case 5: r30 = 0x21c; r31 = 0x295; break;
	case 6:
		r31 = 0x2da;
		if (m1[0] >> 16 > 0) {
			u32 q = 0x8000000U / (u32)(0x10000 - m1[0]);

			r31 = (u32)((int)q >> 1) + (q & 1);
		}
		r30 = 0x242;
		if (m1[2] >> 16 > 0) {
			u32 q = 0x8000000U / (u32)(0x10000 - m1[2]);

			r30 = (u32)((int)q >> 1) + (q & 1);
		}
		break;
	default: r30 = r31 = 0; break;
	}
	/* tisp_clm_t33_write_reg */
	T33_SW(l, 3, t32_tp_le16(sec + 4) & 0x7ff);
	T33_SW(l, 4, (t33_clm_r6(m2[7]) & 0x7ff) << 16 | (t33_clm_r6(m2[2]) & 0x7ff));
	T33_SW(l, 5, (t33_clm_r6(m2[4]) & 0x1ff) << 16 | (t33_clm_r6(m2[5]) & 0x3ff));
	T33_SW(l, 6, (t33_clm_r6(m1[1]) & 0x3ff) << 16 | (t33_clm_r6(m1[0]) & 0x1ff));
	T33_SW(l, 7, t33_clm_r6(m1[2]) & 0xff);
	T33_SW(l, 8, (r31 & 0x3ff) << 16 | (r30 & 0x3ff));
	T33_SW(l, 0xa, (sec[0x3a] & 1U) << 12 | (sec[0x3c] & 1U) | (sec[0x3d] & 1U) << 4 | (sec[0x3e] & 1U) << 8);
	for (k = 0; k < 3; k++)
		T33_SW(l, 0xb + k, (t32_tp_le16(sec + 8 + 4 * k) & 0x3ff) << 16 | (t32_tp_le16(sec + 6 + 4 * k) & 0x3ff));
	T33_SW(l, 0xe, (t32_tp_le16(sec + 0x14) & 0x7ff) << 16 | (t32_tp_le16(sec + 0x12) & 0x7ff));
	T33_SW(l, 0xf, (t32_tp_le16(sec + 0x18) & 0x7ff) << 16 | (t32_tp_le16(sec + 0x16) & 0x7ff));
	T33_SW(l, 0x10, (v1 & 0x3ff) << 16 | (v0 & 0x3ff));
	T33_SW(l, 0x11, (s1 & 0xf) << 8 | (s0 & 0xf));
	T33_SW(l, 1, 1);
	/* tisp_clm_t33_write_lut: both halves of each 70-entry row */
	for (h = 0; h < 2; h++) {
		slot = h ? 0x1be : 0x15;
		T33_SW(l, h ? 0x1bc : 0x13, 0x101);
		for (k = 0; k < 15; k++)
			for (j = 0; j < 7; j++) {
				const u8 *p = b8 + 70 * k + 35 * h + 5 * j;
				const u16 *q = t16 + 70 * k + 35 * h + 5 * j;
				u32 u6 = q[1] & 0x1ff;

				T33_SW(l, slot, (p[1] & 0x7fU) << 7 | (p[2] & 0x7fU) << 14 | (p[0] & 0x7fU) |
						(p[3] & 0x7fU) << 21 | (u32)p[4] << 28);
				T33_SW(l, slot + 1, (p[4] & 0x7fU) >> 4);
				T33_SW(l, slot + 2, (q[2] & 0x1ffU) << 18 | u6 << 9 | u6 | (u32)q[3] << 27);
				T33_SW(l, slot + 3, (q[3] & 0x1ffU) >> 5 | (q[4] & 0x1ffU) << 4);
				slot += 4;
			}
		T33_SW(l, h ? 0x363 : 0x1ba, 0x680102);
	}
	T33_SW(l, 3, t32_tp_le16(sec + 4) & 0x7ff);
	T33_SW(l, 1, 1);
	return len;
}

#endif /* TX_ISP_T33_CLM_H */
