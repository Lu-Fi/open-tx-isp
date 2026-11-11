/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame channel misc devices (vendor tx_isp_frame_chan_misc_register):
 * name "framechan<n>" (+700, n = +720), misc minor 60, 59, 58, 44, 43, 42,
 * 41, 40, 39 for framechan0..8 with fast start (d9008), else dynamic;
 * fops cf5b4; +40 registered.  The misc device is the start of the frame
 * channel (+0 minor, +4 name, +8 fops).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txframe_oracle).
 */
#ifndef TX_ISP_T32_TXFRAME_H
#define TX_ISP_T32_TXFRAME_H

#include "tx_isp_t32_txos.h"

#define T32_FC_NAME		700U
#define T32_FC_INDEX		720U
#define T32_FC_REGISTERED	40U
#define T32_FC_DYN_MINOR	255U
#define T32_ENOENT_FC		2U

/* provided by the driver glue (kernel) or by the oracle (host) */
extern u32 t32_tx_g_d9008;
void t32_tx_os_sprintf_u(char *dst, const char *fmt, u32 v);
int t32_tx_os_misc_register(u32 misc);

/* fixed minors of framechan0..8 in the fast start mode */
static const u8 t32_tx_fc_minor[9] = { 60, 59, 58, 44, 43, 42, 41, 40, 39 };

/* tx_isp_frame_chan_misc_register(fc) */
static inline int t32_tx_frame_chan_misc_register(u32 fc, u32 fops)
{
	char *name = (char *)T32_TX_P(fc + T32_FC_NAME), want[16];
	u32 minor = T32_FC_DYN_MINOR, k;

	t32_tx_os_sprintf_u(name, "framechan%d", t32_tx_ld(fc, T32_FC_INDEX));
	if (t32_tx_g_d9008)
		for (k = 0; k < 9; k++) {
			want[0] = 0;
			memcpy(want, "framechan", 9);
			want[9] = (char)('0' + k);
			want[10] = 0;
			if (!strcmp(name, want)) {
				minor = t32_tx_fc_minor[k];
				break;
			}
		}
	t32_tx_st(fc, 0, minor);
	t32_tx_st(fc, 8, fops);
	t32_tx_st(fc, 4, fc + T32_FC_NAME);
	if (t32_tx_os_misc_register(fc) < 0) {
		t32_tx_os_isp_printf(2, "Failed to register framechan%d!\n", t32_tx_ld(fc, T32_FC_INDEX));
		return -(int)T32_ENOENT_FC;
	}
	t32_tx_os_isp_printf(1, " Create %s OK! \n", name);
	t32_tx_st(fc, T32_FC_REGISTERED, 1);
	return 0;
}

#endif
