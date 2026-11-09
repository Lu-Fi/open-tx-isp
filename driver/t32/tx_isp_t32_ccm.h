/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 colour correction matrix (CCM, block 0x9000) from the IQ bin at
 * AddSensor (S4 step 7): tisp_ccm_init -> tisp_ccm_interp_by_ct (5000 K),
 * tisp_ccm_interp_by_ev (exposure 0), tisp_ccm_calc_para_refresh,
 * tisp_ccm_write_reg and the block id.
 *
 * IQ bin section 12 (872 bytes) and the payload behind it: +0x364 knots of
 * 10 s32 { colour temperature, 3x3 matrix }, +0x365 the hysteresis in K.
 * The matrix of the knot at or around 5000 K is taken as it is when the
 * temperature is within the hysteresis of a knot, otherwise the nine
 * coefficients are interpolated linearly (clamped to +-0x1fff).  The
 * saturation/ev byte tables of the section are copied when the exposure
 * is below the first threshold (the init case: exposure 0), and the
 * 1/(range) factors of the five ranges are computed with the vendor's
 * power-of-two buckets.
 *
 * Built: the init flow with the vendor's initial state (colour temperature
 * 5000, exposure 0, no API override, no tuning array: section bytes
 * +0x360/+0x361 are cleared as the vendor does, so pass a writable copy).
 * A nonzero +0x38 word selects the fixed matrix of the section (+0x3c, nine
 * s32) instead of the knots, as tisp_ccm_interp_by_ct does.
 * Refused: sections whose +0x35f byte is 0 (that path keeps compiled-in
 * defaults of the vendor driver) and an empty knot table.  The exposure knot
 * interpolation (also with a zero first threshold) is t32_ccm_ev_knots.
 *
 * Per frame: struct t32_ccm keeps the vendor's state; t32_ccm_ct_update and
 * t32_ccm_ev_update are tisp_ccm_ct_update / tisp_ccm_ev_update with the cache
 * of the two interpolations and the API override switches.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_ccm_oracle: init, t32_ccmf_oracle: per frame).
 */
#ifndef TX_ISP_T32_CCM_H
#define TX_ISP_T32_CCM_H

#include "tx_isp_t32_tp.h"


#define T32_SEC_CCM		12
#define T32_CCM_SEC_BYTES	872U
#define T32_CCM_ID		0x0134dacdU
#define T32_CCM_CT		5000
#define T32_CCM_WRITES		24
#define T32_CCM_MAX_WRITES	24

static inline int t32_ccm_i32(const u8 *p)
{
	return (int)(p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24);
}

/*
 * tisp_ccm_sub_interp: a + round((b - a) * f / 4096).  The vendor forms the
 * product in 32 bit (sign-extended) before the rounding shift.
 */
static inline int t32_ccm_sub(u32 f, int a, int b)
{
	long long v = (int)((u32)(b - a) * f);

	return a + (int)((v >> 12) + ((v >> 11) & 1));
}

/* the bucket of tisp_ccm_calc_para_refresh: *shift and 1 / d scaled by 2^(shift + 10) */
static inline u32 t32_ccm_bucket(int d, u8 *shift)
{
	int k, base;

	if (d < 3) {
		*shift = 0;
		return d == 2 ? 0x200 : 0;
	}
	k = d > 0x400 ? 10 : d > 0x200 ? 9 : d > 0x100 ? 8 : d > 0x80 ? 7 : d > 0x40 ? 6 :
	    d > 0x20 ? 5 : d > 0x10 ? 4 : d > 8 ? 3 : d > 4 ? 2 : 1;
	base = 0x400 << k;
	*shift = (u8)k;
	return (u16)((u32)((d >> 1) + base) / (u32)d);
}

/* section u16 */
#define T32_CCM_S16(s, o)	t32_tp_le16((s) + (o))

