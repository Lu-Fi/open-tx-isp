/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "hvflip" (openimp #669): the 0x73 case (mirror / flip) of tx_isp_core_ops_s_ctrl
 * with the vendor calling convention, the T32 dispatcher built with the T33 layout (TX_ISP_T33).  The callees
 * are extern (vendor names, recorded by the oracle).
 */
#define TX_ISP_T33
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_txsctrl.h"

u32 t32_tx_link_mode;		/* vendor direct_mode */

extern u32 private_copy_from_user(void *dst, u32 src, u32 n);
extern int isp_printf(u32 level, const char *fmt, ...);
extern int tisp_g_hv_flip(u32 ch, void *flags);
extern int tisp_s_hv_flip(u32 ch, void *flags);
extern int tisp_lsc_hvflip(u32 ch, u32 a, u32 b, u32 h, u32 v);

u32 t32_tx_os_copy_from_user(void *dst, u32 user, u32 n) { return private_copy_from_user(dst, user, n); }
void t32_tx_os_isp_printf(u32 level, const char *fmt, ...) { isp_printf(level, fmt); }
int t32_tx_os_tisp_g_hv_flip(u32 ch, void *flags) { return tisp_g_hv_flip(ch, flags); }
int t32_tx_os_tisp_s_hv_flip(u32 ch, void *flags) { return tisp_s_hv_flip(ch, flags); }
int t32_tx_os_tisp_lsc_hvflip(u32 ch, u32 a, u32 b, u32 h, u32 v) { return tisp_lsc_hvflip(ch, a, b, h, v); }

int bo_tx_isp_core_ops_s_ctrl(u32 ch, u32 isp, u32 ctrl)
{
	return t32_tx_core_ops_s_ctrl(ch, isp, ctrl);
}
