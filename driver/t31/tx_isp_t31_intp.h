/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 tisp_simple_intp and tisp_dmsc_intp, vendor-equal to tx-isp-t31.ko (H20221206a) tisp_simple_intp 0x11350
 * and tisp_dmsc_intp 0x252ac (openimp #533; oracle tests/t31_binoracle/intp.conf).
 *
 * tisp_simple_intp(idx, frac, a): (unsigned) idx >= 8 returns a[8] (a negative index too); else a[idx] moved
 * towards a[idx + 1] by |a[idx + 1] - a[idx]| * frac with an ARITHMETIC >> 16 plus bit 15 as rounding, the
 * direction from a SIGNED compare (the shared tx_isp_lerp_u32 compares and shifts unsigned: differs once the
 * table words or the product reach 2^31).
 * tisp_dmsc_intp(gain): idx = (int)gain >> 16, frac = gain & 0xffff for the 44 DMSC curves of T31_DMSC_INTP_LIST
 * in vendor order (X(name, suffix): driver dmsc_<name>_val from dmsc_<name>_curve<suffix>, vendor
 * dmsc_<name>_intp from dmsc_<name>_array<suffix>).
 */
#ifndef TX_ISP_T31_INTP_H
#define TX_ISP_T31_INTP_H

static inline uint32_t t31_simple_intp(int idx, int frac, const uint32_t *a)
{
	int lo, hi, d, step, sub;

	if ((uint32_t)idx >= 8)
		return a[8];
	lo = (int)a[idx];
	hi = (int)a[idx + 1];
	if (lo == hi)
		return (uint32_t)lo;
	if (lo < hi) {
		d = (int)((uint32_t)hi - (uint32_t)lo);
		sub = 0;
	} else {
		d = (int)((uint32_t)lo - (uint32_t)hi);
		sub = 1;
	}
	d = (int)((uint32_t)d * (uint32_t)frac);
	step = (d >> 16) + ((d & 0x8000) >> 15);
	return sub ? (uint32_t)lo - (uint32_t)step : (uint32_t)lo + (uint32_t)step;
}

#define T31_DMSC_INTP_LIST(X) \
	X(hv_thres_1, ) \
	X(hv_stren, ) \
	X(aa_thres_1, ) \
	X(aa_stren, ) \
	X(hvaa_thres_1, ) \
	X(hvaa_stren, ) \
	X(uu_thres, _now) \
	X(uu_stren, _now) \
	X(alias_stren, ) \
	X(alias_thres_1, ) \
	X(alias_thres_2, ) \
	X(alias_dir_thres, ) \
	X(nor_alias_thres, ) \
	X(sp_d_w_stren, _now) \
	X(sp_d_b_stren, _now) \
	X(sp_d_brig_thres, ) \
	X(sp_d_dark_thres, ) \
	X(sp_ud_w_stren, _now) \
	X(sp_ud_b_stren, _now) \
	X(sp_ud_brig_thres, ) \
	X(sp_ud_dark_thres, ) \
	X(sp_alias_thres, ) \
	X(rgb_dir_thres, ) \
	X(rgb_alias_stren, ) \
	X(fc_alias_stren, ) \
	X(fc_t1_thres, ) \
	X(fc_t1_stren, ) \
	X(fc_t2_stren, ) \
	X(fc_t3_stren, ) \
	X(deir_fusion_thres, ) \
	X(deir_fusion_stren, ) \
	X(sp_d_v2_win5_thres, ) \
	X(sp_d_flat_stren, ) \
	X(sp_d_flat_thres, ) \
	X(sp_d_oe_stren, ) \
	X(sp_ud_std_stren, ) \
	X(sp_ud_std_thres, ) \
	X(sp_ud_flat_thres, ) \
	X(sp_ud_flat_stren, ) \
	X(sp_ud_oe_stren, ) \
	X(fc_lum_stren, ) \
	X(fc_lum_thres, ) \
	X(sp_d_ns_thres, ) \
	X(sp_ud_ns_thres, )

#define T31_DMSC_INTP_ONE(n, s)	dmsc_##n##_val = t31_simple_intp(hi, lo, dmsc_##n##_curve##s);

/* expands to the function body; use it after the dmsc_* globals are declared */
#define T31_DMSC_INTP_BODY(gain) do {			\
	int hi = (int)(gain) >> 16;			\
	int lo = (gain) & 0xffff;			\
							\
	T31_DMSC_INTP_LIST(T31_DMSC_INTP_ONE)		\
} while (0)

