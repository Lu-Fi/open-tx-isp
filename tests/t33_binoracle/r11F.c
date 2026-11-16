/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "r11F": MSCA state helpers, library refresh, interrupt masks. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r11F.h"

extern u32 system_reg_read(u32 reg);
extern u32 system_reg_write(u32 reg, u32 val);
extern u32 tisp_bin_data_parse(u32 ch, u32 size, u32 addr);
extern void tisp_top_switch_para(u32 ch);
extern void tisp_dpc_switch_para(u32 ch);
extern void tisp_blc_switch_para(u32 ch);
extern void tisp_ae_switch_para(u32 ch);
extern void tisp_awb_dn_params_refresh(u32 ch, u32 mode);
extern void tisp_lsc_switch_para(u32 ch);
extern void tisp_tmo_switch_para(u32 ch);
extern void tisp_dmsc_switch_para(u32 ch);
extern void tisp_gamma_switch_para(u32 ch);
extern void tisp_ccm_switch_para(u32 ch);
extern void tisp_clm_switch_para(u32 ch);
extern void tisp_bcsh_switch_para(u32 ch);
extern void tisp_ysp_switch_para(u32 ch);
extern void tisp_mdns_switch_para(u32 ch);
extern void tisp_ydns_switch_para(u32 ch);
extern void tisp_sdns_switch_para(u32 ch);
extern void tisp_cdns_switch_para(u32 ch);
extern void tisp_defog_switch_para(u32 ch, u32 a, u32 b);
extern void tisp_lce_switch_para(u32 ch);
extern void tisp_rdns_switch_para(u32 ch);
extern void tisp_af_switch_para(u32 ch);
extern void tisp_csccr_switch_para(u32 ch);
extern void __wake_up(void *q, u32 mode, u32 nr, void *key);

u8 our_binInfo[80];
u8 *our_tparamsP[4];
u8 our_ispInitFinish[4];
u8 our_stIspInitPar[4032];
u8 our_tispPollValue[4];
u8 our_dumpQueue[8];

static const struct t33_r11f_ops ops = {
	.reg_read = system_reg_read,
	.reg_write = system_reg_write,
	.bin_parse = tisp_bin_data_parse,
	.pre = { tisp_top_switch_para, tisp_dpc_switch_para, tisp_blc_switch_para, tisp_ae_switch_para },
	.awb = tisp_awb_dn_params_refresh,
	.post = { tisp_lsc_switch_para, tisp_tmo_switch_para, tisp_dmsc_switch_para, tisp_gamma_switch_para,
		  tisp_ccm_switch_para, tisp_clm_switch_para, tisp_bcsh_switch_para, tisp_ysp_switch_para,
		  tisp_mdns_switch_para, tisp_ydns_switch_para, tisp_sdns_switch_para, tisp_cdns_switch_para },
	.defog = tisp_defog_switch_para,
	.tail = { tisp_lce_switch_para, tisp_rdns_switch_para, tisp_af_switch_para, tisp_csccr_switch_para },
	.wake = __wake_up,
};

u32 bo_tisp_msca_state(void) { return t33_msca_state(); }
u32 bo_tisp_msca_work_state(u32 a, u32 n) { (void)a; return t33_msca_work_state(&ops, n); }
void bo_tisp_msca_Shd_ctrl(u32 a) { (void)a; t33_msca_shd_ctrl(); }
u32 bo_tisp_msca_addr_fifo_clear_byindex(u32 ch) { return t33_msca_fifo_clear(&ops, ch); }
u32 bo_lib_tisp_refresh_para(u8 *req)
{
	return t33_lib_refresh_para(&ops, req, our_binInfo, our_tparamsP, our_ispInitFinish, our_stIspInitPar,
				    our_tispPollValue, our_dumpQueue);
}
u32 bo_lib_tisp_intp_enable(void) { return t33_lib_intp_enable(&ops); }
u32 bo_lib_tisp_intp_disable(void) { return t33_lib_intp_disable(&ops); }
