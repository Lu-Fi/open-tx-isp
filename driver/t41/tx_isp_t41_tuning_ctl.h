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

#endif
