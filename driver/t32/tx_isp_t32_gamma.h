/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 gamma: the 129 point curve from the IQ bin, as the vendor tisp does at
 * AddSensor (S4 step 5).
 *
 * Reverse engineered flow (PRJ007 driver): tisp_gamma_init ->
 * tisp_gamma_interp_by_ev (strength byte at EV 0), tisp_gamma_update_curve
 * (curve from the look-up table payload behind IQ bin section 13),
 * tisp_gamma_strength_transform (curve blended with the linear ramp by the
 * strength), tisp_gamma_write_lut_rgb (the same 128 packed pairs through the
 * three LUT ports 0x32040/0x32044, 0x32080/0x32084, 0x32060/0x32064).
 *
 * Section 13 (92 bytes): +0 block id, +0x18 twelve u32 EV knots, +0x48 strength
 * mode (0: 255, 1: interpolated over the knots, other: byte +0x49), +0x4a
 * twelve strength bytes, +0x5a number of curves in the payload.
 * Block id below 0x134ff91: one curve of 0x102 bytes in the payload; otherwise the
 * payload holds sec[0x5a] records of 266 bytes (curve, then the exposure range
 * lo/hi as 2 x 2 u16) and the curve for an exposure is chosen or blended from them.
 *
 * Per frame: struct t32_gamma keeps the vendor's state; t32_gamma_ev_update is
 * tisp_gamma_ev_update (strength, curve, strength transform, write request) and
 * t32_gamma_frame_done is tisp_gamma_irq_frame_done, which writes the LUT when a
 * request is pending (the vendor's d03a0 == 0 mode).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_gamma_oracle).
 */
#ifndef TX_ISP_T32_GAMMA_H
#define TX_ISP_T32_GAMMA_H

#include "tx_isp_t32_tp.h"

#define T32_SEC_GAMMA		13
#define T32_GAMMA_SEC_BYTES	92U
#define T32_GAMMA_CURVE_BYTES	0x102U
#define T32_GAMMA_POINTS	129U
#define T32_GAMMA_MAX_WRITES	(3 + 3 * 128 + 3)

/* section bytes beyond the documented ones: the API switches of the strength path */
#define T32_GAMMA_API_A		0x56	/* 1: tuned, other nonzero: API curve; 0 with +0x57 = 2: API curve */
#define T32_GAMMA_API_B		0x57
#define T32_GAMMA_COPY_OUT	0x58	/* 1: keep the transformed curve for the API (blk20) */
#define T32_GAMMA_SAVE_PAYLOAD	0x59	/* 1: write the API curve back into the payload */
#define T32_GAMMA_MULTI_ID	0x134ff91U
#define T32_GAMMA_REC_BYTES	266U	/* one curve + range of the multi-curve payload */

/*
 * The vendor's per-channel state (tisp_gamma_init allocates it; the blocks start
 * as the compiled-in defaults d6a4c / e1848 / d6948 / d6840).
 */
struct t32_gamma {
	u8 *sec;		/* section 13 (92 bytes), live: init clears +0x56/+0x57 */
	u8 *pl;			/* payload behind the section (written back when +0x59 = 1) */
	u32 plbytes;
	u8 gs[260];		/* [0] strength, +2 the transformed curve (129 u16) */
	u32 ev_lo, ev_hi;	/* exposure of the last strength update */
	u8 b12[260];		/* [0] strength or 0, [1] strength or 0xff, +2 curve copy */
	u8 cin[258];		/* curve before the strength transform */
	u8 b20[264];		/* [0] u32, +4 curve kept for the API */
	u8 pending;		/* write request counter (the byte at state + 24) */
};

/* d6840 (data.c:25533): the API curve block before the first transform */
static const u32 t32_gamma_b20_default[66] = {
	0x00000000, 0x00900000, 0x01aa0120, 0x0281021d, 0x032c02da, 0x03bd0377,
	0x043e0400, 0x04b3047a, 0x051e04e9, 0x05810550, 0x05de05b0, 0x0636060b,
	0x06890660, 0x06d906b1, 0x072506ff, 0x076e074a, 0x07b40791, 0x07f807d6,
	0x083a0819, 0x0879085a, 0x08b70898, 0x08f308d5, 0x092d0910, 0x0966094a,
	0x099e0982, 0x09d409b9, 0x0a0909ef, 0x0a3d0a23, 0x0a700a57, 0x0aa20a89,
	0x0ad30aba, 0x0b030aeb, 0x0b320b1a, 0x0b600b49, 0x0b8e0b77, 0x0bbb0ba4,
	0x0be70bd1, 0x0c120bfc, 0x0c3d0c28, 0x0c670c52, 0x0c910c7c, 0x0cb90ca5,
	0x0ce20cce, 0x0d0a0cf6, 0x0d310d1e, 0x0d580d45, 0x0d7e0d6b, 0x0da40d92,
	0x0dca0db7, 0x0def0ddd, 0x0e140e01, 0x0e380e26, 0x0e5c0e4a, 0x0e7f0e6e,
	0x0ea20e91, 0x0ec50eb4, 0x0ee70ed6, 0x0f0a0ef9, 0x0f2b0f1a, 0x0f4d0f3c,
	0x0f6e0f5d, 0x0f8f0f7e, 0x0faf0f9f, 0x0fcf0fbf, 0x0fef0fdf, 0x00000fff,
};

/*
 * Is the strength tuned (1) or does the API provide the curve (0)?  Byte
 * 0x56 = 1, or 0x56 = 0 with 0x57 != 2.
 */
static inline int t32_gamma_tuned(const u8 *sec)
{
	return sec[T32_GAMMA_API_A] == 1 || (sec[T32_GAMMA_API_A] == 0 && sec[T32_GAMMA_API_B] != 2);
}

/*
 * tisp_gamma_interp_by_ev: the strength byte for the exposure hi:lo (the knot
 * walk uses hi << 22 | lo >> 10).  Returns 0 when the byte was stored, 1 when it
 * is unchanged and not forced, 0xffffffff for knots that are not increasing
 * (nothing stored); with the API curve active it stores 0xff and returns
 * (force != 1).
 */
static inline u32 t32_gamma_interp_ev(struct t32_gamma *st, u32 lo, u32 hi, u32 force)
{
	const u8 *sec = st->sec;
	u32 ev = hi << 22 | lo >> 10, knot, prev, d, iv, v = 0;
	unsigned int i;

	force &= 0xff;
	st->ev_lo = lo;
	st->ev_hi = hi;
	if (!t32_gamma_tuned(sec)) {
		st->gs[0] = 0xff;
		return force != 1;
	}
	if (sec[0x48] == 0) {
		if (force) {
			st->gs[0] = 0xff;
			return 0;
		}
		if (st->gs[0] == 0xff)
			return 1;
		st->gs[0] = 0xff;
		return 0;
	}
	if (sec[0x48] != 1) {
		v = sec[0x49];
		if (!force && st->gs[0] == v)
			return 1;
		st->gs[0] = (u8)v;
		return 0;
	}
	v = sec[0x55];
	for (i = 0; i < 12; i++) {
		knot = t32_front_le32(sec + 0x18 + 4 * i);
		if (ev < knot) {
			if (i == 0) {
				v = sec[0x4a];
				break;
			}
			prev = t32_front_le32(sec + 0x18 + 4 * (i - 1));
			d = knot - prev;
			iv = ev - prev;
			if (d == 0)
				return 0xffffffffU;
			v = (sec[0x49 + i] * (d - iv) + sec[0x4a + i] * iv + (d >> 1)) / d;
			break;
		}
	}
	if (v != st->gs[0] || force) {
		st->gs[0] = (u8)v;
		return 0;
	}
	return 1;
}

/*
 * tisp_gamma_update_curve: the curve before the strength transform for the
 * exposure.  Single curve form: the payload as it is (returns 1).  Multi-curve
 * form: the record whose range holds the exposure, the first/last record outside
 * all ranges, or a blend of two records for an exposure below a record's range
 * (the record before it and that one; weights from the record's own range width,
 * 32 bit).  Returns 0, or 255 for a zero-width range.
 */
static inline u32 t32_gamma_update_curve(struct t32_gamma *st, u32 lo, u32 hi)
{
	const u8 *sec = st->sec, *pl = st->pl, *rec;
	int ev = (int)(hi << 22 | lo >> 10);
	unsigned int c, n = sec[0x5a], j;

	if (t32_front_le32(sec) < T32_GAMMA_MULTI_ID) {
		memcpy(st->cin, pl, 258);
		return 1;
	}
	for (c = 0; c < n; c = (c + 1) & 0xff) {
		int rlo, rhi;

		rec = pl + T32_GAMMA_REC_BYTES * c;
		rlo = (int)(t32_tp_le16(rec + 260) << 16) + (int)t32_tp_le16(rec + 258);
		rhi = (int)(t32_tp_le16(rec + 264) << 16) + (int)t32_tp_le16(rec + 262);
		if (ev < rlo) {
			if (c == 0) {
				memcpy(st->cin, pl, 258);
				return 0;
			}
			if (rhi - rlo == 0)
				return 255;
			{
				u32 w = (u32)(rhi - rlo);
				int a = ev - rlo, b = (int)w - a;

				for (j = 0; j < 129; j++) {
					int cur = (int)t32_tp_le16(rec + 2 * j);
					int prv = (int)t32_tp_le16(rec - T32_GAMMA_REC_BYTES + 2 * j);

					t32_tp_put16(st->cin + 2 * j, ((u32)(cur * a + prv * b) + (w >> 1)) / w);
				}
			}
			return 0;
		}
		if (rhi < ev) {
			if (c == n - 1U)
				memcpy(st->cin, rec, 258);
			continue;
		}
		memcpy(st->cin, rec, 258);
		return 0;
	}
	return 0;
}

/*
 * tisp_gamma_strength_transform: the curve blended with the linear ramp by the
 * strength gs[0] (k = s + s / 128), into gs+2; the API copies follow.  With the
 * API curve active the stored API curve (blk20) is taken instead, and written
 * back into the payload when +0x59 is 1.
 */
static inline void t32_gamma_transform(struct t32_gamma *st)
{
	const u8 *sec = st->sec;
	u32 k, acc = 0, v;
	unsigned int i;

	if (t32_gamma_tuned(sec)) {
		k = st->gs[0];
		k += k >> 7;
		for (i = 0; i < 128; i++) {
			v = t32_tp_le16(st->cin + 2 * i) * k + (acc << 5) + 128;	/* tisp_round_int64(.., 8) */
			t32_tp_put16(st->gs + 2 + 2 * i, v >> 8);
			acc += 0x100 - k;
		}
		v = t32_tp_le16(st->cin + 256) * k + (0x100 - k) * 0xfff + 128;
		t32_tp_put16(st->gs + 258, v >> 8);
		if (sec[T32_GAMMA_COPY_OUT] == 1) {
			t32_tp_put32(st->b20, 0);
			memcpy(st->b20 + 4, st->gs + 2, 258);
		}
		memcpy(st->b12 + 2, st->gs + 2, 258);
		st->b12[0] = 0;
		st->b12[1] = st->gs[0];
	} else {
		memcpy(st->gs + 2, st->b20 + 4, 258);
		memcpy(st->b12 + 2, st->b20 + 4, 258);
		st->b12[0] = st->b20[0];
		st->b12[1] = 0xff;
		if (sec[T32_GAMMA_SAVE_PAYLOAD] == 1)
			memcpy(st->pl, st->gs + 2, 258);
	}
}

/* tisp_gamma_write_lut_rgb: the 128 packed pairs through the three LUT ports */
static inline int t32_gamma_write_lut(const struct t32_gamma *st, struct t32_front_write *w)
{
	static const u32 ports[3][2] = { { 0x32040, 0x32044 }, { 0x32080, 0x32084 }, { 0x32060, 0x32064 } };
	unsigned int n = 0, i, p;

	for (p = 0; p < 3; p++)
		T32_FWR(w, n, ports[p][0], 0x101);
	for (i = 0; i < 128; i++)
		for (p = 0; p < 3; p++)
			T32_FWR(w, n, ports[p][1], (t32_tp_le16(st->gs + 4 + 2 * i) & 0xfffU) << 12 |
				(t32_tp_le16(st->gs + 2 + 2 * i) & 0xfff));
	for (p = 0; p < 3; p++)
		T32_FWR(w, n, ports[p][0], 0x7f0102);
	return (int)n;
}

/*
 * tisp_gamma_ev_update: the exposure (hi:lo, limited to 256 << 32) changed.  Returns
 * 0 when a new curve is pending (written at the next frame done), 1 when the strength
 * did not change, -1 for knots that are not increasing.
 */
static inline int t32_gamma_ev_update(struct t32_gamma *st, u32 lo, u32 hi)
{
	int rc;

	if (hi > 256 || (hi == 256 && lo != 0)) {
		lo = 0;
		hi = 256;
	}
	rc = (signed char)t32_gamma_interp_ev(st, lo, hi, 0);
	t32_gamma_update_curve(st, lo, hi);
	if (rc)
		return rc == -1 ? -1 : 1;
	t32_gamma_transform(st);
	st->pending++;
	return 0;
}

/* tisp_gamma_irq_frame_done: writes the LUT once per request; returns the number of writes */
static inline int t32_gamma_frame_done(struct t32_gamma *st, struct t32_front_write *w)
{
	if (!st->pending)
		return 0;
	st->pending = 0;
	return t32_gamma_write_lut(st, w);
}

/*
 * tisp_gamma_init with the state kept.  sec: writable copy of section 13 (the
 * block clears +0x56/+0x57), lut: the payload behind it (writable: +0x59).
 * Returns the number of writes (3 + 3 * 128 + 3), -1 on a refused section.
 */
static inline int t32_gamma_start(struct t32_gamma *st, u8 *sec, u8 *lut, u32 lutbytes,
				  struct t32_front_write *w)
{
	unsigned int i;

	if (t32_front_le32(sec) < T32_GAMMA_MULTI_ID ? lutbytes < T32_GAMMA_CURVE_BYTES || sec[0x5a] < 1 :
	    sec[0x5a] < 1 || lutbytes < T32_GAMMA_REC_BYTES * sec[0x5a])
		return -1;
	st->sec = sec;
	st->pl = lut;
	st->plbytes = lutbytes;
	memset(st->gs, 0, sizeof(st->gs));
	st->gs[0] = 0xff;
	memset(st->b12, 0, sizeof(st->b12));
	memset(st->cin, 0, sizeof(st->cin));
	for (i = 0; i < 66; i++)
		t32_tp_put32(st->b20 + 4 * i, t32_gamma_b20_default[i]);
	st->pending = 0;
	st->ev_lo = 0;
	st->ev_hi = 0;
	sec[T32_GAMMA_API_A] = 0;
	sec[T32_GAMMA_API_B] = 0;
	memcpy(st->cin, lut, 258);
	t32_gamma_interp_ev(st, 0, 0, 1);
	t32_gamma_update_curve(st, 0, 0);
	t32_gamma_transform(st);
	return t32_gamma_write_lut(st, w);
}

/*
 * sec: section 13, lut: payload behind it.  The payload is only written by the API path
 * (+0x59), which the init never takes (it clears +0x56/+0x57), so the const is kept.
 */
static inline int t32_gamma_init(const u8 *sec, const u8 *lut, u32 lutbytes, struct t32_front_write *w)
{
	u8 s[T32_GAMMA_SEC_BYTES];
	int r;
#ifdef __KERNEL__
	struct t32_gamma *st = kmalloc(sizeof(*st), GFP_KERNEL);	/* about 1.1 KB: not on the stack */

	if (!st)
		return -1;
#else
	struct t32_gamma stv, *st = &stv;
#endif

	memcpy(s, sec, T32_GAMMA_SEC_BYTES);
	r = t32_gamma_start(st, s, (u8 *)lut, lutbytes, w);
#ifdef __KERNEL__
	kfree(st);
#endif
	return r;
}

#endif /* TX_ISP_T32_GAMMA_H */
