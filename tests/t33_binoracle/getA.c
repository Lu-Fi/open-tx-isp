/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "getA": init parameter getters. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_getA.h"

u8 stIspInitPar[4032];
u8 stSensorAttr[928];

/*
 * The harness compares the raw returned address; the vendor's globals sit at fixed addresses in the
 * emulated module (this .ko build), so our pointers are rebased onto them (NULL stays 0).
 */
#define VENDOR_INITPAR	269637992U
#define VENDOR_SENSATTR	269642488U

static u32 rebase(const u8 *p, const u8 *base, u32 vbase)
{
	return p ? (u32)(p - base) + vbase : 0;
}

u32 bo_tisp_g_ispinitp_func(u32 ch)
{
	return rebase(t33_g_ispinitp(stIspInitPar, ch), stIspInitPar, VENDOR_INITPAR);
}

u32 bo_lib_tisp_g_ispinitp_func(u32 ch)
{
	return rebase(t33_lib_g_ispinitp(stIspInitPar, ch), stIspInitPar, VENDOR_INITPAR);
}

u32 bo_tisp_get_sensor_attr(u32 ch)
{
	return rebase(t33_get_sensor_attr(stIspInitPar, stSensorAttr, ch), stSensorAttr, VENDOR_SENSATTR);
}
int bo_tisp_wdma_save_file_process(u32 a) { (void)a; return t33_wdma_save_file(); }