/*
 * The exposure-dependent values of the section: twelve exposure knots (the
 * thresholds are u32 at +4, ascending) each carrying 22 u16 values, stored as
 * 22 arrays of 12 entries.  { offset in cp, offset of knot 0 in the section }.
 */
static const u16 t32_ccm_ev_map[22][2] = {
	{ 0x4c, 0x316 }, { 0x4e, 0x32e },
	{ 0x50, 0xe0 }, { 0x52, 0xf8 }, { 0x54, 0x110 }, { 0x56, 0x128 }, { 0x58, 0x140 }, { 0x5a, 0x158 },
	{ 0x5c, 0x170 }, { 0x5e, 0x188 }, { 0x60, 0x1a0 }, { 0x62, 0x1b8 }, { 0x64, 0x1d0 }, { 0x66, 0x1e8 },
	{ 0x3c, 0x2b6 }, { 0x3e, 0x2ce }, { 0x40, 0x2e6 }, { 0x42, 0x2fe },
	{ 0x44, 0x24a }, { 0x46, 0x262 }, { 0x48, 0x27e }, { 0x4a, 0x296 },
};

/*
 * tisp_ccm_interp_by_ev, knot part: ev = exposure >> 10 (the init flow passes
 * 0).  Below the first threshold the values of knot 0 are taken, at or above
 * the last (12th) threshold those of knot 11, in between the two neighbouring
 * knots are blended with f = ((ev - lo) * 4096 + (hi - lo) / 2) / (hi - lo)
 * (signed 32 bit operands, unsigned 64 bit division, as the vendor does).
 * The vendor's equal-threshold exit (returns 2) cannot be reached: the walk only
 * gets here with thr[k - 1] <= ev < thr[k].
 */
static inline void t32_ccm_ev_knots(const u8 *sec, u8 *cp, u32 ev)
{
	int k, i;

	for (k = 0; k < 12; k++) {
		u32 thr = t32_front_le32(sec + 4 + 4 * k);

		if (ev >= thr) {
			if (k != 11)
				continue;
			for (i = 0; i < 22; i++)
				t32_tp_put16(cp + t32_ccm_ev_map[i][0], T32_CCM_S16(sec, t32_ccm_ev_map[i][1] + 22));
			return;
		}
		if (k == 0) {
			for (i = 0; i < 22; i++)
				t32_tp_put16(cp + t32_ccm_ev_map[i][0], T32_CCM_S16(sec, t32_ccm_ev_map[i][1]));
			return;
		}
		{
			u32 prev = t32_front_le32(sec + 4 * k);
			u32 delta = thr - prev;
			long long num, den;
			u32 f;

			den = (int)delta;
			num = (long long)(int)(ev - prev) * 4096 + (den >> 1);
			f = (u32)t32_tp_div64((unsigned long long)num, (unsigned long long)den);
			for (i = 0; i < 22; i++) {
				u32 o = t32_ccm_ev_map[i][1] + 2 * (k - 1);

				t32_tp_put16(cp + t32_ccm_ev_map[i][0],
					     t32_ccm_sub(f, T32_CCM_S16(sec, o), T32_CCM_S16(sec, o + 2)));
			}
			return;
		}
	}
}

/*
 * The vendor's per-channel state of the block (tisp_ccm_init allocates it and
 * copies the compiled-in defaults d6d7c / d6d58 / d6de8 / d6d30 into it).
 */
struct t32_ccm {
	u8 *sec;		/* section 12, live: the vendor edits it (+0x3c, +0x35f) */
	const u8 *lut;		/* knot payload, sec[0x364] knots of 40 bytes */
	u32 lutbytes;
	u8 cp[0x68];		/* parameter block */
	u32 mat_out[9];		/* the matrix last handed out (a copy of cp[0..36)) */
	u32 ev_lo, ev_hi;	/* exposure of the last accepted ev update */
	u32 ct, ct_code;	/* colour temperature and knot band of the last ct update */
	u8 api_on, api_ev;	/* override switches of the API (d6d30 +0, +1) */
	u32 api_mat[9];		/* API override matrix (d6d30 +4) */
};

