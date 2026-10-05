/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T23 tuning controls of the stock 1.3.0 libimp that the recovered driver
 * did not route (they failed with -EINVAL): the data handling of the stock
 * apical_isp_core_ops_s_ctrl / g_ctrl handlers and the tisp_* helpers they
 * call, taken from libt23-firmware-1.3.0-540-31014.a.
 *
 * Only plain buffers in, plain buffers out: the driver passes its copies of
 * the stock objects (the lifted AE's oem_* arrays, tparams), the host tests
 * pass their own.  No locking, no user copies here.
 */
#ifndef TX_ISP_T23_TUNING_EXT_H
#define TX_ISP_T23_TUNING_EXT_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdint.h>
#include <string.h>
#endif

/* ---- AE ------------------------------------------------------------- */

#define T23X_ZONES 225U                 /* 15 x 15 */
#define T23X_WEIGHT_MAX 8U
#define T23X_AE_CTRLS_WORDS 44U         /* tisp_ae_ctrls, 176 bytes */
#define T23X_AE_HIST_BYTES 1052U        /* tisp_ae_hist(_last) */
#define T23X_AE_HIST_THRESH 1044U       /* 4 bin thresholds */
#define T23X_AE_HIST_BINS 1024U         /* 5 x u32 bin sums */
#define T23X_AE_HIST_NODES 1048U        /* nodeh, nodev */
#define T23X_AE_ZONE_OFF 13872U         /* IspAeStatic: 15 x 15 luma */
#define T23X_GAIN_UNITY 1024U

/* tparams offsets (active bank) and the same block in the day/night banks */
#define T23X_TP_BANK_BASE 0x13100U
#define T23X_TP_AE_ZONE_WEIGHT 0x13448U /* bank +840 */
#define T23X_TP_AE_ROUI_WEIGHT 0x137ccU /* bank +1740 */
#define T23X_TP_AE_ROI_WEIGHT 0x13b50U  /* bank +2640 */

/*
 * IMPISPWeight (15 x 15 bytes, 0..8) to the stock u32 table
 * (apical_isp_ae_s_roi / apical_isp_ae_zone_weight_s_attr).  The stock code
 * stops at the first entry above 8 and fails; nothing is applied then.
 */
static inline int t23x_weight_expand(const uint8_t *in, uint32_t *out)
{
	unsigned int i;

	for (i = 0; i < T23X_ZONES; i++)
		if (in[i] > T23X_WEIGHT_MAX)
			return -EINVAL;
	for (i = 0; i < T23X_ZONES; i++)
		out[i] = in[i];
	return 0;
}

/* the get direction: the low byte of each word */
static inline void t23x_weight_pack(const uint32_t *in, uint8_t *out)
{
	unsigned int i;

	for (i = 0; i < T23X_ZONES; i++)
		out[i] = (uint8_t)in[i];
}

/* tisp_s_aeroi_weight: the "roui" table is 8 - roi per zone */
static inline void t23x_roui_from_roi(const uint32_t *roi, uint32_t *roui)
{
	unsigned int i;

	for (i = 0; i < T23X_ZONES; i++)
		roui[i] = T23X_WEIGHT_MAX - roi[i];
}

static inline uint32_t t23x_at_least_unity(uint32_t v)
{
	return v < T23X_GAIN_UNITY ? T23X_GAIN_UNITY : v;
}

/*
 * tisp_ae_manual_set on tisp_ae_ctrls (c) from a 44-word block (w), as the
 * stock code does it:
 *   w0 freeze: c0; then it (w3), again (w1), dgain (w2) and ISP dgain (w4)
 *              are all taken, the gains raised to at least 1x (1024);
 *   else each manual enable (w15 it, w16 again, w38 dgain, w17 ISP dgain)
 *              is stored and, when set, its value;
 *   w26 short-frame freeze, else w18/w19/w28/w39 for the short frame.
 * The caller clears IspAeFlag[7] (the stock tail does).
 */
static inline void t23x_ae_manual_set(uint32_t *c, const uint32_t *w)
{
	c[0] = w[0];
	if (w[0]) {
		c[1] = t23x_at_least_unity(w[1]);
		c[4] = t23x_at_least_unity(w[4]);
		c[2] = t23x_at_least_unity(w[2]);
		c[3] = w[3];
	} else {
		c[15] = w[15];
		if (w[15])
			c[3] = w[3];
		c[16] = w[16];
		if (w[16])
			c[1] = t23x_at_least_unity(w[1]);
		c[17] = w[17];
		if (w[17])
			c[4] = t23x_at_least_unity(w[4]);
		c[38] = w[38];
		if (w[38])
			c[2] = t23x_at_least_unity(w[2]);
	}
	c[26] = w[26];
	if (w[26]) {
		c[20] = t23x_at_least_unity(w[20]);
		c[27] = t23x_at_least_unity(w[27]);
		c[21] = w[21];
		c[29] = t23x_at_least_unity(w[29]);
	} else {
		c[18] = w[18];
		if (w[18])
			c[21] = w[21];
		c[19] = w[19];
		if (w[19])
			c[20] = t23x_at_least_unity(w[20]);
		c[28] = w[28];
		if (w[28])
			c[29] = t23x_at_least_unity(w[29]);
		c[39] = w[39];
		if (w[39])
			c[27] = t23x_at_least_unity(w[27]);
	}
}

/*
 * tisp_set_ae_freeze: the current ctrls with freeze (word 0) and the
 * short-frame freeze (word 26) both set to 0 or 1, through
 * tisp_ae_manual_set.  Other values fail.
 */
static inline int t23x_ae_freeze(uint32_t *c, uint32_t on)
{
	uint32_t w[T23X_AE_CTRLS_WORDS];

	if (on > 1U)
		return -EINVAL;
	memcpy(w, c, sizeof(w));
	w[0] = on;
	w[26] = on;
	t23x_ae_manual_set(c, w);
	return 0;
}

/*
 * apical_isp_expr_s_ctrl: IMPISPExpr.s_attr {mode, unit, time} through
 * tisp_s_ae_attr (the current ctrls with it = w3 and the it manual enable
 * = w15).  mode 0 = auto (enable 0), 1 = manual; unit 0 = lines, 1 = us
 * (divided by the line time, which must not be 0).  Other values fail.
 * The stock auto path passes an uninitialised it, used only while AE is
 * frozen; the current it is kept here.
 */
static inline int t23x_expr_set(uint32_t *c, uint32_t mode, uint32_t unit,
				uint16_t time, uint32_t line_us)
{
	uint32_t w[T23X_AE_CTRLS_WORDS];

	memcpy(w, c, sizeof(w));
	if (mode == 0U) {
		w[15] = 0;
	} else if (mode == 1U) {
		w[15] = 1;
		if (unit == 1U) {
			if (!line_us)
				return -EINVAL;
			w[3] = (uint32_t)time / line_us;
		} else if (unit == 0U) {
			w[3] = time;
		} else {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}
	t23x_ae_manual_set(c, w);
	return 0;
}

/*
 * GetAeHist: IMPISPAEHist (16 bytes) from tisp_ae_hist_last: the four bin
 * thresholds, the five bin sums as u16, nodeh and nodev.
 */
static inline void t23x_ae_hist_pack(const uint8_t *h, uint8_t *out)
{
	unsigned int i;

	memset(out, 0, 16);
	memcpy(out, h + T23X_AE_HIST_THRESH, 4);
	for (i = 0; i < 5U; i++) {
		uint32_t v;

		memcpy(&v, h + T23X_AE_HIST_BINS + 4U * i, sizeof(v));
		out[4U + 2U * i] = (uint8_t)v;
		out[5U + 2U * i] = (uint8_t)(v >> 8);
	}
	out[14] = h[T23X_AE_HIST_NODES];
	out[15] = h[T23X_AE_HIST_NODES + 1U];
}

/*
 * IMPISPAEState (12 bytes) from _ae_stat: stable = word 1 != 0, target =
 * word 0, ae_mean = word 2.  The stock code leaves bytes 1..3 as stack.
 */
static inline void t23x_ae_state_pack(const uint32_t *stat, uint8_t *out)
{
	memset(out, 0, 12);
	out[0] = stat[1] ? 1U : 0U;
	memcpy(out + 4, &stat[0], 4);
	memcpy(out + 8, &stat[2], 4);
}

/*
 * tisp_ae_s_min on ae_exp_th (IspAeExp[k] points at ae_exp_th[k]) and the
 * reload flags in ae_extra_at_list_wdr: a minimum integration time above
 * 0 and not above the maximum (th[0]) goes to th[4] (flag 0), a minimum
 * analog gain of at least 1x and not above th[1] to th[5] (flag 1).  Bad
 * values are only logged by the stock code; the call still returns 0.
 * Returns a mask of the values taken (bit 0 it, bit 1 again).
 */
static inline unsigned int t23x_ae_min_set(uint32_t *th, uint32_t *wdr,
					   uint32_t it_min, uint32_t again_min)
{
	unsigned int taken = 0;

	if (it_min && th[0] >= it_min) {
		wdr[0] = 1;
		th[4] = it_min;
		taken |= 1U;
	}
	if (again_min >= T23X_GAIN_UNITY && th[1] >= again_min) {
		wdr[1] = 1;
		th[5] = again_min;
		taken |= 2U;
	}
	return taken;
}

#endif /* TX_ISP_T23_TUNING_EXT_H */
