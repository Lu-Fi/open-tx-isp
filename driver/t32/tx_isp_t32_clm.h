/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 colour mapping (CLM, block 0x10000) from the IQ bin at AddSensor (S4
 * step 5): tisp_clm_init -> tisp_csc_api_get (colour space mode and matrices
 * of the front end), tisp_clm_write_csc_para, tisp_clm_ct_interp at 5000 K,
 * tisp_clm_write_lut (two 420 word tables through the ports 0x320c0/0x320c4
 * and 0x32100/0x32104), tisp_clm_calc_para_refresh, tisp_clm_write_reg and
 * the block id.
 *
 * IQ bin section 20 (40 bytes: +0x23 number of colour temperature zones,
 * +0x24 hysteresis) and the payload behind it: zones x { s16 colour
 * temperature, 1050 s16 } then zones x 1050 bytes.  At a colour temperature
 * between two zones (outside the hysteresis band) both tables are
 * interpolated with the weight 4096 * (ct - lo) / (hi - lo)
 * (tisp_clm_itp); at or below the first / above the last zone that zone is
 * used as it is.
 *
 * The YCbCr -> RGB matrix of the selected colour space (tisp_csc_api_get's
 * second matrix) is computed from the primaries (Kr, Kb) and the range scale
 * instead of being copied: it equals the vendor's six matrices (BT.601,
 * BT.709, BT.2020, full and limited range).
 *
 * Per-frame path: tisp_clm_dn_params_refresh re-reads the section pointer at
 * tuning base + 0xd608 (tisp_csc_api_get, tisp_clm_write_csc_para, the tables
 * at 5000 K, tisp_clm_write_lut, tisp_clm_calc_para_refresh, tisp_clm_write_reg)
 * and tisp_clm_wake_up rewrites the register set and the cached tables; both
 * work on the per-channel block state (struct t32_clm_ctx).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_clm_oracle, t32_clmf_oracle), including the blend
 * range; the vendor binary was disassembled to confirm the byte compare of
 * t32_clm_ct (0x8a6e4).
 */
#ifndef TX_ISP_T32_CLM_H
#define TX_ISP_T32_CLM_H

#include "tx_isp_t32_tp.h"

#define T32_SEC_CLM		20
#define T32_CLM_SEC_BYTES	40U
#define T32_CLM_ID		0x0134da11U
#define T32_CLM_TABLE		1050U
#define T32_CLM_ZONE_BYTES	(2U + 2U * T32_CLM_TABLE)
#define T32_CLM_CT		5000
#define T32_CLM_WRITES		868
#define T32_CLM_MAX_WRITES	868

/* round(65536 * num / den), half away from zero */
static inline int t32_clm_q16(long long num, long long den)
{
	unsigned long long v = (unsigned long long)(num < 0 ? -num : num) * 65536ULL;
	int q = (int)t32_tp_div64(v * 2 + (unsigned long long)den, 2ULL * (unsigned long long)den);

	return num < 0 ? -q : q;
}

/*
 * Inverse of the front end's matrix for csc mode 0..5 (BT.601, BT.709,
 * BT.2020; full, limited range): rows (y, 0, a), (y, b, c), (y, d, 0) in Q16
 * with y = 1 or 255/219 and the chroma terms scaled by 1 or 255/224.
 * kr, kb in units of 1/10000.
 */
static inline int t32_csc_inverse(unsigned int mode, int m[9])
{
	static const int kr[3] = { 2990, 2126, 2627 }, kb[3] = { 1140, 722, 593 };
	long long d = 10000, r, b, g;
	long long sy = 1, sc = 1, sy_d = 1, sc_d = 1;
	unsigned int c = mode >> 1;

	if (mode > 5)
		return -1;
	r = kr[c];
	b = kb[c];
	g = d - r - b;
	if (mode & 1) {
		sy = 255;
		sy_d = 219;
		sc = 255;
		sc_d = 224;
	}
	m[0] = m[3] = m[6] = t32_clm_q16(sy, sy_d);
	m[1] = m[8] = 0;
	m[2] = t32_clm_q16(2 * (d - r) * sc, d * sc_d);
	m[7] = t32_clm_q16(2 * (d - b) * sc, d * sc_d);
	m[4] = -t32_clm_q16(2 * (d - b) * b * sc, d * g * sc_d);
	m[5] = -t32_clm_q16(2 * (d - r) * r * sc, d * g * sc_d);
	return 0;
}