/* the compiled-in parameter block d6d7c (data.c:25868): identity matrix, limits, unit gains */
static const u32 t32_ccm_cp_default[26] = {
	0x400, 0, 0, 0, 0x400, 0, 0, 0, 0x400,
	0, 0, 0, 0, 0, 0,
	0x00800010, 0x06000400, 0x00800040, 0, 0,
	0x04000400, 0x04000400, 0x04000400, 0x04000400, 0x04000400, 0x04000400,
};

/*
 * Is the matrix computed by the tuning (1) or does the API override it (0)?
 * Section byte 0x360 = 1 or (0 with byte 0x361 below 2) always tunes; any
 * other value tunes only while the API switch is off.
 */
static inline int t32_ccm_tuned(const struct t32_ccm *st)
{
	const u8 *sec = st->sec;

	return sec[0x360] == 1 || (sec[0x360] == 0 && sec[0x361] < 2) || !st->api_on;
}

/*
 * tisp_ccm_interp_by_ct, matrix part: the matrix at colour temperature ct from
 * the knots (hysteresis hyst around each knot).  The vendor compares the knot
 * temperatures unsigned and forms the weighted sum in 32 bit (the low word of
 * its 64-bit multiply-accumulate).
 */
static inline void t32_ccm_knot_matrix(const u8 *lut, int knots, int hyst, u32 ct, int *mat)
{
	int i, idx;

	if (knots == 1 || t32_front_le32(lut) >= ct) {
		for (i = 0; i < 9; i++)
			mat[i] = t32_ccm_i32(lut + 4 + 4 * i);
	} else if (t32_front_le32(lut + 40 * (knots - 1)) <= ct) {
		for (i = 0; i < 9; i++)
			mat[i] = t32_ccm_i32(lut + 40 * (knots - 1) + 4 + 4 * i);
	} else {
		/* first knot at or above ct (exists: the last one is above) */
		for (idx = 1; idx < knots && t32_front_le32(lut + 40 * idx) < ct; idx++)
			;
		{
			const u8 *lo = lut + 40 * (idx - 1), *hi = lut + 40 * idx;
			u32 clo = t32_front_le32(lo), chi = t32_front_le32(hi);

			if (clo + (u32)hyst < ct && ct < chi - (u32)hyst) {
				int den = (int)(chi - clo - 2U * (u32)hyst), num = (int)(ct - clo - (u32)hyst);

				for (i = 0; i < 9; i++) {
					u32 a = t32_front_le32(lo + 4 + 4 * i), b = t32_front_le32(hi + 4 + 4 * i);
					int s = (int)((u32)(den - num) * a + (u32)num * b);
					u32 r = (u32)(den >> 1);

					s = (int)(s < 1 ? (u32)s - r : (u32)s + r) / den;
					mat[i] = s < -0x2000 ? -0x2000 : s > 0x1fff ? 0x1fff : s;
				}
			} else {
				/* within the hysteresis of a knot: that knot's matrix */
				const u8 *k = clo + (u32)hyst < ct ? hi : lo;

				for (i = 0; i < 9; i++)
					mat[i] = t32_ccm_i32(k + 4 + 4 * i);
			}
		}
	}
}

/*
 * The knot band of a colour temperature (signed compares, hysteresis hyst):
 * 0 below the first knot, 2k on knot k, 2k + 1 between knots k and k + 1,
 * 2 (knots - 1) at the last knot (the walk never reaches the vendor's final
 * "knots" value for knots >= 2, it is kept for exactness).
 */
