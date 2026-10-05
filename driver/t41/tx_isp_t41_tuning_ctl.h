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

/*
 * IMPISPCoefftWb { u16 r, g, b } (stock tisp_bcsh_api_set_offset_rgb):
 * kept at bcsh_info+304 and written to the BCSH params +0x118..0x11c, the
 * RGB bias (value - 1024) of the BCSH matrix.  The hardware field is 11
 * bits, so larger values would wrap.
 */
#define T41_BCSH_RGB_OFFSET 0x118U

static inline int t41_bcsh_offset_rgb_ok(const unsigned short *rgb)
{
	return rgb && rgb[0] <= 2047 && rgb[1] <= 2047 && rgb[2] <= 2047;
}

/*
 * Gamma (0x08000025), IMPISPGammaAttr { u32 curve type; u16 gamma[129]; }
 * = 264 bytes.  Stock tx_isp_gamma_s_attr copies the 264 bytes and calls
 * tisp_s_Gamma -> tisp_gamma_set_attr(chan, attr, 0) on the gamma info
 * block (1060 bytes, params pointer at +0):
 *   +4     the last attribute (264 bytes), +8 its table
 *   +532   EV of the last strength interpolation, +536 the strength
 *   +540   the programmed 129-point curve (258 bytes)
 *   +1056  manual flag: the EV worker leaves the LUT alone while it is set
 *   params+300 the calibrated curve, params+558 ten per-EV strengths
 * type 0 clears the flag and the caller re-interpolates and writes the
 * calibrated curve.  Types 1..4 (sRGB, then the stock library's HDR and
 * REC709 which the stock driver maps to its rec709 and hdr tables in that
 * order, USER with the caller's table) set the flag, install the table as
 * programmed curve and as the calibrated curve with strength 255 for every
 * EV, and the caller writes the LUT.  tisp_g_Gamma returns the stored type
 * with the programmed curve in its table.
 *
 * Beyond the stock driver: a USER table with an entry above 4095 (12-bit
 * LUT) is refused, because the calibrated-curve path rejects such a table
 * afterwards.
 */
#define T41_GAMMA_ATTR_BYTES		264U
#define T41_GAMMA_TABLE_BYTES		258U
#define T41_GAMMA_INFO_BYTES		1060U
#define T41_GAMMA_INFO_ATTR		4U
#define T41_GAMMA_INFO_STRENGTH		536U
#define T41_GAMMA_INFO_TABLE		540U
#define T41_GAMMA_INFO_MANUAL		1056U
#define T41_GAMMA_PARAM_CURVE		300U
#define T41_GAMMA_PARAM_STRENGTHS	558U
#define T41_GAMMA_STRENGTHS		10U

/* 0: calibrated curve (re-interpolate and write), 1: table installed
 * (write the LUT), -1: invalid request. */
static inline int t41_gamma_attr_set(unsigned char *info,
		unsigned int info_bytes, unsigned char *params,
		unsigned int param_bytes, const unsigned char *attr,
		const unsigned char *srgb, const unsigned char *rec709,
		const unsigned char *hdr)
{
	unsigned int type, i;
	const unsigned char *table;

	if (!info || !params || !attr || !srgb || !rec709 || !hdr ||
	    info_bytes < T41_GAMMA_INFO_BYTES ||
	    param_bytes < T41_GAMMA_PARAM_STRENGTHS + T41_GAMMA_STRENGTHS)
		return -1;
	type = t41_tmo_le32(attr);
	if (type > 4)
		return -1;
	table = type == 1 ? srgb : type == 2 ? rec709 : type == 3 ? hdr :
		attr + 4;
	if (type == 4)
		for (i = 0; i < 129; ++i)
			if (t41_tmo_le16(attr + 4 + i * 2) > 4095)
				return -1;
	for (i = 0; i < T41_GAMMA_ATTR_BYTES; ++i)
		info[T41_GAMMA_INFO_ATTR + i] = attr[i];
	if (!type) {
		info[T41_GAMMA_INFO_MANUAL] = 0;
		return 0;
	}
	info[T41_GAMMA_INFO_MANUAL] = 1;
	for (i = 0; i < T41_GAMMA_TABLE_BYTES; ++i) {
		info[T41_GAMMA_INFO_TABLE + i] = table[i];
		params[T41_GAMMA_PARAM_CURVE + i] = table[i];
	}
	for (i = 0; i < T41_GAMMA_STRENGTHS; ++i)
		params[T41_GAMMA_PARAM_STRENGTHS + i] = 255;
	info[T41_GAMMA_INFO_STRENGTH] = 255;
	info[T41_GAMMA_INFO_STRENGTH + 1] = 0;
	info[T41_GAMMA_INFO_STRENGTH + 2] = 0;
	info[T41_GAMMA_INFO_STRENGTH + 3] = 0;
	return 1;
}

