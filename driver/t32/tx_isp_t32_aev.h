/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE exposure allocation: readable C of the vendor functions
 * tisp_ae_api_ev_alloc_calc, tisp_ae_ev_alloc_calc and tisp_ae_ev_list_alloc_calc
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  The fixed point maths are
 * hooks (the vendor's fix_point_* functions); every function is checked
 * against the vendor C by tests/t32_oracle (t32_aev*_oracle).
 */
#ifndef TX_ISP_T32_AEV_H
#define TX_ISP_T32_AEV_H

#include "tx_isp_t32_stats.h"

struct t32_aev_ops {
	u32 (*div32)(u32 shift, u32 a, u32 b);
	u32 (*mult2_32)(u32 shift, u32 a, u32 b);
	t32_u64 (*div64)(u32 shift, t32_u64 a, t32_u64 b);
	t32_u64 (*mult2_64)(u32 shift, t32_u64 a, t32_u64 b);
	t32_u64 (*mult3_64)(u32 shift, t32_u64 a, t32_u64 b, t32_u64 c);
	/* tisp_ae_list_alloc_calc only: the frame rate hooks */
	void (*set_fps)(u32 ch);			/* tisp_set_ae_sensor_fps */
	u32 (*max_exp_fps)(u32 ch, u32 ev);		/* tisp_ae_max_exp_calc_fps */
};

/* low word of a 64 bit logical shift right (vendor __lshrdi3 on a 32 bit value) */
static inline u32 t32_aev_lsr(u32 v, u32 sh)
{
	return (u32)((t32_u64)v >> (sh & 63));
}

/*
 * Shared core of tisp_ae_api_ev_alloc_calc and the default case of
 * tisp_ae_ev_alloc_calc: from the target x = a3:a2 and the state words st
 * (608, 612, 620, table 1700) it updates the three allocations *s0 (<< shift),
 * *s1, *s2.  Returns 1 when the vendor sets its "limited" flag (the caller of
 * tisp_ae_ev_alloc_calc counts it in st[1184]).
 */
static inline u32 t32_aev_core(const u8 *st, u32 sh, u32 mode, u32 t5, u32 t6, u32 k50, u32 n, u32 a2,
			       u32 a3, u32 m, u32 *ps0, u32 *ps1, u32 *ps2, const struct t32_aev_ops *ops)
{
	u32 sx = sh & 31, sB = t32_ld32(st, 612), sC = t32_ld32(st, 620);
	u32 s0 = *ps0, s1 = *ps1, s2 = *ps2, flag = 0;
	u32 amax = t32_ld32(st, 608) << sx;
	t32_u64 xx = ops->div64(sh, (t32_u64)a3 << 32 | a2, m), px;
	u32 lo = (u32)xx, hi = (u32)(xx >> 32), th, v, r;

	if (hi == 0 && lo < amax) {
		lo = amax;
		hi = 0;
	}
	xx = (t32_u64)hi << 32 | lo;
	if (mode == 1) {
		th = t32_ld32(st, 1700) << sx;
		if (hi == 0 && lo < th) {
			if (t5 == 0) {
				v = t32_aev_lsr(lo, sh) << sx;
				if (v < s0)
					s0 = v;
				lo = ops->div32(sh, lo, s0);
				if (!(s2 < lo)) {
					s1 = mode << sx;
					s2 = lo;
				} else if (!(ops->mult2_32(sh, s2, s1) < lo)) {
					s1 = ops->div32(sh, lo, s2);
				}
			} else if (t5 == mode) {
				if (th < s0)
					s0 = th;
				s2 = t5 << sx;
				s1 = s2;
			} else {
				int f = th < s0;

				r = ops->div32(sh, t6 << sx, k50 << sx);
				r = ops->mult2_32(sh, th, r);
				r = (r >> sx) << sx;
				if (f)
					s0 = th;
				if (r < s0)
					s0 = r;
				s2 = mode << sx;
				if (!(lo < s0)) {
					s0 = t32_aev_lsr(lo, sh) << sx;
					s2 = ops->div32(sh, lo, s0);
				}
				s1 = 1U << sx;
			}
		} else {
			const u8 *tbl = st + 1700;
			u32 idx = 1, lim;

			v = ops->div32(sh, t32_ld32(tbl, 4 * n) << sx, amax);
			lim = (20U << sx) + s0;
			for (;;) {
				if (!(lim < v))
					break;
				n = (n - 1) & 0xffff;
				if (n == 0) {
					n = 1;
					break;
				}
				v = t32_ld32(tbl, 4 * n) << sx;
			}
			while (!(n < idx)) {
				if (hi != 0 || !(lo < (t32_ld32(tbl, 4 * idx) << sx))) {
					idx = (idx + 1) & 0xff;
					continue;
				}
				break;
			}
			v = t32_ld32(tbl, 4 * (idx - 1)) << sx;
			if (v < s0)
				s0 = v;
			v = (u32)ops->div64(sh, xx, s0);
			if (!(s2 < v)) {
				s1 = 1U << sx;
				s2 = v;
			} else {
				r = ops->div32(sh, v, s2);
				if (r < s1)
					s1 = r;
				flag = 1;
			}
		}
	} else {
		r = ops->mult2_32(sh, m, amax);
		if (a3 == 0 && !(r < a2)) {
			s1 = sC;
			s2 = sB;
			s0 = amax;
			flag = 1;
		} else if (hi == 0 && !(s0 < lo)) {
			s0 = t32_aev_lsr(lo, sh) << sx;
			s1 = (u32)ops->div64(sh, lo, s0);
			s2 = 1024;
		} else {
			px = ops->mult2_64(sh, s0, s2);
			if (!(px < xx)) {
				s2 = (u32)ops->div64(sh, xx, s0);
				s1 = 1U << sx;
			} else if (!(ops->mult3_64(sh, s0, s2, s1) < xx)) {
				px = ops->mult2_64(sh, s0, s2);
				s1 = (u32)ops->div64(sh, xx, px);
			} else {
				flag = 1;
			}
		}
	}
	*ps0 = s0;
	*ps1 = s1;
	*ps2 = s2;
	return flag;
}

/*
 * tisp_ae_api_ev_alloc_calc: splits the target x = a3:a2 into the three
 * allocation outputs *o_a (exposure, >> shift), *o_b, *o_c from the channel state
 * st, the tuning (shift 1736, mode 2920, 2922, 2924) and buf[12842].  The "limit"
 * constant of the tuning-2924 ratio is 50.  Returns *o_c.
 */
static inline u32 t32_aev_api_ev_alloc_calc(const u8 *tune, const u8 *buf, const u8 *st, u32 a2, u32 a3,
					    u32 *o_a, u32 *o_b, u32 *o_c, const struct t32_aev_ops *ops)
{
	u32 sh = t32_st16(tune, 1736), sx = sh & 31, sB = t32_ld32(st, 612), sC = t32_ld32(st, 620);
	u32 s0 = t32_ld32(st, 624) << sx;
	u32 s1 = ops->div32(sh, t32_ld32(st, 636), sC);
	u32 m = ops->mult2_32(sh, sB, sC);
	u32 s2 = ops->div32(sh, t32_ld32(st, 628), sB);

	t32_aev_core(st, sh, t32_st16(tune, 2920), t32_st16(tune, 2922), t32_st16(tune, 2924), 50, buf[12842],
		     a2, a3, m, &s0, &s1, &s2, ops);
	*o_a = s0 >> sx;
	*o_b = ops->mult2_32(sh, s2, sB);
	*o_c = ops->mult2_32(sh, s1, sC);
	return *o_c;
}

/*
 * buf word at off: the vendor's clamp of the running value into the buffer.
 * A new value below the stored one replaces it; otherwise a stored value below
 * the floor is raised to the floor.  Returns the resulting value.
 */
static inline u32 t32_aev_clamp(u8 *buf, u32 off, u32 nv, u32 floor)
{
	u32 cur = t32_ld32(buf, off);

	if (nv < cur) {
		t32_wr32(buf, off, nv);
		return nv;
	}
	if (cur < floor) {
		t32_wr32(buf, off, floor);
		return floor;
	}
	return cur;
}

/*
 * tisp_ae_ev_alloc_calc: like tisp_ae_api_ev_alloc_calc, but with the
 * st[1276/1277/1279] selector (st[1284] == 1 forces case 7) choosing which of
 * the three allocations are taken from the running clamps in buf (8608, 8612,
 * 8620) and which are derived from the target x = a3:a2.  Case 0 (no selector)
 * is the shared core; its "limited" flag counts in st[1184] and sets buf u16
 * 8568 once the count passes tune[1620], every other case clears st[1184].
 * k50 is the vendor's 8th argument.  The vendor return value (a leftover
 * pointer) is not reproduced.
 */
static inline void t32_aev_ev_alloc_calc(const u8 *tune, u8 *buf, u8 *st, u32 a2, u32 a3, u32 *o_a,
					 u32 *o_b, u32 *o_c, u32 k50, const struct t32_aev_ops *ops)
{
	u32 sh = t32_st16(tune, 1736), sx = sh & 31, sA = t32_ld32(st, 608), sB = t32_ld32(st, 612);
	u32 sC = t32_ld32(st, 620), sD = t32_ld32(st, 628), sE = t32_ld32(st, 624), sF = t32_ld32(st, 636);
	u32 s0 = sE << sx, s1 = ops->div32(sh, sF, sC), m = ops->mult2_32(sh, sB, sC);
	u32 s2 = ops->div32(sh, sD, sB), sel, s7, m2, xlo, flag = 0;
	t32_u64 xx, p;
	int xf;

	sel = (u32)st[1277] << 1 | (u32)st[1279] << 2 | st[1276];
	if (st[1284] == 1)
		sel = 7;
	else if (!(sel - 1 < 7))
		sel = 0;
	switch (sel) {
	case 1:
		s0 = t32_aev_clamp(buf, 8608, sE, sA) << sx;
		s7 = 1U << sx;
		xx = ops->div64(sh, (t32_u64)a3 << 32 | a2, s0);
		xlo = (u32)xx;
		{
			u32 xhi = (u32)(xx >> 32), lhi = (u32)((int)s7 >> 31);
			u32 d = sD, f = sF;
			int s2f = 0, c18 = 0;

			if (lhi < xhi) {
				s2f = 1;
				c18 = 1;
			} else if (xhi != lhi) {
				d = f = s7;
			} else {
				s2f = xhi != 0;
				if (s7 < xlo)
					c18 = 1;
				else
					d = f = s7;
			}
			if (c18) {
				if (!s2f && !(d < xlo)) {
					d = (u32)ops->div64(sh, (t32_u64)a3 << 32 | a2, s0);
					f = s7;
				} else {
					m2 = ops->mult2_32(sh, d, f);
					if (!s2f && !(m2 < xlo))
						f = (u32)ops->div64(sh, xlo, d);
				}
			}
			s2 = d;
			s1 = f;
		}
		*o_b = ops->mult2_32(sh, s2, sB);
		*o_c = ops->mult2_32(sh, s1, sC);
		break;
	case 2:
		s2 = t32_aev_clamp(buf, 8612, sD, sB);
		xx = ops->div64(sh, (t32_u64)a3 << 32 | a2, s2);
		xlo = (u32)xx;
		xf = (xx >> 32) != 0;
		s7 = sA << sx;
		m2 = ops->mult2_32(sh, m, s7);
		if (!xf && !(m2 < xlo)) {
			s1 = 1U << sx;
			s0 = s7;
		} else if (!xf && !(s0 < xlo)) {
			s0 = t32_aev_lsr(xlo, sh) << sx;
			s1 = 1U << sx;
		} else {
			m2 = ops->mult2_32(sh, s0, s1);
			if (!xf && !(m2 < xlo))
				s1 = (u32)ops->div64(sh, xlo, s0);
		}
		*o_b = s2;
		*o_c = ops->mult2_32(sh, s1, sC);
		break;
	case 3:
		s0 = t32_aev_clamp(buf, 8608, sE, sA) << sx;
		s1 = t32_aev_clamp(buf, 8612, sD, sB);
		p = ops->mult2_64(sh, s0, s1);
		xx = ops->div64(sh, (t32_u64)a3 << 32 | a2, p);
		s7 = sF;
		if ((xx >> 32) == 0) {
			if ((u32)xx < sC)
				s7 = sC;
			else if ((u32)xx < s7)
				s7 = (u32)xx;
		}
		*o_b = s1;
		*o_c = ops->mult2_32(sh, s7, sC);
		break;
	case 4:
		s1 = t32_aev_clamp(buf, 8620, sF, sC);
		xx = ops->div64(sh, (t32_u64)a3 << 32 | a2, s1);
		xlo = (u32)xx;
		xf = (xx >> 32) != 0;
		s7 = sA << sx;
		m2 = ops->mult2_32(sh, m, s7);
		if (!xf && !(m2 < xlo)) {
			s2 = sB;
			s0 = s7;
		} else if (!xf && !(s0 < xlo)) {
			s0 = t32_aev_lsr(xlo, sh) << sx;
			s2 = sB;
		} else {
			m2 = ops->mult2_32(sh, s0, s2);
			if (!xf && !(m2 < xlo))
				s2 = (u32)ops->div64(sh, xlo, s0);
		}
		*o_b = ops->mult2_32(sh, s2, sB);
		*o_c = s1;
		break;
	case 5:
		s0 = t32_aev_clamp(buf, 8608, sE, sA) << sx;
		s1 = t32_aev_clamp(buf, 8620, sF, sC);
		p = ops->mult2_64(sh, s0, s1);
		xx = ops->div64(sh, (t32_u64)a3 << 32 | a2, p);
		s7 = sD;
		if ((xx >> 32) == 0) {
			if ((u32)xx < sB)
				s7 = sB;
			else if ((u32)xx < s7)
				s7 = (u32)xx;
		}
		*o_b = ops->mult2_32(sh, s7, sB);
		*o_c = s1;
		break;
	case 6:
		s2 = t32_aev_clamp(buf, 8612, sD, sB);
		s1 = t32_aev_clamp(buf, 8620, sF, sC);
		p = ops->mult2_64(sh, s2, s1);
		xx = ops->div64(sh, (t32_u64)a3 << 32 | a2, p);
		s7 = sA << sx;
		m2 = ops->mult2_32(sh, m, s7);
		if ((xx >> 32) == 0) {
			if (!(m2 < (u32)xx))
				s0 = s7;
			else if (!(s0 < (u32)xx))
				s0 = t32_aev_lsr((u32)xx, sh) << sx;
		}
		*o_b = s2;
		*o_c = s1;
		break;
	case 7:
		s0 = t32_aev_clamp(buf, 8608, sE, sA) << sx;
		*o_b = t32_aev_clamp(buf, 8612, sD, sB);
		*o_c = t32_aev_clamp(buf, 8620, sF, sC);
		break;
	default:
		flag = t32_aev_core(st, sh, t32_st16(tune, 2920), t32_st16(tune, 2922), t32_st16(tune, 2924),
				    k50, buf[12842], a2, a3, m, &s0, &s1, &s2, ops);
		*o_b = ops->mult2_32(sh, s2, sB);
		*o_c = ops->mult2_32(sh, s1, sC);
		break;
	}
	*o_a = s0 >> sx;
	if (flag) {
		u32 cnt = t32_ld32(st, 1184) + 1;

		t32_wr32(st, 1184, cnt);
		if (t32_ld32(tune, 1620) < cnt)
			t32_wr16(buf, 8568, 1);
	} else {
		t32_wr32(st, 1184, 0);
	}
}

/* frame rate state shared by the cases of tisp_ae_ev_list_alloc_calc */
struct t32_aev_fps {
	u32 nv, t1, t2, t3, t4, ch;
};

/*
 * The vendor's frame rate decision for the exposure value v: above the new
 * limit nv the rate limit st[1264] is applied, above buf[8688] the rate
 * follows tisp_ae_max_exp_calc_fps (never raised above st[1264]), below it the
 * buf[8692] rate is restored.  Every change goes to tune[1664] and
 * tisp_set_ae_sensor_fps.  The max_exp hook's 3rd/4th argument registers are
 * stale in the vendor code and not passed.
 */
static inline void t32_aev_fps_adjust(u8 *tune, const struct t32_aev_fps *f, u32 v, const struct t32_aev_ops *ops)
{
	u32 r;

	if (f->nv < v) {
		if (f->t2 != f->t1) {
			t32_wr32(tune, 1664, f->t1);
			ops->set_fps(f->ch);
		}
	} else if (f->t3 < v) {
		r = ops->max_exp_fps(f->ch, v);
		if (f->t1 < r && f->t2 != r) {
			t32_wr32(tune, 1664, r);
			ops->set_fps(f->ch);
		}
	} else if (v < f->t3 && f->t4 != f->t2 && f->t2 != 0) {
		t32_wr32(tune, 1664, f->t4);
		ops->set_fps(f->ch);
	}
}

/* buf[8608]: frame rate decision on the stored value, clamp to nv and floor sA, returns the exposure << shift */
static inline u32 t32_aev_exp_clamp(u8 *tune, u8 *buf, const struct t32_aev_fps *f, u32 sA, u32 sx,
				    const struct t32_aev_ops *ops)
{
	u32 cur = t32_ld32(buf, 8608), s4;

	t32_aev_fps_adjust(tune, f, cur, ops);
	if (f->nv < cur) {
		/* the vendor re-reads buf[8608] and tests nv < value again: dead after this store */
		t32_wr32(buf, 8608, f->nv);
		cur = f->nv;
	}
	s4 = cur << sx;
	if (cur < sA) {
		s4 = sA << sx;
		t32_wr32(buf, 8608, sA);
	}
	return s4;
}

/* the result lo word of a division with a plain 32 bit divisor */
static inline u32 t32_aev_div64_lo(const struct t32_aev_ops *ops, u32 sh, t32_u64 a, u32 b)
{
	return (u32)ops->div64(sh, a, (t32_u64)b);
}

/*
 * tisp_ae_ev_list_alloc_calc: ev_alloc_calc with the exposure list.  The
 * selector (st[1276/1277/1279], st[1284] == 1 forces case 7) takes the
 * exposure (cases 1, 3, 5, 7: buf[8608] with frame rate decision) and the
 * allocations from the running clamps in buf (8608, 8612, 8620) as ev_alloc_calc
 * does, but the rest from the target x = a3:a2 directly.  Selector 0 (and any
 * other value) searches the list of st+80 (8 byte entries, 16 of them) and
 * interpolates in the table at st+672 (16 byte entries: exposure, gain1, gain2)
 * around buf u16 8576 (the stored position); mode tune[2920] == 1 then limits the
 * exposure to st[1700] with the tune[2922] rules (k is the vendor's 8th
 * argument).  The outputs are *o_a (exposure >> shift), *o_b, *o_c.
 * Frame rate changes: tune[1664] and the hooks.  The vendor return value (a
 * leftover pointer) is not reproduced.
 */
static inline void t32_aev_list_alloc_calc(const u8 *ctune, u8 *buf, u8 *st, u32 ch, u32 a2, u32 a3, u32 *o_a,
					   u32 *o_b, u32 *o_c, u32 k, const struct t32_aev_ops *ops)
{
	u8 *tune = (u8 *)ctune;
	u32 sh = t32_st16(tune, 1736), sx = sh & 31, sA = t32_ld32(st, 608), sB = t32_ld32(st, 612);
	u32 sC = t32_ld32(st, 620), sD = t32_ld32(st, 628), sE = t32_ld32(st, 624), sF = t32_ld32(st, 636);
	struct t32_aev_fps f = { t32_ld32(buf, 8684), t32_ld32(st, 1264), t32_ld32(st, 1260), t32_ld32(buf, 8688),
				 t32_ld32(buf, 8692), ch };
	u32 s4 = f.nv << sx, s7 = f.nv, s2, s3, s5, s6, m, s1i, s2i, sel, xq, r, mode, th, t5;
	t32_u64 X = (t32_u64)a3 << 32 | a2, q;
	int xf;

	m = ops->mult2_32(sh, sB, sC);
	s1i = ops->div32(sh, sF, sC);
	s2i = ops->div32(sh, sD, sB);
	sel = (u32)st[1277] << 1 | (u32)st[1279] << 2 | st[1276];
	if (st[1284] == 1)
		sel = 7;
	else if (!(sel - 1 < 7))
		sel = 0;
	switch (sel) {
	case 1:
		s4 = t32_aev_exp_clamp(tune, buf, &f, sA, sx, ops);
		q = ops->div64(sh, X, s4);
		xq = (u32)q;
		xf = (q >> 32) != 0;
		if (!xf && !(m < xq)) {
			*o_b = sB;
			*o_c = sC;
		} else {
			if (!xf && !(sD < xq)) {
				*o_b = t32_aev_div64_lo(ops, sh, X, s4);
				*o_c = sC;
			} else {
				*o_b = sD;
				r = ops->mult2_32(sh, sD, sF);
				if (xf || r < xq)
					*o_c = sF;
				else
					*o_c = t32_aev_div64_lo(ops, sh, xq, sD);
			}
		}
		break;
	case 2:
	case 4:
	case 6:
		if (sel == 2)
			s6 = t32_aev_clamp(buf, 8612, sD, sB);
		else if (sel == 4)
			s6 = t32_aev_clamp(buf, 8620, sF, sC);
		else
			s6 = 0;
		if (sel == 6) {
			s3 = t32_aev_clamp(buf, 8612, sD, sB);
			s5 = t32_aev_clamp(buf, 8620, sF, sC);
			q = ops->div64(sh, X, ops->mult2_32(sh, s3, s5));
		} else {
			q = ops->div64(sh, X, s6);
		}
		xq = (u32)q;
		xf = (q >> 32) != 0;
		s7 = sA << sx;
		r = ops->mult2_32(sh, m, s7);
		if (sel == 6) {
			if (xf) {
				/* s4 stays nv << shift */
			} else if (!(r < xq)) {
				s4 = s7;
			} else if (s4 < xq) {
				/* stays */
			} else {
				t32_aev_fps_adjust(tune, &f, xq, ops);
				s4 = t32_aev_lsr(xq, sh) << sx;
			}
			*o_b = s3;
			*o_c = s5;
		} else if (!xf && !(r < xq)) {
			s4 = s7;
			if (sel == 2) {
				*o_b = s6;
				*o_c = 1U << sx;
			} else {
				*o_b = sB;
				*o_c = s6;
			}
		} else if (xf || s4 < xq) {
			r = ops->mult2_32(sh, s4, sel == 2 ? s1i : s2i);
			if (sel == 2) {
				*o_b = s6;
				*o_c = (xf || r < xq) ? s1i : t32_aev_div64_lo(ops, sh, xq, s4);
			} else {
				*o_b = (xf || r < xq) ? s2i : t32_aev_div64_lo(ops, sh, xq, s4);
				*o_c = s6;
			}
		} else {
			t32_aev_fps_adjust(tune, &f, xq, ops);
			s4 = t32_aev_lsr(xq, sh) << sx;
			if (sel == 2) {
				*o_b = s6;
				*o_c = t32_aev_div64_lo(ops, sh, xq, s4);
			} else {
				*o_b = sB;
				*o_c = s6;
			}
		}
		break;
	case 3:
		s4 = t32_aev_exp_clamp(tune, buf, &f, sA, sx, ops);
		s3 = t32_aev_clamp(buf, 8612, sD, sB);
		q = ops->div64(sh, X, ops->mult2_32(sh, s4, s3));
		*o_b = s3;
		if ((q >> 32) != 0)
			*o_c = sF;
		else if ((u32)q < sC)
			*o_c = sC;
		else
			*o_c = (u32)q < sF ? (u32)q : sF;
		break;
	case 5:
		s4 = t32_aev_exp_clamp(tune, buf, &f, sA, sx, ops);
		s3 = t32_aev_clamp(buf, 8620, sF, sC);
		q = ops->div64(sh, X, ops->mult2_32(sh, s4, s3));
		if ((q >> 32) != 0)
			*o_b = sD;
		else if ((u32)q < sB)
			*o_b = sB;
		else
			*o_b = (u32)q < sD ? (u32)q : sD;
		*o_c = s3;
		break;
	case 7:
		s4 = t32_aev_exp_clamp(tune, buf, &f, sA, sx, ops);
		*o_b = t32_aev_clamp(buf, 8612, sD, sB);
		*o_c = t32_aev_clamp(buf, 8620, sF, sC);
		break;
	default:
		/* exposure list */
		s2 = 0;
		s5 = (u32)-1;
		s4 = 0;
		{
			const u8 *L = st + 672, *p = st + 80;
			u32 pos = t32_st16(buf, 8576), ehi, elo;
			int cmp0 = 0, search = 1, ge = 1;
			const u8 *P5, *P6;
			u32 f1, nf1, f2, f3, nf2, d, s6b;

			for (;;) {	/* the entries below x, at most 16 */
				ehi = t32_ld32(p, 4);
				elo = t32_ld32(p, 0);
				if (!(ehi < a3 || (ehi == a3 && elo < a2))) {
					cmp0 = s5 == (u32)-1;	/* the vendor also re-tests entry 0 for s5 == 0: it is below x then, no effect */
					break;
				}
				s5 = (s5 + 1) & 0xff;
				if (s5 == 15)
					break;
				if (t32_ld32(p, 8) == elo && t32_ld32(p, 12) == ehi) {
					break;
				}
				p += 8;
			}
			if (cmp0) {
				ehi = t32_ld32(st, 84);
				elo = t32_ld32(st, 80);
				s5 = 0;
				if (ehi != a3 || elo != a2) {	/* entry 0 is >= x here, so x < entry 0 unless equal */
					s7 = t32_ld32(L, 4) << sx;
					s4 = t32_ld32(L, 8);
					s2 = t32_ld32(L, 12);
					search = 0;
				}
			}
			if (search) {
				p = st + 80 + 8 * pos;
				ehi = t32_ld32(p, 4);
				elo = t32_ld32(p, 0);
				if (ehi < a3 || (ehi == a3 && elo < a2)) {
					P5 = L + 16 * pos;
					s7 = t32_ld32(P5, 4) << sx;
					s4 = t32_ld32(P5, 8);
					s2 = t32_ld32(P5, 12);
					ge = 0;
				}
			}
			if (search && ge) {
				P5 = L + 16 * s5;
				P6 = P5 + 16;
				f1 = t32_ld32(P5, 4);
				nf1 = t32_ld32(P5, 20);
				f2 = t32_ld32(P5, 8);
				f3 = t32_ld32(P5, 12);
				nf2 = t32_ld32(P6, 8);
				if (sE < f1 || sE < nf1) {
					r = ops->max_exp_fps(ch, nf1);
					t32_wr32(tune, 1664, r);
					ops->set_fps(ch);
				} else if (f1 < sE && nf1 < sE && f.t3 < sE) {
					r = ops->max_exp_fps(ch, nf1);
					t32_wr32(tune, 1664, f.t4 < r ? f.t4 : r);
					ops->set_fps(ch);
				}
				d = t32_aev_div64_lo(ops, sh, X, ops->mult2_32(sh, f2, f3));
				s7 = f1 << sx;
				s6b = 0;
				if (d < s7) {
					s4 = t32_aev_div64_lo(ops, sh, X, ops->mult2_32(sh, s7, f3));
					s6 = f2;
					if (s4 < s6) {
						s6b = 1;
					} else {
						s6 = nf2;
						s6b = 2;
					}
				} else {
					s7 = nf1 << sx;
					if (s7 < d) {
						s4 = t32_aev_div64_lo(ops, sh, X, ops->mult2_32(sh, s7, f3));
						s6 = f2;
						if (s4 < s6) {
							s6b = 1;
						} else {
							s6 = nf2;
							s6b = 2;
						}
					} else {
						s7 = (d >> sx) << sx;
						q = ops->div64(sh, X, ops->mult2_32(sh, s7, f3));
						s4 = f2;
						s2 = f3;
						if (s4 == 0) {
							s4 = (u32)q;
							s6 = nf2;
							s6b = 2;
						} else {
							s2 = t32_aev_div64_lo(ops, sh, X, ops->mult2_32(sh, s7, f2));
						}
					}
				}
				if (s6b == 2 && !(s6 < s4)) {
					s2 = f3;
				} else if (s6b) {
					s2 = t32_aev_div64_lo(ops, sh, X, ops->mult2_32(sh, s7, s6));
					s4 = s6;
				}
			}
			/* mode 1: the exposure is limited to the table entry st[1700] */
			mode = t32_st16(tune, 2920);
			t5 = t32_st16(tune, 2922);
			if (mode == 1) {
				int go1 = 0;

				th = t32_ld32(st, 1700) << sx;
				if (!(s7 < th)) {
					r = ops->div32(sh, (20U << sx) + s7, th);
					r = t32_ld32(st, 1696 + 4 * ((r >> sx) & 0xff)) << sx;
					th = t32_ld32(L, 16 * (s5 + 1) + 4) << sx;
					th = th < r ? th : r;
					go1 = 1;
				} else if (t5 == 1) {
					go1 = 1;
				} else if (t5 == 2) {
					u32 kk = t32_st16(tune, 2924);

					r = ops->div32(sh, kk << sx, k << sx);
					s5 = ops->mult2_32(sh, th, r);
					if (!(s5 < th)) {
						s7 = th;
					} else if (s7 < s5) {
						s3 = ops->div32(sh, s7, s5);
						s4 = ops->mult2_32(sh, s4, s3);
						if (s4 < sB) {
							r = ops->mult2_32(sh, s2, ops->div32(sh, s3, sC));
							s2 = sC < r ? r : sC;
							s4 = sB;
						}
						s7 = (s5 >> sx) << sx;
					}
				}
				if (go1) {
					s5 = ops->div32(sh, s7, th);
					s4 = ops->mult2_32(sh, s4, s5);
					s7 = th;
					if (s4 < sB) {
						r = ops->mult2_32(sh, s2, ops->div32(sh, s5, sC));
						s2 = sC < r ? r : sC;
						s4 = sB;
					}
				}
			}
		}
		*o_b = s4;
		*o_c = s2;
		s4 = s7;
		break;
	}
	*o_a = s4 >> sx;
}

#endif /* TX_ISP_T32_AEV_H */