static inline u32 t32_ccm_ct_band(const u8 *lut, int knots, int hyst, int ct)
{
	int i;

	if (!((int)((u32)hyst + t32_front_le32(lut)) < ct))
		return 0;
	if (!(ct < (int)(t32_front_le32(lut + 40 * (knots - 1)) - (u32)hyst)))
		return 2U * (u32)(knots - 1);
	for (i = 1; i < knots; i++) {
		u32 c = t32_front_le32(lut + 40 * i);

		if (ct < (int)(c - (u32)hyst)) {
			if ((int)((u32)hyst + t32_front_le32(lut + 40 * (i - 1))) < ct)
				return 2U * (u32)(i - 1) + 1;
		} else if (!((int)((u32)hyst + c) < ct)) {
			return 2U * (u32)i;
		}
	}
	return (u32)knots;
}

/*
 * tisp_ccm_interp_by_ct.  Returns 0 when the matrix was (re)computed, 1 when
 * nothing changed.  force 1 recomputes always; any other nonzero value only
 * lifts the cache (and is refused while the API overrides the matrix).
 * Cache: the same even band is done; the same odd band (between two knots) and
 * every band change need the colour temperature to move by more than the
 * u16 threshold at +0x248.
 */
static inline int t32_ccm_ct_interp(struct t32_ccm *st, u32 ct, u32 force)
{
	u8 *sec = st->sec, *cp = st->cp;
	int knots = sec[0x364], hyst = sec[0x365], i;
	u32 band, m;

	force &= 0xff;
	if (!t32_ccm_tuned(st)) {
		/* the API matrix, taken only on a forced call */
		if (force != 1)
			return 1;
		for (i = 0; i < 9; i++)
			t32_tp_put32(cp + 4 * i, st->api_mat[i]);
		memcpy(st->mat_out, st->api_mat, sizeof(st->mat_out));
		if (sec[0x362] == 1)
			for (i = 0; i < 9; i++)
				t32_tp_put32(sec + 0x3c + 4 * i, st->api_mat[i]);
		return 0;
	}
	band = t32_ccm_ct_band(st->lut, knots, hyst, (int)ct);
	if (!force) {
		if (band == st->ct_code && !(band & 1))
			return 1;
		m = (u32)((int)(ct - st->ct) >> 31);
		if (!((int)T32_CCM_S16(sec, 0x248) < (int)(((ct - st->ct) ^ m) - m)))
			return 1;
	}
	st->ct = ct;
	st->ct_code = band;
	if (t32_front_le32(sec + 0x38)) {
		/* fixed matrix: the section's own nine coefficients at +0x3c */
		memcpy(cp, sec + 0x3c, 36);
	} else {
		int mat[9];

		t32_ccm_knot_matrix(st->lut, knots, hyst, ct, mat);
		for (i = 0; i < 9; i++)
			t32_tp_put32(cp + 4 * i, (u32)mat[i]);
	}
	for (i = 0; i < 9; i++)
		st->mat_out[i] = t32_front_le32(cp + 4 * i);
	if (sec[0x363] == 1)
		memcpy(st->api_mat, st->mat_out, sizeof(st->api_mat));
	return 0;
}

/*
 * tisp_ccm_interp_by_ev, cache and switches around t32_ccm_ev_knots.  The
 * exposure is hi:lo (64 bit; the knot walk uses hi << 22 | lo >> 10).
 * Returns 0 when the parameter block changed, 1 when not (cache hit, or the
 * fixed set of the section, which the vendor reports as 1 as well).
 */
