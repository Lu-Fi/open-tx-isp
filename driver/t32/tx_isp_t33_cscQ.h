/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 CSC API / refresh helpers of the vendor module (openimp #99), binary oracle block "cscQ"
 * (tests/t33_binoracle/cscQ.conf):
 *
 * - tisp_csc_api_get: copy the channel's working block (csc_para[ch] + 8, 92 bytes) to the caller's
 *   buffer; -1 when the channel has no csc_para entry.
 * - tisp_csc_api_set: copy the caller's 92 bytes into the working block, mark it (byte 99 = 2), then
 *   refresh by mode, sync the API parameters, update and write the registers; returns 0.
 * - tisp_csc_fra_down_interrupt: with stIspInitPar[904] == 1, tispCscRefresh == 1 triggers the module
 *   (13) and clears the flag, == 2 writes the registers again; returns 1.
 *
 * csc_para is the vendor's per-channel pointer array (u8 *csc_para[4]); the working block is the
 * second pointer of the channel (offset 8 of the entry).
 */
#ifndef TX_ISP_T33_CSCQ_H
#define TX_ISP_T33_CSCQ_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_csc.h"

#define T33_CSCQ_BYTES	92

/* tisp_csc_api_get: ch = a0 & 0xff, dst = the caller's 92-byte buffer */
static inline int t33_csc_api_get(u8 *csc_para[], u32 ch, u8 *dst)
{
	u8 *e = csc_para[ch & 0xff];

	if (!e)
		return -1;
	memcpy(dst, *(u8 **)(e + 8), T33_CSCQ_BYTES);
	return 0;
}

/* tisp_csc_api_set: ch = a0 & 0xff, src = the caller's 92-byte buffer */
static inline int t33_csc_api_set(u8 *csc_para[], u32 ch, const u8 *src,
				  void (*refresh_by_mode)(u32, u32, u8 *),
				  void (*sync_api_para)(u32, u32),
				  void (*update_para)(u32),
				  int (*write_reg)(u32))
{
	u8 *e = csc_para[ch & 0xff];
	u8 *cp = *(u8 **)e;

	cp[99] = 2;
	memcpy(*(u8 **)(e + 8), src, T33_CSCQ_BYTES);
	refresh_by_mode(*(u32 *)src, 1, *(u8 **)(e + 8));
	sync_api_para(ch & 0xff, 0);
	update_para(ch & 0xff);
	write_reg(ch & 0xff);
	return 0;
}

/* tisp_csc_fra_down_interrupt: ch = a0 & 0xff */
static inline int t33_csc_fra_down_interrupt(u32 ch, const u32 *init_par, u8 *refresh,
					     void (*cfg_trig)(u32, u32),
					     int (*write_reg)(u32))
{
	if (init_par[904 / 4] != 1)
		return 1;
	if (*refresh == 1) {
		cfg_trig(ch & 0xff, T33_CSC_ID);
		*refresh = 0;
	} else if (*refresh == 2) {
		write_reg(ch & 0xff);
	}
	return 1;
}

#endif /* TX_ISP_T33_CSCQ_H */
