/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) LDC parameter state helpers of the vendor tx-isp-t33.ko (tisp_ldc_set_bypass_state,
 * tisp_ldc_get_extra_par, tisp_ldc_reset_par_state); proven by tests/t33_binoracle/ldc.conf (openimp #99).
 * Separate from tx_isp_t33_ldc.h (the list layout), which stays free of tx_isp_t33_blk.h.
 */
#ifndef TX_ISP_T33_LDCPAR_H
#define TX_ISP_T33_LDCPAR_H

#include "tx_isp_t33_blk.h"

#define T33_LDC_PAR_BYTES	80
#define T33_LDC_EBADSZ		-1

/*
 * tisp_ldc_set_bypass_state: the vendor clears a 4-byte block on its stack, sets byte [idx] when state == 1 and
 * calls tisp_top_ldc_bypass_refresh(idx & 0xff, block) (the caller does that call).  Returns the masked index.
 * idx is a channel (0..3): the vendor does not range-check it, an index >= 4 writes past its block (from 12 on
 * over the saved return address); we ignore such an index instead of copying that.
 */
static inline u32 t33_ldc_bypass_buf(u8 *b, u32 idx, u32 state)
{
	b[0] = b[1] = b[2] = b[3] = 0;
	if (state == 1 && (idx & 0xff) < 4)
		b[idx & 0xff] = 1;
	return idx & 0xff;
}

/*
 * tisp_ldc_get_extra_par: info word +16 is the "present" flag, +24 the size, +28 the 80-byte
 * parameter block; stored to slot idx & 0xff of par and the flag to exist[]. Returns 0, or
 * T33_LDC_EBADSZ when the size is not 80 (the vendor also logs it).
 */
static inline int t33_ldc_extra_par(u8 *exist, u8 *par, u32 idx, const u8 *info)
{
	u32 flag, size, i;

	flag = t33_le32(info + 16);
	if (flag != 1)
		return 0;
	size = t33_le32(info + 24);
	if (size != T33_LDC_PAR_BYTES)
		return T33_LDC_EBADSZ;
	for (i = 0; i < T33_LDC_PAR_BYTES; i++)
		par[(idx & 0xff) * T33_LDC_PAR_BYTES + i] = info[28 + i];
	exist[idx & 0xff] = (u8)flag;
	return 0;
}

/* tisp_ldc_reset_par_state */
static inline void t33_ldc_reset_par(u8 *exist, u32 idx)
{
	exist[idx & 0xff] = 0;
}

#endif /* TX_ISP_T33_LDCPAR_H */
