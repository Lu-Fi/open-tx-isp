/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA / TOP / MDNS list glue (openimp #99), ports of tisp_msca_t33_frame_done_interrupt (system irq 33),
 * tisp_top_t33_pipe_null_down_interrupt (system irq 0, the frame end), tisp_mdns_t33_reg_trig and
 * tisp_get_rcfg_module.  Proven by tests/t33_binoracle block "fdmisc".
 *
 * The callees (tisp_rcfg_module_cfg_trig, tisp_top_reg_write, tisp_reg_write_t33) must be declared by the
 * includer; par = stIspInitPar (word 904: 1 = list refresh active).
 */
#ifndef TX_ISP_T33_FD_MISC_H
#define TX_ISP_T33_FD_MISC_H

#include "tx_isp_t33_blk.h"

#define T33_FDM_INITPAR_MODE	904
#define T33_FDM_MOD_MSCA	0x17
#define T33_FDM_MOD_TOP		0x21
#define T33_FDM_MOD_MDNS	0xf

/* tisp_msca_t33_frame_done_interrupt: test counter 0..10; refresh 1 starts list 23.  Returns 1. */
static inline int t33_msca_frame_done(u32 ch, const u8 *par, u8 *refresh, u32 *test_num)
{
	*test_num += 1;
	if (*test_num > 10)
		*test_num = 0;
	if (t33_le32(par + T33_FDM_INITPAR_MODE) != 1)
		return 1;
	if (*refresh == 1) {
		tisp_rcfg_module_cfg_trig(ch & 0xff, T33_FDM_MOD_MSCA);
		*refresh = 0;
	}
	return 1;
}

/* tisp_top_t33_pipe_null_down_interrupt: refresh 1 starts list 33, refresh 2 writes TOP again.  Returns 1. */
static inline int t33_top_pipe_null_down(u32 ch, const u8 *par, u8 *refresh)
{
	if (t33_le32(par + T33_FDM_INITPAR_MODE) != 1)
		return 1;
	if (*refresh == 1) {
		tisp_rcfg_module_cfg_trig(ch & 0xff, T33_FDM_MOD_TOP);
		*refresh = 0;
	} else if (*refresh == 2) {
		tisp_top_reg_write(ch & 0xff);
	}
	return 1;
}

/* tisp_mdns_t33_reg_trig: the MDNS start word (slot 1 / 0xd014) = 1; before mdnsInit the refresh flag */
static inline void t33_mdns_reg_trig(u32 ch, u8 cfg_mode, u8 init, u8 *refresh)
{
	tisp_reg_write_t33(ch & 0xff, T33_FDM_MOD_MDNS, cfg_mode ? 1 : 0xd014, 1);
	if (!init)
		*refresh = 1;
}

#endif /* TX_ISP_T33_FD_MISC_H */
