/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 LDC helpers: parameter getter and the IPC frame-down interrupt handler.
 */
#ifndef TX_ISP_T33_LDCB_H
#define TX_ISP_T33_LDCB_H

#include "tx_isp_t33_blk.h"

#define T33_LDC_PAR_BYTES	80

/* lib_tisp_ldc_get_par: copy the 80-byte parameter record of input idx; -1 when none was stored */
static inline int t33_ldc_get_par(const u8 *exist, const u8 *par, u32 idx, u8 *out)
{
	u32 i = idx & 0xff, k;

	if (!exist[i])
		return -1;
	for (k = 0; k < T33_LDC_PAR_BYTES; k++)
		out[k] = par[80 * i + k];
	return 0;
}

/*
 * tisp_ipc_fra_down_interrupt: when the pending WDMA channel is the one after idx, program
 * its base (and, for a long config, the second address) and clear the pending channel.
 * cfg: st_dma_cfg words (92 bytes = 23 words per channel).
 */
static inline int t33_ipc_fra_down(u32 idx, u8 sensor_num, signed char *vinum, const u32 *cfg,
				   void (*wr)(u32, u32))
{
	u32 next = (idx & 0xff) + 1;
	const u32 *c;

	next = (next % sensor_num) & 0xff;
	if (*vinum == (int)next) {
		c = cfg + *vinum * 23;
		wr(0x20058, c[13]);
		if (c[7] >= 35)
			wr(0x2005c, c[18] * c[19] + c[13]);
		*vinum = -1;
	}
	return 0;
}

#endif /* TX_ISP_T33_LDCB_H */
