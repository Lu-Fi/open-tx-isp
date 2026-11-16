/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r11H": block teardown functions with the calling convention of the vendor
 * functions (spec: r11H.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r11H.h"

u32 our_defog_info[4], our_defog_weight[4], our_gamma_para[4], our_lsc_para[4], our_ae_ctx[4];
u32 our_af_info[4], our_af_buf_info[4], our_awb_ct[4], our_awb_info[4], our_awb_stat[4];
u32 our_rcfg_buf_info;
u8 our_lcemodeflag[16], our_lcemodecounter[16], our_lce_frame_flag[4];

int bo_tisp_defog_deinit(u32 ch) { return t33_defog_free(our_defog_info, our_defog_weight, ch); }
int bo_tisp_gamma_deinit(u32 ch) { return t33_gamma_free(our_gamma_para, ch); }
void bo_tisp_lce_deinit(u32 ch) { t33_lce_free(our_lcemodeflag, our_lcemodecounter, our_lce_frame_flag, ch); }
void bo_tisp_lsc_deinit(u32 ch) { t33_lsc_free(our_lsc_para, ch); }
void bo_tisp_ae_deinit(u32 ch) { t33_ae_free(our_ae_ctx, ch); }
void bo_tisp_af_deinit(u32 ch) { t33_af_free(our_af_info, our_af_buf_info, ch); }
void bo_tisp_awb_deinit(u32 ch) { t33_awb_free(our_awb_ct, our_awb_info, our_awb_stat, ch); }
void bo_tisp_process_deinit(void) { t33_process_exit(); }
void bo_tisp_process_init(void) { t33_process_nop(); }
void bo_tisp_rcfg_deinit(void) { t33_rcfg_free(&our_rcfg_buf_info); }
void bo_tisp_top_deinit(u32 ch) { t33_top_release(ch); }