static inline int t32_ccm_ev_interp(struct t32_ccm *st, u32 lo, u32 hi, u32 force)
{
	u8 *sec = st->sec, *cp = st->cp;
	int tuned = t32_ccm_tuned(st), i;
	u32 dv, thr, dhi;
	unsigned long long now = (unsigned long long)hi << 32 | lo, old = (unsigned long long)st->ev_hi << 32 | st->ev_lo, d;

	d = ((int)hi < (int)st->ev_hi || (hi == st->ev_hi && lo < st->ev_lo)) ? old - now : now - old;
	dhi = (u32)(d >> 32);
	dv = (int)dhi < 1 ? (u32)d : 0xffffffffU;
	force &= 0xff;

	if ((tuned && !sec[0x35f]) || (!tuned && !st->api_ev)) {
		/* no exposure dependence: unit gains */
		for (i = 0; i < 12; i++)
			t32_tp_put16(cp + 0x50 + 2 * i, 1024);
		if (!tuned && sec[0x363] == 1)
			sec[0x35f] = st->api_ev;
		if (tuned && sec[0x362] == 1)
			st->api_ev = sec[0x35f];
		return 0;
	}
	thr = t32_front_le32(sec + 0x34);
	if (dv < thr && !force)
		return 1;
	st->ev_lo = lo;
	st->ev_hi = hi;
	if (t32_front_le32(sec + 0x38) == 1) {
		t32_tp_put16(cp + 0x4c, T32_CCM_S16(sec, 0x346));
		t32_tp_put16(cp + 0x4e, T32_CCM_S16(sec, 0x348));
		for (i = 0; i < 12; i++)
			t32_tp_put16(cp + 0x50 + 2 * i, T32_CCM_S16(sec, 0x200 + 2 * i));
		for (i = 0; i < 4; i++)
			t32_tp_put16(cp + 0x3c + 2 * i, T32_CCM_S16(sec, 0x2ae + 2 * i));
		return 1;
	}
	t32_ccm_ev_knots(sec, cp, hi << 22 | lo >> 10);
	return 0;
}

/* tisp_ccm_calc_para_refresh: { hi, lo, scale, shift } of five ranges */
static inline void t32_ccm_calc_para(struct t32_ccm *st)
{
	static const u8 ranges[5][4] = {	/* hi, lo, scale, shift offset in cp */
		{ 0x46, 0x44, 0x26, 0x24 }, { 0, 0, 0x2a, 0x28 }, { 0x4a, 0x48, 0x2e, 0x2c },
		{ 0x4e, 0x4c, 0x32, 0x30 }, { 0x3e, 0x3c, 0x36, 0x34 } };
	u8 *cp = st->cp;
	const u8 *sec = st->sec;
	int i, d;
	u32 v;
	u8 sh;

	for (i = 0; i < 5; i++) {
		if (i == 1)
			d = (int)T32_CCM_S16(sec, 0x27c) - (int)T32_CCM_S16(sec, 0x27a);
		else
			d = (int)t32_tp_le16(cp + ranges[i][0]) - (int)t32_tp_le16(cp + ranges[i][1]);
		v = t32_ccm_bucket(d, &sh);
		t32_tp_put16(cp + ranges[i][2], v);
		cp[ranges[i][3]] = sh;
	}
	d = (int)t32_tp_le16(cp + 0x42) - (int)t32_tp_le16(cp + 0x40);
	v = t32_ccm_bucket(d, &sh);
	t32_tp_put16(cp + 0x3a, v);
	cp[0x38] = sh;
}

/*
 * tisp_ccm_write_reg(mode): 0 all groups, 1 the exposure part (0x9054..0x908c),
 * 2 the matrix part (0x9040..0x9050), 3 the tail (0x9090, 0x9094); every mode
 * ends with the update strobe 0x9010.  Returns the number of writes.
 */