/* tisp_round_int64(x, sign, 6) and the absolute value the register fields take */
static inline u32 t32_clm_abs6(int x)
{
	int v = t32_front_round6(x);

	return (u32)(v < 0 ? -v : v);
}

/* tisp_clm_calc_para_refresh: ratio and shift of a range (hi - lo) */
static inline void t32_clm_ratio(int diff, u32 *ratio, u32 *shift)
{
	int n = 0, h = diff >> 1;

	if (diff < 3) {
		*ratio = diff == 2 ? 0x200 : 0;
		*shift = 0;
		return;
	}
	if (diff > 0x400)
		n = 10;
	else if (diff > 0x200)
		n = 9;
	else if (diff > 0x100)
		n = 8;
	else if (diff > 0x80)
		n = 7;
	else if (diff > 0x40)
		n = 6;
	else if (diff > 0x20)
		n = 5;
	else if (diff > 0x10)
		n = 4;
	else if (diff > 8)
		n = 3;
	else if (diff > 4)
		n = 2;
	else
		n = 1;
	*ratio = (u16)((h + (0x400 << n)) / diff);
	*shift = n;
}

/* tisp_clm_itp: a towards b by w / 4096, rounded */
static inline int t32_clm_itp(int a, int b, int w)
{
	return (short)(a + (short)(((b - a) * w + 0x800) >> 12));
}

static inline int t32_clm_sat_limits(unsigned int mode, u32 *lo, u32 *hi)
{
	/* the vendor's per-mode limits of tisp_clm_write_reg (low field, high field) */
	static const u16 lim[6][2] = {
		{ 0x242, 0x2da }, { 0x238, 0x2b1 }, { 0x228, 0x28a },
		{ 0x222, 0x272 }, { 0x220, 0x2b6 }, { 0x21c, 0x295 },
	};

	if (mode > 5)
		return -1;
	*lo = lim[mode][0];
	*hi = lim[mode][1];
	return 0;
}

static inline s16 t32_clm_s16(const u8 *p)
{
	return (s16)(p[0] | p[1] << 8);
}

/* the 2 x 420 word table: out[0..1049] shorts, ob[0..1049] bytes at the colour temperature */
/*
 * tisp_clm_ct_interp at the forced init update.  The recovered flow, with its
 * unsigned compares of the sign-extended s16 colour temperatures:
 *  - one zone, or 5000 K not above the first zone: zone 0 as it is;
 *  - 5000 K not below the last zone: the last zone as it is;
 *  - else the first zone v above 5000 K and its lower neighbour v-1: within the
 *    hysteresis of the lower one (ct <= z[v-1] + hyst): zone v-1;
 *    otherwise the vendor tests the first signed byte of the byte table of zone v
 *    against the hysteresis (0x8a6e4: lb, subu, sltu; a colour temperature may
 *    have been meant, the binary reads the byte): ct >= byte - hyst takes zone v,
 *    else the blend runs for ct < z[v] - hyst with the weight
 *    4096 * (ct - lo) / (hi - lo), and for a larger ct the tables stay unchanged
 *    (zero at init).
 */