/*
 * tiziano_ct_ccm_interpolation 0x26ff0: zone from ct against 5000 - thr (D), thr + 3800 (D/T), 3800 - thr (T),
 * thr + 2800 (T/A), all unsigned and strict (ct == 5000 - thr is zone 1); without real, zone 0 / 2 / 4 equal to
 * the last zone returns; else last = zone and out[9] = D, T, A or the 32-bit (|hi - lo| * dist) / span blend.
 * Not taken over: the vendor's tail copy of out into a second .bss buffer (ours has one buffer).
 * span == 0 (thr == 600) is a vendor divide by zero; ours uses span 1 (beyond vendor guard).
 */
static inline void t31_ccm_blend(int32_t *out, const int32_t *hi, const int32_t *lo, uint32_t dist, uint32_t span)
{
	int i;

	for (i = 0; i < 9; i++) {
		if (hi[i] < lo[i])
			out[i] = lo[i] - (int32_t)(((uint32_t)lo[i] - (uint32_t)hi[i]) * dist / span);
		else
			out[i] = (int32_t)(((uint32_t)hi[i] - (uint32_t)lo[i]) * dist / span) + lo[i];
	}
}

static inline uint32_t t31_absdiff(uint32_t a, uint32_t b) { return b < a ? a - b : b - a; }

static inline void t31_ct_ccm_interp(uint32_t ct, uint32_t thr, uint32_t *flag, uint32_t *last, uint32_t real,
				     int32_t *out, const int32_t *d, const int32_t *t, const int32_t *a)
{
	uint32_t b_d = 5000 - thr, b_dt = thr + 3800, b_t = 3800 - thr, b_ta = thr + 2800, span;

	if (b_d < ct)
		*flag = 0;
	else if (b_dt < ct)
		*flag = 1;
	else if (b_t < ct)
		*flag = 2;
	else if (b_ta < ct)
		*flag = 3;
	else
		*flag = 4;
	if (!real) {
		if (*flag == 0 && *last == 0)
			return;
		if ((*flag == 2 || *flag == 4) && *last == *flag)
			return;
	}
	*last = *flag;
	switch (*flag) {
	case 0: memcpy(out, d, 36); break;
	case 1:
		span = t31_absdiff(b_d, b_dt);
		t31_ccm_blend(out, d, t, t31_absdiff(ct, b_dt), span ? span : 1);
		break;
	case 2: memcpy(out, t, 36); break;
	case 3:
		span = t31_absdiff(b_t, b_ta);
		t31_ccm_blend(out, t, a, t31_absdiff(ct, b_ta), span ? span : 1);
		break;
	default: memcpy(out, a, 36); break;
	}
}

/*
 * tiziano_ct_bcsh_interpolation 0x287e0: fixed zones ct >= 4951 D, >= 3851 D/T (|ct - 3850| / 1100), >= 3751 T,
 * >= 2851 T/A (|ct - 2850| / 900), else A; the same hysteresis on BCSH_real and the CCM blend.
 * Not taken over: the vendor's tail copy into a second .bss buffer.
 */
static inline void t31_ct_bcsh_interp(uint32_t ct, uint32_t *flag, uint32_t *last, int real,
				      int32_t *out, const int32_t *d, const int32_t *t, const int32_t *a)
{
	if (ct >= 4951)
		*flag = 0;
	else if (ct >= 3851)
		*flag = 1;
	else if (ct >= 3751)
		*flag = 2;
	else if (ct >= 2851)
		*flag = 3;
	else
		*flag = 4;
	if (!real) {
		if (*flag == 0 && *last == 0)
			return;
		if ((*flag == 2 || *flag == 4) && *last == *flag)
			return;
	}
	*last = *flag;
	switch (*flag) {
	case 0: memcpy(out, d, 36); break;
	case 1: t31_ccm_blend(out, d, t, t31_absdiff(ct, 3850), 1100); break;
	case 2: memcpy(out, t, 36); break;
	case 3: t31_ccm_blend(out, t, a, t31_absdiff(ct, 2850), 900); break;
	default: memcpy(out, a, 36); break;
	}
}

#endif /* TX_ISP_T31_INTP_H */