static inline unsigned int t32_ccm_write(const struct t32_ccm *st, u32 mode, struct t32_front_write *w)
{
	const u8 *sec = st->sec, *cp = st->cp;
	unsigned int n = 0;
	int i;

	mode &= 0xff;
#define CP32(i)		t32_front_le32(cp + 4 * (i))
#define CP16(o)		t32_tp_le16(cp + (o))
	if (!(mode & 0xfd)) {
		for (i = 0; i < 4; i++)
			T32_FWR(w, n, 0x9040 + 4 * i, (CP32(2 * i + 1) & 0x3fff) << 16 | (CP32(2 * i) & 0x3fff));
		T32_FWR(w, n, 0x9050, (sec[0x356] & 1U) << 16 | (sec[0x357] & 1U) << 20 | (CP32(8) & 0x3fff) |
			(sec[0x358] & 3U) << 24 | (sec[0x359] & 3U) << 28);
	}
	if (mode < 2) {
		T32_FWR(w, n, 0x9054, (sec[0x35a] & 1U) | (u32)cp[0x24] << 28 | (CP16(0x44) & 0x7ff) << 4 | (CP16(0x46) & 0x7ffU) << 16);
		T32_FWR(w, n, 0x9058, (CP16(0x26) & 0x3ff) | (u32)cp[0x28] << 28 | (sec[0x35b] & 1U) << 12 |
			(T32_CCM_S16(sec, 0x27a) & 0xfffU) << 16);
		T32_FWR(w, n, 0x905c, (CP16(0x2a) & 0x3ffU) << 12 | (T32_CCM_S16(sec, 0x27c) & 0xfff));
		T32_FWR(w, n, 0x9060, (sec[0x35c] & 1U) | (u32)cp[0x2c] << 28 | (CP16(0x48) & 0x7ff) << 4 | (CP16(0x4a) & 0x7ffU) << 16);
		T32_FWR(w, n, 0x9064, (CP16(0x2e) & 0x3ff) | (u32)cp[0x30] << 28 | (sec[0x35d] & 1U) << 12 | (CP16(0x4c) & 0x7ffU) << 16);
		T32_FWR(w, n, 0x9068, (CP16(0x32) & 0x3ffU) << 12 | (CP16(0x4e) & 0x7ff));
		T32_FWR(w, n, 0x906c, (CP16(0x3c) & 0x7ff) << 4 | (CP16(0x3e) & 0x7ffU) << 16 | (sec[0x35e] & 1U));
		T32_FWR(w, n, 0x9070, (CP16(0x42) & 0x7ffU) << 12 | (CP16(0x40) & 0x7ff));
		T32_FWR(w, n, 0x9074, (CP16(0x36) & 0x3ff) | (u32)cp[0x38] << 28 | (CP16(0x3a) & 0x3ffU) << 12 | (cp[0x34] & 0xfU) << 24);
		for (i = 0; i < 6; i++)
			T32_FWR(w, n, 0x9078 + 4 * i, (CP16(0x52 + 4 * i) & 0x3fffU) << 16 | (CP16(0x50 + 4 * i) & 0x3fff));
	}
	if (mode == 0 || mode == 3) {
		T32_FWR(w, n, 0x9090, (u32)sec[0x34a] | (u32)T32_CCM_S16(sec, 0x350) << 24 | (T32_CCM_S16(sec, 0x34c) & 0xffU) << 8 |
			(u32)sec[0x34e] << 16);
		T32_FWR(w, n, 0x9094, (T32_CCM_S16(sec, 0x354) & 0xffU) << 8 | sec[0x352]);
	}
	T32_FWR(w, n, 0x9010, 1);
#undef CP32
#undef CP16
	return n;
}

/*
 * tisp_ccm_ct_update: the colour temperature changed.  Recomputes the matrix
 * (not forced) and writes the matrix part.  Returns the number of writes
 * (0 when the cache says nothing changed).
 */
static inline int t32_ccm_ct_update(struct t32_ccm *st, u32 ct, struct t32_front_write *w)
{
	if (t32_ccm_ct_interp(st, ct, 0))
		return 0;
	return (int)t32_ccm_write(st, 2, w);
}

/*
 * tisp_ccm_ev_update: the exposure (hi:lo) changed.  The exposure is limited
 * to 256 << 32, the interpolation is not forced; on a change the derived
 * parameters are refreshed and the exposure part written.
 */