static inline int t32_clm_ct(const u8 *sec, const u8 *lut, u32 lutbytes, int ct, s16 *tab, u8 *tb)
{
	unsigned int count = sec[0x23], hyst = sec[0x24], v, i, z0 = 0, z1 = 0;
	const u8 *bytes;
	u32 w = 0;
	u32 uct = (u32)ct;

	if (count < 1 || lutbytes < count * (T32_CLM_ZONE_BYTES + T32_CLM_TABLE))
		return -1;
	for (i = 0; i < T32_CLM_TABLE; i++) {
		tab[i] = 0;
		tb[i] = 0;
	}
	bytes = lut + count * T32_CLM_ZONE_BYTES;
	if (count == 1 || !((u32)(int)t32_clm_s16(lut) < uct)) {
		z0 = z1 = 0;
	} else if (!(uct < (u32)(int)t32_clm_s16(lut + (count - 1) * T32_CLM_ZONE_BYTES))) {
		z0 = z1 = count - 1;
	} else {
		for (v = 1; v < count && (u32)(int)t32_clm_s16(lut + v * T32_CLM_ZONE_BYTES) < uct; v++)
			;
		if (v >= count)
			return 0;
		z0 = v - 1;
		z1 = v;
		{
			u32 lo = (u32)(int)t32_clm_s16(lut + z0 * T32_CLM_ZONE_BYTES) + hyst;

			if (!(lo < uct)) {
				z1 = z0;
			} else if (!(uct < (u32)((int)(signed char)bytes[v * T32_CLM_TABLE] - (int)hyst))) {
				z0 = z1;
			} else {
				u32 hi = (u32)(int)t32_clm_s16(lut + z1 * T32_CLM_ZONE_BYTES) - hyst;

				if (!(uct < hi))
					return 0;
				/* lo < ct < hi here, so the divisor is not zero */
				w = ((uct - lo) << 12) / (hi - lo);
			}
		}
	}
	for (i = 0; i < T32_CLM_TABLE; i++) {
		int a = t32_clm_s16(lut + z0 * T32_CLM_ZONE_BYTES + 2 + 2 * i);
		int b = t32_clm_s16(lut + z1 * T32_CLM_ZONE_BYTES + 2 + 2 * i);
		int ba = (signed char)bytes[z0 * T32_CLM_TABLE + i];
		int bb = (signed char)bytes[z1 * T32_CLM_TABLE + i];

		tab[i] = (s16)(z0 == z1 ? a : t32_clm_itp(a, b, (int)w));
		tb[i] = (u8)(z0 == z1 ? ba : t32_clm_itp(ba, bb, (int)w));
	}
	return 0;
}

/*
 * mode: colour space mode (CSC section byte 0x5b), m1: the front end's
 * RGB -> YCbCr matrix of that mode (t32_csc_modes[mode].m); lut / lutbytes:
 * the payload behind section 20.
 */
