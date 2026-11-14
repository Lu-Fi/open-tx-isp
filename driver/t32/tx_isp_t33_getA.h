/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 small getters of the ISP init parameter table (binary oracle block getA).
 */
#ifndef TX_ISP_T33_GETA_H
#define TX_ISP_T33_GETA_H

#include "tx_isp_t33_blk.h"

#define T33_INITPAR_STRIDE	1008
#define T33_INITPAR_NSENS	904	/* byte offset of the sensor count in channel 0 */
#define T33_SENSATTR_STRIDE	232

/* tisp_g_ispinitp_func: &stIspInitPar[ch & 0xff] */
static inline u8 *t33_g_ispinitp(u8 *par, u32 ch)
{
	return par + (ch & 0xff) * 1008;
}

/* lib_tisp_g_ispinitp_func: tail call of the above */
static inline u8 *t33_lib_g_ispinitp(u8 *par, u32 ch)
{
	return t33_g_ispinitp(par, ch);
}

/* tisp_get_sensor_attr: &stSensorAttr[ch] while ch < stIspInitPar word 226, else NULL */
static inline u8 *t33_get_sensor_attr(const u8 *par, u8 *attr, u32 ch)
{
	u32 n = t33_le32(par + 904);

	return ch < n ? attr + ch * 232 : NULL;
}

/* tisp_wdma_save_file_process: nothing to do */
static inline int t33_wdma_save_file(void)
{
	return 0;	/* no-op */
}

#endif /* TX_ISP_T33_GETA_H */