static inline int t32_ccm_ev_update(struct t32_ccm *st, u32 lo, u32 hi, struct t32_front_write *w)
{
	if (hi > 256 || (hi == 256 && lo != 0)) {
		lo = 0;
		hi = 256;
	}
	if (t32_ccm_ev_interp(st, lo, hi, 0))
		return 0;
	t32_ccm_calc_para(st);
	return (int)t32_ccm_write(st, 1, w);
}

/*
 * tisp_ccm_init with the state kept: sec: writable copy of section 12, lut:
 * the payload behind it (lutbytes long, knots * 40).  Returns the number of
 * writes (24), -1 on a refused section.
 */
static inline int t32_ccm_start(struct t32_ccm *st, u8 *sec, const u8 *lut, u32 lutbytes, struct t32_front_write *w)
{
	unsigned int n;
	int i;

	if (t32_front_le32(sec) != T32_CCM_ID || !sec[0x35f])
		return -1;
	st->sec = sec;
	st->lut = lut;
	st->lutbytes = lutbytes;
	sec[0x360] = 0;
	sec[0x361] = 0;
	if (sec[0x364] < 1 || lutbytes < (u32)sec[0x364] * 40U)
		return -1;
	for (i = 0; i < 26; i++)
		t32_tp_put32(st->cp + 4 * i, t32_ccm_cp_default[i]);
	for (i = 0; i < 9; i++) {
		st->mat_out[i] = t32_ccm_cp_default[i];
		st->api_mat[i] = t32_ccm_cp_default[i];
	}
	st->ev_lo = 0;
	st->ev_hi = 0;
	st->ct = T32_CCM_CT;
	st->ct_code = 255;
	st->api_on = 0;
	st->api_ev = 0;

	/* first calls forced: colour temperature 5000, exposure 0 */
	t32_ccm_ct_interp(st, T32_CCM_CT, 1);
	t32_ccm_ev_interp(st, 0, 0, 1);
	t32_ccm_calc_para(st);
	n = t32_ccm_write(st, 0, w);
	T32_FWR(w, n, 0x9000, T32_CCM_ID);
	return (int)n;
}

/*
 * tisp_ccm_dn_params_refresh: the tuning section moved (day/night switch).
 * The vendor points the state at the new section (+0x7e40 of the tuning
 * block), clears the day/night byte +0x361, then forces both interpolations
 * (colour temperature and exposure from the state), refreshes the derived
 * parameters and writes all groups.
 */
static inline int t32_ccm_dn_params_refresh(struct t32_ccm *st, u8 *tuning_base,
					    struct t32_front_write *w)
{
	st->sec = tuning_base + 0x7e40;
	st->sec[0x361] = 0;
	t32_ccm_ct_interp(st, st->ct, 1);
	t32_ccm_ev_interp(st, st->ev_lo, st->ev_hi, 1);
	t32_ccm_calc_para(st);
	return (int)t32_ccm_write(st, 0, w);
}

/*
 * tisp_ccm_wdr_en: the WDR mode changed.  Forces both interpolations from
 * the state and writes all groups.
 */
static inline int t32_ccm_wdr_en(struct t32_ccm *st, struct t32_front_write *w)
{
	t32_ccm_ct_interp(st, st->ct, 1);
	t32_ccm_ev_interp(st, st->ev_lo, st->ev_hi, 1);
	t32_ccm_calc_para(st);
	return (int)t32_ccm_write(st, 0, w);
}

/* tisp_ccm_wake_up: rewrite all groups from the current parameter block */
static inline int t32_ccm_wake_up(struct t32_ccm *st, struct t32_front_write *w)
{
	return (int)t32_ccm_write(st, 0, w);
}

static inline int t32_ccm_init(u8 *sec, const u8 *lut, u32 lutbytes, struct t32_front_write *w)
{
	struct t32_ccm st;

	return t32_ccm_start(&st, sec, lut, lutbytes, w);
}

#endif /* TX_ISP_T32_CCM_H */