static inline int t32_clm_init(const u8 *sec, const u8 *lut, u32 lutbytes, unsigned int mode,
			       const int m1[9], struct t32_front_write *w)
{
	static s16 tab[T32_CLM_TABLE];
	static u8 tb[T32_CLM_TABLE];
	int m2[9], port, o, i;
	unsigned int n = 0;
	u32 lo, hi, ratio1, shift1, ratio2, shift2, a, b, c, d, y0, y1, y2;
	static const u32 ports[2][2] = { { 0x320c0, 0x320c4 }, { 0x32100, 0x32104 } };

	if (t32_front_le32(sec) != T32_CLM_ID || t32_csc_inverse(mode, m2) ||
	    t32_clm_sat_limits(mode, &lo, &hi) || t32_clm_ct(sec, lut, lutbytes, T32_CLM_CT, tab, tb))
		return -1;
	a = t32_clm_abs6(m2[2]);
	b = t32_clm_abs6(m2[4]);
	c = t32_clm_abs6(m2[5]);
	d = t32_clm_abs6(m2[7]);
	y0 = t32_clm_abs6(m1[0]);
	y1 = t32_clm_abs6(m1[1]);
	y2 = t32_clm_abs6(m1[2]);

	/* tisp_clm_write_csc_para */
	T32_FWR(w, n, 0x10044, (d & 0x7ff) << 16 | (a & 0x7ff));
	T32_FWR(w, n, 0x10048, (b & 0x1ff) << 16 | (c & 0x3ff));
	T32_FWR(w, n, 0x1004c, (y1 & 0x3ff) << 16 | (y0 & 0x1ff));
	T32_FWR(w, n, 0x10050, y2 & 0xff);
	T32_FWR(w, n, 0x10054, (hi & 0x3ff) << 16 | (lo & 0x3ff));
	T32_FWR(w, n, 0x10010, 1);
	/* tisp_clm_write_lut */
	for (port = 0; port < 2; port++) {
		T32_FWR(w, n, ports[port][0], 0x101);
		for (o = 0; o < 15; o++) {
			for (i = 0; i < 7; i++) {
				const u8 *p = tb + port * 35 + o * 70 + i * 5;
				const s16 *q = tab + port * 35 + o * 70 + i * 5;
				u32 q1 = (u16)q[1] & 0x1ff, q3 = (u16)q[3];

				T32_FWR(w, n, ports[port][1], (u32)(p[1] & 0x7f) << 7 | (u32)(p[2] & 0x7f) << 14 |
					(p[0] & 0x7fU) | (u32)(p[3] & 0x7f) << 21 | (u32)p[4] << 28);
				T32_FWR(w, n, ports[port][1], (p[4] & 0x7fU) >> 4);
				T32_FWR(w, n, ports[port][1], ((u32)(u16)q[2] & 0x1ff) << 18 | q1 << 9 | q1 | q3 << 27);
				T32_FWR(w, n, ports[port][1], ((q3 & 0x1ff) >> 5) | ((u32)(u16)q[4] & 0x1ff) << 4);
			}
		}
		T32_FWR(w, n, ports[port][0], 0x680102);
	}
	T32_FWR(w, n, 0x10040, t32_tp_le16(sec + 4) & 0x7ff);
	T32_FWR(w, n, 0x10010, 1);
	/* tisp_clm_calc_para_refresh, tisp_clm_write_reg */
	t32_clm_ratio((int)t32_tp_le16(sec + 0x1c) - (int)t32_tp_le16(sec + 0x1a), &ratio1, &shift1);
	t32_clm_ratio((int)t32_tp_le16(sec + 0x20) - (int)t32_tp_le16(sec + 0x1e), &ratio2, &shift2);
	T32_FWR(w, n, 0x10040, t32_tp_le16(sec + 4) & 0x7ff);
	T32_FWR(w, n, 0x10044, (d & 0x7ff) << 16 | (a & 0x7ff));
	T32_FWR(w, n, 0x10048, (b & 0x1ff) << 16 | (c & 0x3ff));
	T32_FWR(w, n, 0x1004c, (y1 & 0x3ff) << 16 | (y0 & 0x1ff));
	T32_FWR(w, n, 0x10050, y2 & 0xff);
	T32_FWR(w, n, 0x10054, (hi & 0x3ff) << 16 | (lo & 0x3ff));
	T32_FWR(w, n, 0x10090, (u32)(sec[0x22] & 1) << 12 | (sec[0x25] & 1U) | (u32)(sec[0x26] & 1) << 4 |
		(u32)(sec[0x27] & 1) << 8);
	T32_FWR(w, n, 0x10094, (t32_tp_le16(sec + 0x10) & 0x3ff) << 16 | (t32_tp_le16(sec + 0xe) & 0x3ff));
	T32_FWR(w, n, 0x10098, (t32_tp_le16(sec + 0x14) & 0x3ff) << 16 | (t32_tp_le16(sec + 0x12) & 0x3ff));
	T32_FWR(w, n, 0x1009c, (t32_tp_le16(sec + 0x18) & 0x3ff) << 16 | (t32_tp_le16(sec + 0x16) & 0x3ff));
	T32_FWR(w, n, 0x100a0, (t32_tp_le16(sec + 0x1c) & 0x7ff) << 16 | (t32_tp_le16(sec + 0x1a) & 0x7ff));
	T32_FWR(w, n, 0x100a4, (t32_tp_le16(sec + 0x20) & 0x7ff) << 16 | (t32_tp_le16(sec + 0x1e) & 0x7ff));
	T32_FWR(w, n, 0x100a8, (ratio2 & 0x3ff) << 16 | (ratio1 & 0x3ff));
	T32_FWR(w, n, 0x100ac, (shift2 & 0xf) << 8 | (shift1 & 0xf));
	T32_FWR(w, n, 0x10010, 1);
	T32_FWR(w, n, 0x10000, T32_CLM_ID);
	return (int)n;
}

/* tuning base offset of section 20 (the vendor's dd0a4[ch] + 0xd608) */
#define T32_CLM_SEC_OFF		0xd608U
/*
 * The framing path interpolates the tables at colour temperature 1, i.e. below
 * the first zone: the recovered flow's tisp_clm_ct_interp(ch, lut, 1) restores
 * the tables of zone 0 (the forced init update uses 5000 K, T32_CLM_CT).
 */
#define T32_CLM_FRAME_CT	1

/*
 * Per-frame path.  The vendor keeps one block state per channel behind
 * t32_data_e28d0[ch]: the section pointer (+0), the YCbCr -> RGB matrix of
 * tisp_csc_api_get (+4), the payload pointer (+0x60) and the range ratios of
 * tisp_clm_calc_para_refresh.  Our struct holds the same values.
 */
