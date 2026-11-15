/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 LDC interrupt status (vendor tisp_ldc_t33_intp_handle 0xf3a1c, reached from ldc_interrupt_service_routine via
 * lib_tisp_ldc_intp_handle; openimp #590):
 *   read the status 0x27080, write it back to 0x27084 (clear);
 *   bit 0 (pass done): ldcIntpInfo[0] += 1, and the next online pass is armed (0x27024 = 1) only when the
 *        stIspInitPar mode word (+904) is 1 or 2 and the LDC init flag (byte +44) is 1;
 *   else bit 1 (error): ldcIntpInfo[1] += 1;
 *   returns the status.
 * Checked against the vendor instructions by tests/t33_binoracle (block ldcI).
 */
#ifndef TX_ISP_T33_LDCINTP_H
#define TX_ISP_T33_LDCINTP_H

#include "tx_isp_t33_blk.h"

#define T33_LDC_INTP_STA	0x27080
#define T33_LDC_INTP_CLR	0x27084
#define T33_LDC_INTP_START	0x27024
#define T33_INITPAR_MODE	904	/* stIspInitPar: mode word */
#define T33_INITPAR_LDC_ON	44	/* stIspInitPar: LDC init flag (byte) */

struct t33_ldcintp_hw {
	u32 (*rd)(u32 reg);		/* system_reg_read */
	void (*wr)(u32 reg, u32 val);	/* system_reg_write */
};

static inline u32 t33_ldc_intp_handle(const u8 *initpar, u32 *info, const struct t33_ldcintp_hw *hw)
{
	u32 st = hw->rd(T33_LDC_INTP_STA);

	hw->wr(T33_LDC_INTP_CLR, st);
	if (st & 1) {
		info[0]++;
		if (t33_le32(initpar + T33_INITPAR_MODE) - 1 < 2 && initpar[T33_INITPAR_LDC_ON] == 1)
			hw->wr(T33_LDC_INTP_START, 1);
	} else if (st & 2) {
		info[1]++;
	}
	return st;
}

#endif
