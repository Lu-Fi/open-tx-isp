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

#endif /* TX_ISP_T32_AEFU_H */