struct t32_clm_ctx {
	u8 *sec;			/* section 20: tuning base + 0xd608 */
	int csc[9];			/* YCbCr -> RGB matrix of the mode */
	u8 *lut;			/* payload behind section 20 */
	u32 lutbytes;
	unsigned int mode;		/* colour space mode of the front end */
	u32 ratio[2], shift[2];		/* tisp_clm_calc_para_refresh */
	s16 tab[T32_CLM_TABLE];		/* tisp_clm_ct_interp: 2 x 420 words */
	u8 tb[T32_CLM_TABLE];
};

/* tisp_clm_write_csc_para: matrix, trigger and the saturation range of the mode */
static inline void t32_clm_csc_para(const struct t32_clm_ctx *ctx, const int m1[9],
				    struct t32_front_write *w, unsigned int *n)
{
	u32 lo = 0, hi = 0;

	t32_clm_sat_limits(ctx->mode, &lo, &hi);
	T32_FWR(w, *n, 0x10044, (t32_clm_abs6(ctx->csc[7]) & 0x7ff) << 16 |
		(t32_clm_abs6(ctx->csc[2]) & 0x7ff));
	T32_FWR(w, *n, 0x10048, (t32_clm_abs6(ctx->csc[4]) & 0x1ff) << 16 |
		(t32_clm_abs6(ctx->csc[5]) & 0x3ff));
	T32_FWR(w, *n, 0x1004c, (t32_clm_abs6(m1[1]) & 0x3ff) << 16 | (t32_clm_abs6(m1[0]) & 0x1ff));
	T32_FWR(w, *n, 0x10050, t32_clm_abs6(m1[2]) & 0xff);
	T32_FWR(w, *n, 0x10054, (hi & 0x3ff) << 16 | (lo & 0x3ff));
	T32_FWR(w, *n, 0x10010, 1);
}

/* tisp_clm_write_lut: the 2 x 420 word table through the two LUT ports */
static inline void t32_clm_lut(const struct t32_clm_ctx *ctx, struct t32_front_write *w,
			       unsigned int *n)
{
	const s16 *tab = ctx->tab;
	const u8 *tb = ctx->tb;
	int port, o, i;

	for (port = 0; port < 2; port++) {
		u32 pl = 0x320c0 + (u32)port * 0x40, pd = 0x320c4 + (u32)port * 0x40;

		T32_FWR(w, *n, pl, 0x101);
		for (o = 0; o < 15; o++) {
			for (i = 0; i < 7; i++) {
				const u8 *p = tb + port * 35 + o * 70 + i * 5;
				const s16 *q = tab + port * 35 + o * 70 + i * 5;
				u32 q1 = (u16)q[1] & 0x1ff, q3 = (u16)q[3];

				T32_FWR(w, *n, pd, (u32)(p[1] & 0x7f) << 7 | (u32)(p[2] & 0x7f) << 14 |
					(p[0] & 0x7fU) | (u32)(p[3] & 0x7f) << 21 | (u32)p[4] << 28);
				T32_FWR(w, *n, pd, (p[4] & 0x7fU) >> 4);
				T32_FWR(w, *n, pd, ((u32)(u16)q[2] & 0x1ff) << 18 | q1 << 9 | q1 | q3 << 27);
				T32_FWR(w, *n, pd, ((q3 & 0x1ff) >> 5) | ((u32)(u16)q[4] & 0x1ff) << 4);
			}
		}
		T32_FWR(w, *n, pl, 0x680102);
	}
	T32_FWR(w, *n, 0x10040, t32_tp_le16(ctx->sec + 4) & 0x7ff);
	T32_FWR(w, *n, 0x10010, 1);
}

/* tisp_clm_write_reg: section fields, the range ratios and the trigger */
static inline void t32_clm_regs(const struct t32_clm_ctx *ctx, const int m1[9],
				struct t32_front_write *w, unsigned int *n)
{
	const u8 *sec = ctx->sec;
	u32 lo = 0, hi = 0;