static inline int t41_gamma_attr_get(unsigned char *info,
		unsigned int info_bytes, unsigned char *out)
{
	unsigned int i;

	if (!info || !out || info_bytes < T41_GAMMA_INFO_BYTES)
		return -1;
	for (i = 0; i < T41_GAMMA_TABLE_BYTES; ++i)
		info[T41_GAMMA_INFO_ATTR + 4 + i] =
			info[T41_GAMMA_INFO_TABLE + i];
	for (i = 0; i < T41_GAMMA_ATTR_BYTES; ++i)
		out[i] = info[T41_GAMMA_INFO_ATTR + i];
	return 0;
}

/*
 * CCM (0x08000080), the 40-byte block { s8 manual, s8 sat, pad[2],
 * word[9] } libimp builds (tisp_s_ccm_attr -> tisp_ccm_api_set).  The CCM
 * info block (196 bytes) keeps it verbatim at +96 (tisp_ccm_api_get hands
 * the same 40 bytes back, whatever the matrix in use is) and the manual
 * flag (block byte 0 == 1) at +192.  With the flag set the selected
 * matrix is the block's words, each (word + 32) >> 6 clipped to 13 bits
 * (tisp_round_int64(word, 6) in tisp_ccm_interp_by_ct), and byte 1 == 0
 * skips the saturation transform (tisp_ccm_matrix_trans_by_sat).
 */
#define T41_CCM_BLOCK_BYTES		40U
#define T41_CCM_INFO_BYTES		196U
#define T41_CCM_INFO_BLOCK		96U
#define T41_CCM_INFO_MANUAL		192U

static inline int t41_ccm_block_set(unsigned char *info,
		unsigned int info_bytes, const unsigned char *block)
{
	unsigned int i;

	if (!info || !block || info_bytes < T41_CCM_INFO_BYTES)
		return -1;
	for (i = 0; i < T41_CCM_BLOCK_BYTES; ++i)
		info[T41_CCM_INFO_BLOCK + i] = block[i];
	info[T41_CCM_INFO_MANUAL] = block[0] == 1;
	return 0;
}

static inline int t41_ccm_block_get(const unsigned char *info,
		unsigned int info_bytes, unsigned char *block)
{
	unsigned int i;

	if (!info || !block || info_bytes < T41_CCM_INFO_BYTES)
		return -1;
	for (i = 0; i < T41_CCM_BLOCK_BYTES; ++i)
		block[i] = info[T41_CCM_INFO_BLOCK + i];
	return 0;
}

/* The manual matrix; the words are signed 32-bit, shifted arithmetically. */
static inline void t41_ccm_manual_matrix(const unsigned char *info,
		short out[9])
{
	unsigned int i;

	for (i = 0; i < 9; ++i) {
		int w = (int)t41_tmo_le32(info + T41_CCM_INFO_BLOCK + 4 + i * 4);
		int v = (w >> 6) + ((w >> 5) & 1);

		out[i] = v < -8192 ? -8192 : v > 8191 ? 8191 : v;
	}
}

/*
 * Return code of tisp_s_ccm_attr / tisp_g_ccm_attr for the TOP bypass word
 * (bit 15 = BCSH, bit 9 = CCM bypassed): the stock driver reports -1 (the
 * caller's ioctl fails) when the CCM block ran (and, for the BCSH
 * colour matrix it also programs, in the case where both blocks are
 * active) and 0 otherwise; with the CCM bypassed and the BCSH active only
 * the BCSH matrix is used.  0: CCM block, return 0; -1: CCM block, then
 * return -1; 1: BCSH colour matrix only (the open BCSH has none).
 */
static inline int t41_ccm_route(unsigned int bypass)
{
	if (bypass & 0x8000U)
		return (bypass & 0x200U) ? -1 : 0;
	return (bypass & 0x200U) ? 1 : -1;
}

/*
 * CSC (0x08000096), the 92-byte stock block { u32 mode, ... }.  Modes
 * 0..5 are the BT601/BT709/BT2020 full/limited presets, 6 the user table
 * (tisp_csc_api_set copies all 92 bytes); mode 7 is answered with -1 and
 * not applied.  libimp only sends 0..4.
 */
#define T41_CSC_BYTES		92U
#define T41_CSC_USER		6U

#endif
