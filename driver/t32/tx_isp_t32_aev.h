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

#endif /* TX_ISP_T32_AEV_H */
