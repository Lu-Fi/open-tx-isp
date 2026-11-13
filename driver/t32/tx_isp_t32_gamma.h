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
 * by tests/t32_oracle (t32_gamma_oracle, t32_gammax_oracle).
 */
#ifndef TX_ISP_T32_GAMMA_H
#define TX_ISP_T32_GAMMA_H

#include "tx_isp_t32_tp.h"
#ifdef __KERNEL__
#include <linux/slab.h>	/* kmalloc/kfree in t32_gamma_init */
#endif

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
	u32 plbytes;		/* t32_data_e1820: bytes of one payload record */
	u8 gs[260];		/* [0] strength, +2 the transformed curve (129 u16) */
	u32 ev_lo, ev_hi;	/* exposure of the last strength update */
	u8 b12[260];		/* [0] strength or 0, [1] strength or 0xff, +2 curve copy */
	u8 cin[258];		/* curve before the strength transform */
	u8 b20[264];		/* [0] u32, +4 curve kept for the API */
	u8 pending;		/* write request counter (the byte at state + 24) */
	u32 pl_cap;		/* capacity of the payload block (vendor dc1a4 + ch * 28 + 12) */
	u16 info_sz;		/* payload size word of the look-up table info (dc274[ch] + 4) */
	u16 last_sz;		/* t32_data_da93c: size of the last payload set */
	void *own[5];		/* vendor allocations at state +4, +8, +12, +16, +20 */
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
 * tisp_gamma_addr_write_lut_rgb (t32_data_d03a0 == 1): the same LUT through
 * tisp_reg_write / tisp_reg_lut_write into the register shadow image (index ->
 * value): per port the control word at T32_GAMMA_IMG_CTL[p], the 128 pairs at
 * +2 + i and the end word at +131.  Ports in write_lut order (0x32040, 0x32080,
 * 0x32060).
 */
