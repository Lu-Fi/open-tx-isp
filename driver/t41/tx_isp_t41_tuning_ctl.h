/* SPDX-License-Identifier: MIT */
/*
 * T41 tuning-control payload helpers (isp-m0 0xc0105435 envelope), kept
 * free of kernel state so the host tests can check them against the stock
 * tx_isp_core_ops_g_ctrl/s_ctrl layouts (libt41-firmware 1.2.6).
 */
#ifndef TX_ISP_T41_TUNING_CTL_H
#define TX_ISP_T41_TUNING_CTL_H
#include "tx_isp_t41_tmo.h"

/*
 * IMPISPSENSORAttr { hts, vts, fps, width, height } (stock g_ctrl 0x08000033):
 * hts/vts are the u16 total_width/total_height of the sensor attribute
 * (attr+180/+182), fps is the packed num<<16|den of the core video slot
 * (video+68), width/height the sensor output size (video+60/+64).
 */
#define T41_SENSOR_ATTR_WORDS 5U

static inline int t41_sensor_attr_fill(const unsigned char *video,
		const unsigned char *attr, unsigned int *out)
{
	if (!video || !attr || !out)
		return -1;
	out[0] = t41_tmo_le16(attr + 180);
	out[1] = t41_tmo_le16(attr + 182);
	out[2] = t41_tmo_le32(video + 68);
	out[3] = t41_tmo_le32(video + 60);
	out[4] = t41_tmo_le32(video + 64);
	return 0;
}

/*
 * Packed sensor frame rate num<<16|den (stock s_ctrl 0x08000070 passes it
 * inline).  0: valid; -1: zero numerator/denominator; -2: faster than the
 * reference mode rate (the sensor mode cannot shorten its frame below the
 * mode's VTS, so such a request can only be refused by the sensor).
 * ref == 0 disables the upper bound.  num, den <= 0xffff, so the cross
 * products fit 32 bits and need no 64-bit helper.
 */
static inline int t41_sensor_fps_check(unsigned int fps, unsigned int ref)
{
	unsigned int num = fps >> 16, den = fps & 0xffffU;
	unsigned int ref_num = ref >> 16, ref_den = ref & 0xffffU;

	if (!num || !den)
		return -1;
	if (ref_num && ref_den && num * ref_den > ref_num * den)
		return -2;
	return 0;
}

/*
 * IMPISPAEWeightAttr { u32 roi_enable; u32 weight_enable; u8 ae_roi[225];
 * u8 ae_weight[225]; } - stock copies 460 bytes.  api_ae_set_weight: an
 * enabled ROI goes to AE params+0x4ee and 8 - roi to state+0x2528; an
 * enabled weight table goes to params+0x82e.  api_ae_get_weight returns the
 * stored enables and both tables from the params.
 *
 * The open zone meter (t41_ae_weight_mean) needs at least one non-zero
 * weight, and a ROI with at least one zone below and one above zero (it
 * divides by the foreground and the background area).  Values are the
 * documented 0..8.  Such input is refused instead of stalling the meter.
 */
#define T41_AE_WEIGHT_ATTR_BYTES 460U
#define T41_AE_WEIGHT_ROI 8U
#define T41_AE_WEIGHT_TABLE 233U
#define T41_AE_PARAM_ROI 0x4eeU
#define T41_AE_PARAM_WEIGHT 0x82eU
#define T41_AE_STATE_ROI_INV 0x2528U
#define T41_AE_WEIGHT_ZONES 225U

static inline int t41_ae_weight_table_ok(const unsigned char *t, int roi)
{
	unsigned int i, low = 0, high = 0;

	for (i = 0; i < T41_AE_WEIGHT_ZONES; ++i) {
		if (t[i] > 8)
			return 0;
		low |= t[i] < 8;
		high |= t[i] > 0;
	}
	return roi ? low && high : high;
}

static inline int t41_ae_weight_set(unsigned char *params,
		unsigned int param_bytes, unsigned char *state,
		unsigned int state_bytes, unsigned char *enables,
		const unsigned char *attr)
{
	unsigned int roi, weight, i;

	if (!params || !state || !enables || !attr ||
	    param_bytes < T41_AE_PARAM_WEIGHT + T41_AE_WEIGHT_ZONES ||
	    state_bytes < T41_AE_STATE_ROI_INV + T41_AE_WEIGHT_ZONES)
		return -1;
	roi = t41_tmo_le32(attr);
	weight = t41_tmo_le32(attr + 4);
	if (roi > 1 || weight > 1)
		return -1;
	if (roi && !t41_ae_weight_table_ok(attr + T41_AE_WEIGHT_ROI, 1))
		return -1;
	if (weight && !t41_ae_weight_table_ok(attr + T41_AE_WEIGHT_TABLE, 0))
		return -1;
	enables[0] = roi;
	enables[1] = weight;
	if (roi) {
		for (i = 0; i < T41_AE_WEIGHT_ZONES; ++i) {
			params[T41_AE_PARAM_ROI + i] = attr[T41_AE_WEIGHT_ROI + i];
			state[T41_AE_STATE_ROI_INV + i] =
				8 - attr[T41_AE_WEIGHT_ROI + i];
		}
	}
	if (weight)
		for (i = 0; i < T41_AE_WEIGHT_ZONES; ++i)
			params[T41_AE_PARAM_WEIGHT + i] =
				attr[T41_AE_WEIGHT_TABLE + i];
	return 0;
}

static inline int t41_ae_weight_get(const unsigned char *params,
		unsigned int param_bytes, const unsigned char *enables,
		unsigned char *attr)
{
	unsigned int i;

	if (!params || !enables || !attr ||
	    param_bytes < T41_AE_PARAM_WEIGHT + T41_AE_WEIGHT_ZONES)
		return -1;
	for (i = 0; i < T41_AE_WEIGHT_ATTR_BYTES; ++i)
		attr[i] = 0;
	attr[0] = enables[0];
	attr[4] = enables[1];
	for (i = 0; i < T41_AE_WEIGHT_ZONES; ++i) {
		attr[T41_AE_WEIGHT_ROI + i] = params[T41_AE_PARAM_ROI + i];
		attr[T41_AE_WEIGHT_TABLE + i] = params[T41_AE_PARAM_WEIGHT + i];
	}
	return 0;
}

#endif