	t32_clm_sat_limits(ctx->mode, &lo, &hi);
	T32_FWR(w, *n, 0x10040, t32_tp_le16(sec + 4) & 0x7ff);
	T32_FWR(w, *n, 0x10044, (t32_clm_abs6(ctx->csc[7]) & 0x7ff) << 16 |
		(t32_clm_abs6(ctx->csc[2]) & 0x7ff));
	T32_FWR(w, *n, 0x10048, (t32_clm_abs6(ctx->csc[4]) & 0x1ff) << 16 |
		(t32_clm_abs6(ctx->csc[5]) & 0x3ff));
	T32_FWR(w, *n, 0x1004c, (t32_clm_abs6(m1[1]) & 0x3ff) << 16 | (t32_clm_abs6(m1[0]) & 0x1ff));
	T32_FWR(w, *n, 0x10050, t32_clm_abs6(m1[2]) & 0xff);
	T32_FWR(w, *n, 0x10054, (hi & 0x3ff) << 16 | (lo & 0x3ff));
	T32_FWR(w, *n, 0x10090, (u32)(sec[0x22] & 1) << 12 | (sec[0x25] & 1U) | (u32)(sec[0x26] & 1) << 4 |
		(u32)(sec[0x27] & 1) << 8);
	T32_FWR(w, *n, 0x10094, (t32_tp_le16(sec + 0x10) & 0x3ff) << 16 |
		(t32_tp_le16(sec + 0xe) & 0x3ff));
	T32_FWR(w, *n, 0x10098, (t32_tp_le16(sec + 0x14) & 0x3ff) << 16 |
		(t32_tp_le16(sec + 0x12) & 0x3ff));
	T32_FWR(w, *n, 0x1009c, (t32_tp_le16(sec + 0x18) & 0x3ff) << 16 |
		(t32_tp_le16(sec + 0x16) & 0x3ff));
	T32_FWR(w, *n, 0x100a0, (t32_tp_le16(sec + 0x1c) & 0x7ff) << 16 |
		(t32_tp_le16(sec + 0x1a) & 0x7ff));
	T32_FWR(w, *n, 0x100a4, (t32_tp_le16(sec + 0x20) & 0x7ff) << 16 |
		(t32_tp_le16(sec + 0x1e) & 0x7ff));
	T32_FWR(w, *n, 0x100a8, (ctx->ratio[1] & 0x3ff) << 16 | (ctx->ratio[0] & 0x3ff));
	T32_FWR(w, *n, 0x100ac, (ctx->shift[1] & 0xf) << 8 | (ctx->shift[0] & 0xf));
	T32_FWR(w, *n, 0x10010, 1);
}

/*
 * tisp_clm_dn_params_refresh: the framing path re-reads the section pointer,
 * refreshes the CSC matrix and the tables at 5000 K, then rewrites the tables
 * and every register.  mode / m1 are the colour space mode and the front end's
 * RGB -> YCbCr matrix the CSC module holds (tisp_csc_api_get).  The vendor
 * takes the indirect LUT port variant when t32_data_d03a0[0] is set; the T32
 * ISP runs the direct path modelled here (the init path above).
 */
static inline int t32_clm_dn_params_refresh(struct t32_clm_ctx *ctx, u8 ch, u8 *tuning_base,
					    unsigned int mode, const int m1[9],
					    struct t32_front_write *w)
{
	unsigned int n = 0;

	(void)ch;
	ctx->sec = tuning_base + T32_CLM_SEC_OFF;
	ctx->mode = mode;
	if (t32_csc_inverse(mode, ctx->csc))
		return -1;
	t32_clm_csc_para(ctx, m1, w, &n);
	if (t32_clm_ct(ctx->sec, ctx->lut, ctx->lutbytes, T32_CLM_FRAME_CT, ctx->tab, ctx->tb))
		return -1;
	t32_clm_lut(ctx, w, &n);
	t32_clm_ratio((int)t32_tp_le16(ctx->sec + 0x1c) - (int)t32_tp_le16(ctx->sec + 0x1a),
		      &ctx->ratio[0], &ctx->shift[0]);
	t32_clm_ratio((int)t32_tp_le16(ctx->sec + 0x20) - (int)t32_tp_le16(ctx->sec + 0x1e),
		      &ctx->ratio[1], &ctx->shift[1]);
	t32_clm_regs(ctx, m1, w, &n);
	return (int)n;
}

/* tisp_clm_wake_up: the register set and the tables cached in the state */
static inline int t32_clm_wake_up(struct t32_clm_ctx *ctx, u8 ch, const int m1[9],
				  struct t32_front_write *w)
{
	unsigned int n = 0;

	(void)ch;
	t32_clm_regs(ctx, m1, w, &n);
	t32_clm_lut(ctx, w, &n);
	return (int)n;
}

#endif /* TX_ISP_T32_CLM_H */