#define T32_GAMMA_IMG_WORDS	2436U
static inline void t32_gamma_addr_write_lut(const struct t32_gamma *st, u32 *img)
{
	static const u32 ctl[3] = { 2038, 2171, 2304 };
	unsigned int i, p;

	for (p = 0; p < 3; p++)
		img[ctl[p]] = 0x101;
	for (i = 0; i < 128; i++)
		for (p = 0; p < 3; p++)
			img[ctl[p] + 2 + i] = (t32_tp_le16(st->gs + 4 + 2 * i) & 0xfffU) << 12 |
				(t32_tp_le16(st->gs + 2 + 2 * i) & 0xfff);
	for (p = 0; p < 3; p++)
		img[ctl[p] + 131] = 0x7f0102;
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
 * Per-frame paths: tisp_gamma_dn_params_refresh / tisp_gamma_wdr_en /
 * tisp_gamma_wake_up.  The vendor state is reached through t32_data_e1834[ch]
 * (the struct t32_gamma pointer); the section is at tuning base + 0x81a8.
 * With t32_data_d03a0 == 1 the LUT goes into the shadow image img
 * (T32_GAMMA_IMG_WORDS); otherwise the new curve is left pending for the
 * frame done.  Neither path writes a register directly (they return 0).
 */
/* vendor transport flag (1 = shadow image), defined by the driver / host env */
extern u8 t32_data_d03a0[4];

static inline int t32_gamma_dn_params_refresh(struct t32_gamma *st, u8 ch,
					      u8 *tuning_base, u32 *img)
{
	(void)ch;
	st->sec = tuning_base + 0x81a8;
	t32_gamma_interp_ev(st, st->ev_lo, st->ev_hi, 1);
	t32_gamma_update_curve(st, st->ev_lo, st->ev_hi);
	t32_gamma_transform(st);
	if (t32_data_d03a0[0] == 1)
		t32_gamma_addr_write_lut(st, img);
	else
		st->pending++;
	return 0;
}

static inline int t32_gamma_wdr_en(struct t32_gamma *st, u8 ch, u32 *img)
{
	(void)ch;
	t32_gamma_interp_ev(st, st->ev_lo, st->ev_hi, 1);
	t32_gamma_update_curve(st, st->ev_lo, st->ev_hi);
	t32_gamma_transform(st);
	if (t32_data_d03a0[0] == 1)
		t32_gamma_addr_write_lut(st, img);
	else
		st->pending++;
	return 0;
}

/* tisp_gamma_wake_up: tail call of addr_write_lut_rgb (d03a0 == 1) or write_lut_rgb; no pending check */
static inline int t32_gamma_wake_up(struct t32_gamma *st, u32 *img, struct t32_front_write *w)
{
	if (t32_data_d03a0[0] == 1) {
		t32_gamma_addr_write_lut(st, img);
		return 0;
	}
	return t32_gamma_write_lut(st, w);
}

/*
 * Parameter accessors and the API entry points (tisp_gamma_param_array_get /
 * _set, tisp_gamma_api_set / _get, tisp_gamma_deinit).  The vendor reaches the
 * state through t32_data_e1834[ch] and the payload through the look-up table
 * info t32_data_dc274[ch] (+32 payload, +4 size word) and t32_data_dc1a4 (+12
 * capacity); the port takes the state with those fields directly.
 */

/*
 * tisp_gamma_param_array_get: section 13, then sec[0x5a] payload records of
 * plbytes bytes each; the source records are plbytes rounded down to even apart.
 */
static inline int t32_gamma_param_array_get(const struct t32_gamma *st, u8 *buf, u32 *len)
{
	u32 n = st->sec[0x5a], stride = st->plbytes & ~1U, i;

	memcpy(buf, st->sec, T32_GAMMA_SEC_BYTES);
	for (i = 0; i < n; i++)
		memcpy(buf + T32_GAMMA_SEC_BYTES + i * st->plbytes, st->pl + i * stride, st->plbytes);
	*len = T32_GAMMA_SEC_BYTES + n * st->plbytes;
	return 0;
}

/*
 * tisp_gamma_param_array_set: replace the section and the payload (records of
 * 266 bytes, the payload block grows when too small, the copy size is a u16),
 * mark the API switch +0x57 = 1, then refresh the curve.
 */
static inline int t32_gamma_param_array_set(struct t32_gamma *st, u8 ch, const u8 *buf,
					    void *(*valloc)(u32 size), void (*vfree)(void *p),
					    u32 *img)
{
	u32 n, sz;

	(void)ch;
	memcpy(st->sec, buf, T32_GAMMA_SEC_BYTES);
	st->plbytes = T32_GAMMA_REC_BYTES;
	n = st->sec[0x5a];
	sz = n * T32_GAMMA_REC_BYTES;
	if (st->pl_cap < sz) {
		if (st->pl) {
			vfree(st->pl);
			st->pl = NULL;
		}
		st->pl_cap = sz;
		st->pl = valloc(sz);
	}
	st->info_sz = (u16)sz;
	memcpy(st->pl, buf + T32_GAMMA_SEC_BYTES, (u16)sz);
	st->last_sz = (u16)sz;
	st->sec[T32_GAMMA_API_B] = 1;
	t32_gamma_interp_ev(st, st->ev_lo, st->ev_hi, 1);
	t32_gamma_update_curve(st, st->ev_lo, st->ev_hi);
	t32_gamma_transform(st);
	if (t32_data_d03a0[0] == 1)
		t32_gamma_addr_write_lut(st, img);
	else
		st->pending++;
	return 0;
}

/*
 * tisp_gamma_api_set: api = { u32 mode, u8 curve[258] }.  Mode 0 returns to the
 * tuned curve (clears +0x56/+0x57 when 2) and refreshes it; modes 1..3 take the
 * compiled-in preset curves t32_data_bf754 / bf650 / bf54c (preset[0..2], 258
 * bytes each), mode 4 the curve of the call; all set +0x57 = 2.  Other modes
 * return -1 after storing the mode.
 */
static inline int t32_gamma_api_set(struct t32_gamma *st, u8 ch, const u8 *api,
				    const u8 *const preset[3], u32 *img)
{
	u32 v = t32_front_le32(api);
	const u8 *src;

	(void)ch;
	t32_tp_put32(st->b20, v);
	if (v == 0) {
		if (st->sec[T32_GAMMA_API_A] == 2)
			st->sec[T32_GAMMA_API_A] = 0;
		if (st->sec[T32_GAMMA_API_B] == 2)
			st->sec[T32_GAMMA_API_B] = 0;
		t32_gamma_interp_ev(st, st->ev_lo, st->ev_hi, 1);
		t32_gamma_update_curve(st, st->ev_lo, st->ev_hi);
	} else if (v <= 4) {
		st->sec[T32_GAMMA_API_B] = 2;
		if (v == 1)
			src = preset[0];
		else if (v == 2)
			src = preset[1];
		else if (v == 3)
			src = preset[2];
		else
			src = api + 4;
		memcpy(st->b20 + 4, src, 258);
	} else {
		return -1;
	}
	t32_gamma_transform(st);
	if (t32_data_d03a0[0] == 1)
		t32_gamma_addr_write_lut(st, img);
	else
		st->pending++;
	return 0;
}

/* tisp_gamma_api_get: the API curve block (blk20) */
static inline int t32_gamma_api_get(const struct t32_gamma *st, u8 *buf)
{
	memcpy(buf, st->b20, 264);
	return 0;
}

/*
 * tisp_gamma_deinit: free the state's blocks (state +4, +12, +8, +16, +20, each
 * when set), then the state itself, and clear the slot.
 */
static inline int t32_gamma_deinit(void (*vfree)(void *p), struct t32_gamma **slot)
{
	static const u8 order[5] = { 0, 2, 1, 3, 4 };
	struct t32_gamma *st = *slot;
	unsigned int i;

	if (!st)
		return 0;
	for (i = 0; i < 5; i++)
		if (st->own[order[i]])
			vfree(st->own[order[i]]);
	vfree(*slot);
	*slot = NULL;
	return 0;
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
