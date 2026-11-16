/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AE API accessors (block r11C): vendor api_ae_get_hist_bin, api_ae_get_y_zone,
 * tisp_ae_api_get_flicker_flag, tisp_ae_api_get_gobal_roi_luma, tisp_g_ae_flicker_flag,
 * tisp_g_ae_statis_attr, tisp_g_face_ae_luma, tisp_ae_par_sensor_trig, calculate_exponential.
 * The spin lock the vendor takes around the histogram / y-zone copies is left to the caller.
 */
#ifndef TX_ISP_T33_R11C_H
#define TX_ISP_T33_R11C_H

#include "tx_isp_t33_blk.h"

#define T33_AE_HIST_STRIDE	1052U
#define T33_AE_HIST_BYTES	1024U
#define T33_AE_HIST_BINS	5U
#define T33_AE_HIST_OUT		10U
#define T33_AE_YZONE_OFF	1928U
#define T33_AE_YZONE_BYTES	900U
#define T33_AE_YZONE_OUT	1034U
#define T33_AE_FLICKER_OFF	1696U
#define T33_AE_ROI_A		1524U
#define T33_AE_ROI_B		1528U
#define T33_AE_TATTR_STRIDE	72U
#define T33_AE_TATTR_SEL	1588U

/* api_ae_get_hist_bin: 1024 bytes at out + 10, five u16 bin limits at out + 0 */
static inline void t33_ae_get_hist_bin(const u8 *hist_last, u32 ch, u8 *out)
{
	const u8 *h = hist_last + ch * T33_AE_HIST_STRIDE;
	u32 i;

	for (i = 0; i < T33_AE_HIST_BYTES; i++)
		out[T33_AE_HIST_OUT + i] = h[i];
	for (i = 0; i < T33_AE_HIST_BINS; i++) {
		out[2 * i] = h[T33_AE_HIST_BYTES + 4 * i];
		out[2 * i + 1] = h[T33_AE_HIST_BYTES + 4 * i + 1];
	}
}

/* api_ae_get_y_zone: info[ch] is the channel's AE state (stAeInfo) */
static inline void t33_ae_get_y_zone(u8 *const *info, u32 ch, u8 *out)
{
	const u8 *s = info[ch] + T33_AE_YZONE_OFF;
	u32 i;

	for (i = 0; i < T33_AE_YZONE_BYTES; i++)
		out[i] = s[i];
}

/* tisp_ae_api_get_flicker_flag / tisp_g_ae_flicker_flag */
static inline void t33_ae_get_flicker_flag(u8 *const *cache, u32 ch, u32 *out)
{
	*out = t33_le32(cache[ch & 0xff] + T33_AE_FLICKER_OFF);
}

/* tisp_ae_api_get_gobal_roi_luma */
static inline void t33_ae_get_gobal_roi_luma(u8 *const *cache, u32 ch, u32 *o1, u32 *o2)
{
	const u8 *c = cache[ch & 0xff];

	*o1 = t33_le32(c + T33_AE_ROI_A);
	*o2 = t33_le32(c + T33_AE_ROI_B);
}

/* tisp_g_face_ae_luma */
static inline void t33_g_face_ae_luma(u8 *const *cache, u32 ch, u32 *out)
{
	t33_ae_get_gobal_roi_luma(cache, ch & 0xff, out, out + 1);
}

/* tisp_g_ae_statis_attr: histogram (or the gsm getter when the tattr flag is set), then the y zones */
static inline int t33_g_ae_statis_attr(const u8 *tattr, u32 ch, u8 *out, void (*hist)(u32, u8 *),
				       void (*gsm)(u32, u8 *), void (*yzone)(u32, u8 *))
{
	if (t33_le32(tattr + ch * T33_AE_TATTR_STRIDE + T33_AE_TATTR_SEL) == 0)
		hist(ch, out);
	else
		gsm(ch, out);
	yzone(ch, out + T33_AE_YZONE_OUT);
	return 0;
}

/* tisp_ae_par_sensor_trig: tail call of tisp_normal_ae_par_sensor_trig with the same argument */
static inline void t33_ae_par_sensor_trig(u32 ch, void (*normal)(u32 ch))
{
	normal(ch);
}

/*
 * calculate_exponential(x, n, a, q): e^x in Qq by n series terms; each x / k is shifted right by
 * a - q (low 5 bits), the 64 bit product with the running term is shifted right by q, the sum keeps 32 bits.
 */
static inline u32 t33_calculate_exponential(u32 x, u32 n, u32 a, u32 q)
{
	u32 acc = 1U << ((q & 0xff) & 31);
	long long t = (long long)(int)acc;
	int k;

	for (k = 1; k <= (int)(n & 0xff); k++) {
		int f = ((int)x / k) >> (((a & 0xff) - (q & 0xff)) & 31);

		t = (long long)((unsigned long long)(long long)f * (unsigned long long)t);
		t >>= (q & 0xff);
		acc += (u32)t;
	}
	return acc;
}

#endif /* TX_ISP_T33_R11C_H */
