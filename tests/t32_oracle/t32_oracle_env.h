/* SPDX-License-Identifier: GPL-2.0 */
/* Host oracle environment for Matt Davis' recovered T32 C (see env.c). */
#ifndef T32_ORACLE_ENV_H
#define T32_ORACLE_ENV_H

#include <stdint.h>

#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
typedef uint64_t u64;

#define T32O_MAX_WRITES	4096

struct t32o_log {
	unsigned int n, printfs;
	u32 irq;
	struct { u32 reg, val; } w[T32O_MAX_WRITES];
};
extern struct t32o_log t32o_log;

extern u32 t32_data_e28f0[4], t32_data_dd0a4[4], t32_data_dc274[4], t32_data_da844[0x100];
extern u8 t32_data_d03a0[4];
extern u8 t32_data_da850[704], t32_data_dd114[224], t32_data_dc1a4[56];
extern u32 t32_data_e1820[4];
extern u32 t32_data_e17c0[4], t32_data_e2450[4];
extern u8 t32_data_e1460[8], t32_data_dc284[1808];
extern u32 t32_data_e1464[4], t32_data_e1474[4], t32_data_e1484[4];

extern u32 t32o_arg5, t32o_arg6;
extern u32 (*t32o_rd_hook)(u32 reg);
typedef u64 (*t32o_fn)(u32, u32, u32, u32, u32);

void *t32o_alloc(u32 size);
void t32o_reset(void);
extern u8 t32o_ctbss[0x1270];
extern u32 t32o_ct_stop, t32o_ct_stack[208], t32o_ct_reg[28];
extern u32 t32o_complete_n, t32o_complete_arg, t32o_ev_n, t32o_ev_ch, t32o_ev_w[8];
#define T32O_EVLOG 16
extern u32 t32o_evlog[T32O_EVLOG][9];
extern u8 t32_data_e1690[4];
extern u32 t32o_pr_stop, t32o_pr_reg[28];
extern u32 t32o_lg_stop, t32o_lg_stack[116], t32o_lg_reg[28];
u64 t32o_call(t32o_fn fn, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

/* reference functions (ref/, o32 shape) */
u64 tisp_lce_init(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_e1da8[4];
extern u8 t32_data_e1db8[4 * 36];
u64 tisp_gib_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_dpc_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_lsc_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_lsc_ct_update(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_lsc_tgain_update(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_set_gain(u32 a0, u32 a1, u32 a2, u32 a3);
u64 Tiziano_Awb_Ct_Detect_GrayWorld(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 Tiziano_Awb_Ct_Cal(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult3_32(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_div_32(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ISPAWBInterpolation1(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6);
u64 ISPAWBInterpolation2(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6);
u64 func_zone_ct_weight(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);
u64 tisp_awb_spec_calculate(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_long_par_update(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_sat_weight(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6);
u64 tisp_msca_curve_calc(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_e2768[4], t32_data_e28ac[4];
extern u8 t32_data_e2898[16];
u64 tisp_mdns_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_bcsh_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_bcsh_ct_update(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_bcsh_ev_update(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);
u64 tisp_bcsh_irq_frame_down(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_bcsh_interp_by_ev(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 tisp_bcsh_interp_by_ct(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 tisp_bcsh_calc_para_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_bcsh_write_reg(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ysp_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_clm_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_gamma_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_gamma_ev_update(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_gamma_irq_frame_done(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_gamma_interp_by_ev(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 tisp_gamma_update_curve(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_gamma_strength_transform(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_e1834[4];
u64 tisp_dmsc_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ydns_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_cdns_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_sdns_init(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32o_reg88;
u64 tisp_csc_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_csccr_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_bin_data_parse(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_top_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ccm_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ccm_write_reg(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ccm_ct_update(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ccm_ev_update(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ccm_interp_by_ct(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ccm_interp_by_ev(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

#define T32O_HOST(v)	((void *)(uintptr_t)(v))
#define T32O_PTR(p)	((u32)(uintptr_t)(p))

#endif
