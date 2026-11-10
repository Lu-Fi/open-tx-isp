/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE flicker detection: readable C of the vendor function
 * tisp_ae_fliker_detect (Matt Davis' recovered C, PR #15 @ 2e2e06bc), checked
 * against the vendor C by tests/t32_oracle (t32_aef_oracle).  The vendor sort
 * (tisp_ae_sort_H2L) is a hook.
 *
 * State block P (vendor e15d8[ch]):
 *    0: call counter            4: previous sample set (15 x 15 words)
 *  904: current sample set (15 x 15 words; only word 0 gets the new sample)
 * 2704: difference sets, 9 x (15 x 15) words, one per counter phase 1..9
 *10804: stored value  10808: flicker decision  10812: history (10 words)
 *11756: decision history (3 words: current, previous, stable)   size 11768
 * Q (vendor e15c8[ch]): 0 threshold, 4 history tolerance, 8 values summed, 12 history length
 * R (vendor e1588[ch]): byte 1694 receives the flicker decision.
 */
#ifndef TX_ISP_T32_AEFU_H
#define TX_ISP_T32_AEFU_H

#include "tx_isp_t32_stats.h"
#include "tx_isp_t32_aev.h"

struct t32_aef_ops {
	void (*sort)(u32 *val, u32 *idx);	/* tisp_ae_sort_H2L: |val| descending, idx follows */
};

typedef int t32_aef_s32;

static inline void t32_aef_cpy(u8 *d, const u8 *s, u32 n)
{
	while (n--)
		*d++ = *s++;
}

/* the vendor's MIPS div: a zero divisor gives 1 / -1 (no trap) */
static inline t32_aef_s32 t32_aef_sdiv(t32_aef_s32 a, t32_aef_s32 b)
{
	if (b == 0)
		return a < 0 ? 1 : -1;
	if (b == -1)
		return (t32_aef_s32)(0U - (u32)a);
	return a / b;
}

/* diff set of phase ph (0..8): clamp(cur - prev) to +-2000 */
static inline void t32_aef_diff(u8 *p, u32 ph)
{
	u32 i;

	for (i = 0; i < 225; i++) {
		t32_aef_s32 d = (t32_aef_s32)(t32_ld32(p, 904 + 4 * i) - t32_ld32(p, 4 + 4 * i));

		if (!(d < 2000))
			d = 2000;
		else if (d < -1999)
			d = -2000;
		t32_wr32(p, 2704 + 900 * ph + 4 * i, (u32)d);
	}
}

/* phase 9: the 9 diff sets give the new value, the history ev[] and the decision */
static inline void t32_aef_decide(u8 *p, const u8 *q, u8 *r, u32 *ev, const struct t32_aef_ops *ops)
{
	t32_aef_s32 m[15][9], v[15], nlim = (t32_aef_s32)t32_ld32(q, 8), tq = (t32_aef_s32)t32_ld32(q, 4);
	t32_aef_s32 n2 = (t32_aef_s32)t32_ld32(q, 12), mx, mn, thr, d, ntq = (t32_aef_s32)(0U - (u32)tq);
	u32 val[15], idx[15], k, j, i, flag, w0, t1, t2, c, sum;

	for (k = 0; k < 9; k++) {
		for (j = 0; j < 15; j++) {
			u32 sm = 0, base = 2704 + 4 * (k * 225 + 15 * j);
			t32_aef_s32 half = nlim / 2, x;

			for (i = 0; i < 15; i++) {
				idx[i] = i;
				val[i] = t32_ld32(p, base + 4 * i);
			}
			ops->sort(val, idx);
			for (i = 0; (t32_aef_s32)i < nlim; i = (i + 1) & 0xff)
				sm += t32_ld32(p, base + 4 * idx[i]);
			x = (t32_aef_s32)((t32_aef_s32)sm >= 0 ? (sm << 4) + (u32)half : (sm << 4) - (u32)half);
			m[j][k] = t32_aef_sdiv(x, nlim);
		}
	}
	for (j = 0; j < 15; j++) {
		u32 sq = 0;

		for (k = 0; k < 9; k++)
			sq += (u32)m[j][k] * (u32)m[j][k];
		v[j] = (t32_aef_s32)(sq + 4) / 9;
	}
	mx = mn = v[0];
	sum = 0;
	for (j = 0; j < 15; j++) {
		if (mx < v[j])
			mx = v[j];
		if (v[j] < mn)
			mn = v[j];
		sum += (u32)v[j];
	}
	thr = (t32_aef_s32)((u32)((t32_aef_s32)(sum - (u32)mn - (u32)mx) / 256) + 6) / 13;
	/* history entries within the tolerance of their neighbour (and ev[0] of the new value) */
	t1 = 0;
	for (c = 0; (t32_aef_s32)c < n2 - 2; c = (c + 1) & 0xff) {
		d = (t32_aef_s32)(ev[c + 1] - ev[c]);
		if (!(tq < d))
			t1 += !(d < ntq);
	}
	d = (t32_aef_s32)(ev[0] - (u32)thr);
	if (!(tq < d))
		t1 += !(d < ntq);
	t2 = (u32)n2 - 1;
	if (t1 != t2) {
		/* the history shifts up, the new value enters at 0 */
		for (c = ((u32)n2 - 2) & 0xff; c; c = (c - 1) & 0xff)
			ev[c] = ev[c - 1];
		ev[0] = (u32)thr;
	} else {
		/* all within tolerance: the whole history takes the new value */
		for (c = 0; (t32_aef_s32)c < n2; c = (c + 1) & 0xff)
			ev[c] = (u32)thr;
		t32_wr32(p, 10804, (u32)thr);
	}
	for (c = 0; c < 40; c++)
		p[10812 + c] = (u8)(ev[c >> 2] >> (8 * (c & 3)));
	flag = !((t32_aef_s32)t32_ld32(p, 10804) < (t32_aef_s32)t32_ld32(q, 0));
	w0 = t32_ld32(p, 11756);
	if (flag != w0 || flag != t32_ld32(p, 11760)) {
		t32_wr32(p, 10808, t32_ld32(p, 11764));
		t32_wr32(p, 11760, w0);
		t32_wr32(p, 11756, flag);
	} else {
		t32_wr32(p, 11764, flag);
		t32_wr32(p, 10808, flag);
	}
	r[1694] = (u8)t32_ld32(p, 10808);
}

/*
 * tisp_ae_fliker_detect: one call per frame with the new sample (4 bytes at
 * smp).  Every 10th call (counter % 10 == 0) the state is cleared, the
 * other phases collect the diff set and the 9th phase decides.  Returns the
 * new counter (the vendor returns P << 32 | counter).  The history length
 * q[12] must be 1..11 (and 2..11 for phase 9), the values summed q[8] 0..15.
 */
static inline u32 t32_aef_fliker_detect(u8 *p, const u8 *q, u8 *r, const u8 *smp, const struct t32_aef_ops *ops)
{
	u32 ev[13], cnt = t32_ld32(p, 0), i;
	t32_aef_s32 rem = (t32_aef_s32)cnt % 10;

	for (i = 0; i < 13; i++)
		ev[i] = i < 10 ? t32_ld32(p, 10812 + 4 * i) : t32_ld32(p, 11756 + 4 * (i - 10));
	if (rem == 0) {
		/* the oldest history word survives as the stored value */
		u32 keep = ev[t32_ld32(q, 12) - 1];

		for (i = 0; i < 11768; i++)
			p[i] = 0;
		t32_wr32(p, 10804, keep);
		for (i = 0; i < 10; i++)
			t32_wr32(p, 10812 + 4 * i, ev[i]);
		for (i = 0; i < 3; i++)
			t32_wr32(p, 11756 + 4 * i, ev[10 + i]);
		t32_aef_cpy(p + 4, smp, 4);
		t32_aef_cpy(p + 904, smp, 4);
	} else {
		t32_aef_cpy(p + 4, p + 904, 4);
		t32_aef_cpy(p + 904, smp, 4);
		if (rem > 0) {
			t32_aef_diff(p, rem - 1);
			if (rem == 9)
				t32_aef_decide(p, q, r, ev, ops);
		}
	}
	cnt = t32_ld32(p, 0) + 1;
	t32_wr32(p, 0, cnt);
	return cnt;
}

/*
 * tisp_ae_tune (vendor 0x2e938): the per-frame AE tuning step of one channel.
 * T = vendor e15a8[ch][0] (tune), B = e15a8[ch][1] (buffer), R = e1588[ch]
 * (state), eflag = &e1510[ch], dcw = word 900 of the channel block at dc284
 * (selects the list allocation), total = vendor a1, p = vendor a2 (two words,
 * the weights of the convergence step), w = vendor a3 (two words).  The
 * vendor return value is the result 0..2 (high word: B, a leftover pointer).
 * Hooks: t32_aeu_ops (fix_point_*, bilinear, the frame rate stubs).
 *
 * Not reproduced: the vendor call of fix_point_mult2_64 on (X, q) whose result
 * is never used, and the second fix_point_mult3_64 / div64 / mult2_64 call of
 * the !up branch (the vendor recomputes the same pure value).
 */
struct t32_aeu_ops {
	const struct t32_ae_ops *ae;
	const struct t32_aev_ops *ev;
};

static inline t32_u64 t32_aeu_shl(t32_u64 v, u32 n)
{
	return v << (n & 63);
}

/* history tables: 15 x (64 bit value, 16 bit gain) from T, optionally filtered, stored back */
static inline void t32_aeu_tables(u8 *T, u8 *tab, u8 *y)
{
	u32 i;

	for (i = 0; i < 15; i++) {
		t32_wr64(tab, 8 * i, t32_ld64(T, 1496 + 8 * i));
		t32_wr16(y, 2 * i, t32_st16(T, 2870 + 2 * i));
	}
	if (t32_st16(T, 2956) != 1) {
		for (i = 0; i < 15; i++) {
			t32_wr64(T, 128 + 8 * i, t32_ld64(tab, 8 * i));
			t32_wr32(T, 528 + 4 * i, t32_st16(y, 2 * i));
		}
		return;
	}
	for (i = 0; i < 15; i++) {
		u32 c = t32_st16(T, 2958), h = t32_st16(y, 2 * i);
		t32_u64 v = (t32_ld64(tab, 8 * i) * c) >> 7;
		t32_aef_s32 g = (t32_aef_s32)(h * c) / 128;

		g = (t32_aef_s32)((u32)g * t32_st16(T, 2972 + 2 * i));
		h = ((u32)((t32_aef_s32)((u32)g * t32_st16(T, 2960)) / 16384)) & 0xffff;
		if (v == 0)
			v = 1;
		if (h == 0)
			h = 1;
		t32_wr64(tab, 8 * i, v);
		t32_wr16(y, 2 * i, h);
		t32_wr64(T, 128 + 8 * i, v);
		t32_wr32(T, 528 + 4 * i, h);
	}
}

/* the new gain step W (64 bit, fixed point) from the convergence ratio r */
static inline t32_u64 t32_aeu_step(const u8 *T, u32 sh, u32 up, u32 r, u8 *h198, u8 *h196, t32_u64 W,
				   const struct t32_aeu_ops *o)
{
	u32 sh2 = sh << 1;
	t32_u64 one = t32_aeu_shl(1, sh2), R64 = t32_aeu_shl(r, sh), H;

	if (up) {
		H = t32_aeu_shl(t32_st16(h196, 0), sh);
		if (t32_st16(T, 2858) < r)
			return o->ev->mult2_64(sh2, H, R64) + one;
		return o->ev->mult3_64(sh2, H, R64, R64) + one;
	}
	H = t32_aeu_shl(t32_st16(h198, 0), sh);
	if (0U - t32_st16(T, 2860) < 0U - r) {
		t32_u64 q = t32_aeu_shl(1024 - r, sh), d;

		d = o->ev->div64(sh2, o->ev->mult3_64(sh2, H, R64, R64), q);
		return one < d ? W : one - d;
	}
	{
		t32_u64 e = o->ev->mult2_64(sh2, H, R64);

		return one < e ? W : one - e;
	}
}

/* the result 0..2 of the load check (tail of the vendor function) */
static inline u32 t32_aeu_result(const u8 *T, const u8 *B, const u8 *w, u32 sh, u32 total, u32 tgt, u32 old,
				 u32 flag0, const struct t32_aeu_ops *o)
{
	u32 sx = sh & 31, sum1 = 0, sum2 = 0, k, res = old, w0 = t32_ld32(w, 0), w1 = t32_ld32(w, 4);

	for (k = 0; k != t32_ld32(T, 1696); k++)
		sum1 += t32_ld32(B, 4 * k);
	for (k = t32_ld32(T, 1688) + 1; k < 256; k++)
		sum2 += t32_ld32(B, 4 * k);
	if (t32_st16(B, 8568) != 0 || flag0 == 1)
		return res;
	if (total < tgt) {
		if (!(t32_ld32(T, 1700) < sum1))
			return res;
		if (o->ev->mult2_32(sh, w1 << sx, t32_st16(T, 2936)) < (w0 << sx) && !(w0 < 41))
			return 2;
		if (!(o->ev->mult2_32(sh, w0 << sx, t32_st16(T, 2932)) < (w1 << sx)))
			return 0;
		return !(w1 < 41);
	}
	if (!(t32_ld32(T, 1692) < sum2))
		return res;
	if (o->ev->mult2_32(sh, w1 << sx, t32_st16(T, 2938)) < (w0 << sx))
		return 2;
	return o->ev->mult2_32(sh, w0 << sx, t32_st16(T, 2934)) < (w1 << sx);
}

static inline u32 t32_ae_tune(u8 *T, u8 *B, u8 *R, u8 *eflag, u32 dcw, u32 ch, u32 total, const u8 *p,
			      const u8 *w, const struct t32_aeu_ops *o)
{
	u32 sh = t32_st16(T, 1736), sx = sh & 31, sh2 = sh << 1, flag0 = t32_st16(B, 8568), old = t32_st16(B, 8574);
	u32 k256 = t32_st16(T, 2920), tgt = 0, q, r, up, nR, cur, i, sum, avg, o192, o188, o184;
	t32_u64 d1 = t32_aeu_shl(1024, sh), one = t32_aeu_shl(1, sh2), W, X, U, Lo, G, N, V;
	u8 tab[120], y[30], h198[2], h196[2];

	X = o->ev->mult3_64(sh, t32_ld32(B, 7620), t32_ld32(B, 7628), t32_aeu_shl(t32_ld32(B, 7616), sh));
	W = d1 - t32_aeu_shl(1, sh + 8);
	t32_wr16(h198, 0, t32_st16(T, 2852));
	t32_wr16(h196, 0, t32_st16(T, 2854));
	t32_aeu_tables(T, tab, y);
	if (total == 0)
		total = 1;
	for (i = 0; i < 255; i++)
		t32_wr16(B, 11856 + 2 * i, t32_st16(B, 11858 + 2 * i));
	t32_wr16(B, 12366, total);
	if (t32_st16(B, 8566) == 0) {
		t32_u64 lim = sx == 31 ? 0xffffffff80000000ull : (t32_u64)1 << sx;

		X = t32_aeu_shl(t32_ld64(T, 368), 10);
		if (X < lim)
			X = lim;
		t32_wr16(B, 8566, 1);
		t32_wr16(B, 8568, 0);
		R[1285] = 1;
		t32_wr16(B, 8570, t32_ae_long_target(X, tab, y, sh, o->ae));
	}
	q = o->ev->div32(sh, t32_st16(B, 8570) << sx, total << sx);
	if (t32_st16(B, 8568) == 1) {
		if (q < t32_st16(T, 2862) && t32_st16(T, 2864) < q)
			tgt = t32_st16(B, 8570);
		else
			t32_wr16(B, 8568, 0);
	}
	t32_wr16(B, 8572, total);
	cur = t32_st16(B, 8570);
	if (t32_st16(B, 8568) != 0 && R[1285] == 0)
		goto tail;
	if (!(total < cur - t32_st16(T, 2866)) && !(cur + t32_st16(T, 2868) < total) && R[1285] == 0) {
		t32_wr16(B, 8568, 1);
		goto tail;
	}
	if (R[1297] != 0) {
		if (R[1296] < 10)
			R[1296] = 10;
		else if (!(R[1296] < 201))
			R[1296] = 200;
		tgt = R[1296];
	} else {
		tgt = t32_ae_long_target(X, tab, y, sh, o->ae);
	}
	t32_wr16(B, 8570, tgt);
	up = total < tgt;
	r = o->ev->div32(sh, (up ? tgt - total : total - tgt) << sx, total << sx);
	t32_ae_calc_convergence_speed(p, h198, h196, t32_st16(T, k256 == 1 ? 1644 : 1648), sh, sh << 10, o->ae);
	W = t32_aeu_step(T, sh, up, r, h198, h196, W, o);
	G = t32_aeu_shl(t32_st16(T, 2856), sh);
	U = G + d1;
	Lo = d1 - G;
	if (W < U && one < W)
		W = U;
	else if (Lo < W && W < one)
		W = Lo;
	N = o->ev->mult2_64(sh2, W, t32_aeu_shl(X, sh)) >> (sh & 63);
	nR = t32_ld32(R, 1540);
	for (i = 0; i < 29; i++)
		t32_wr32(R, 1380 + 4 * i, t32_ld32(R, 1384 + 4 * i));
	t32_wr32(R, 1496, R[1297]);
	for (i = 0, sum = 0; i < 30; i++)
		sum += t32_ld32(R, 1380 + 4 * i);
	for (i = 0; i + 1 < nR; i++)
		t32_wr32(R, 1300 + 4 * i, t32_ld32(R, 1304 + 4 * i));
	t32_wr32(R, 1300 + 4 * nR - 4, (u32)N);
	for (i = 0, avg = 0; i < nR; i++)
		avg += t32_ld32(R, 1300 + 4 * i) / nR;
	V = sum < 16 ? N : (t32_u64)avg;
	if (V < t32_aeu_shl(1, sh))
		V = t32_aeu_shl(1, sh);
	if (t32_ld32(T, 1740) == 1 && dcw == 1)
		t32_aev_list_alloc_calc(T, B, R, ch, (u32)V, (u32)(V >> 32), &o192, &o188, &o184, tgt, o->ev);
	else
		t32_aev_ev_alloc_calc(T, B, R, (u32)V, (u32)(V >> 32), &o192, &o188, &o184, tgt, o->ev);
	t32_wr32(R, 576, o192);
	t32_wr32(R, 580, o188);
	t32_wr32(R, 588, o184);
	*eflag = 1;
tail:
	r = t32_aeu_result(T, B, w, sh, total, tgt, old, flag0, o);
	t32_wr16(B, 8574, r);
	return r;
}

#endif /* TX_ISP_T32_AEFU_H */
