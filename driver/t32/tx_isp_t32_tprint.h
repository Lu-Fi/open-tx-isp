/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32: the vendor's tisp debug dumps of tx-isp-t32.o (Matt Davis' recovered
 * C, PR #15) as readable C: printk / isp_printf of tuning structures and
 * block registers.  The format strings and the order of their arguments are
 * the vendor's (.rodata.str), the structures are read at the vendor's
 * offsets.
 *
 * Checked by tests/t32_oracle (t32_tpr1_oracle ...): the full print stream
 * (isp_printf level / printk and the rendered text), register reads, the
 * state the vendor writes and the result, against the recovered C on random
 * states.
 *
 * Vendor behaviour kept on purpose, named here and not fixed:
 *  - t32_tmo_adr_reg_params_print uses the module global t32_data_e3514 (u16)
 *    as its loop counter: it is left at the last array length (4);
 *  - t32_tmo_adr_reg_params_print does not check the channel index of the
 *    two parameter tables (t32_data_e35e0 / e35b0);
 *  - tisp_params_print prints its labels as printk FORMATS (part 1) and
 *    isp_printf formats (part 2); the vendor's detect dump ends in a tail
 *    call of print_array_awb_32 (its result is the last printk's, no caller
 *    uses it): t32_tisp_params_print_detect returns nothing;
 *  - the tisp_* dumps take the channel index unchecked.
 */
#ifndef TX_ISP_T32_TPRINT_H
#define TX_ISP_T32_TPRINT_H

#include "tx_isp_t32_glue.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#define T32_SHOW_PRINTK		printk
#endif
/* host oracle: T32_SHOW_PRINTK and isp_printf come from tests/t32_oracle/t32_show_rec.h */

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

int isp_printf(unsigned int level, const char *fmt, ...);

/* ---- tisp_mdns_reg_print (0x6a6c8): three MDNS interpolation registers by field ---- */
/* reg_read = tisp_reg_read(ch, reg); with the register shadow (t32_data_d03a0 != 0) the shadow index */
static inline int t32_tisp_mdns_reg_print(u32 (*reg_read)(u32 ch, u32 reg), const u8 *shadow, u32 ch)
{
	static const char *const qp[5] = {
		"mpit_tmp->mdns_yqpvalue0_intp = %5d\n",
		"mpit_tmp->mdns_yqpvalue1_intp = %5d\n",
		"mpit_tmp->mdns_yqpvalue2_intp = %5d\n",
		"mpit_tmp->mdns_yqpvalue3_intp = %5d\n",
		"mpit_tmp->mdns_ycmpqpen_intp = %5d\n",
	};
	static const u32 qreg[2][2] = { { 0xd374, 1118 }, { 0xd378, 1119 } };	/* { direct, shadow index } */
	u32 v, r, i;
	int ret = 0;

	v = reg_read(ch, *shadow ? 1017 : 0xd14c);
	T32_SHOW_PRINTK("mpit_tmp->mdns_yrefweimin0_intp = %5d\n", v & 0xff);
	T32_SHOW_PRINTK("mpit_tmp->mdns_yrefweimax0_intp = %5d\n", (v >> 8) & 0xff);
	T32_SHOW_PRINTK("mpit_tmp->mdns_yrefmeanfuswei_intp = %5d\n", (v >> 16) & 0xff);
	for (r = 0; r < 2; r++) {
		v = reg_read(ch, qreg[r][*shadow ? 1 : 0]);
		for (i = 0; i < 4; i++)
			T32_SHOW_PRINTK(qp[i], (v >> (8 * i)) & 0x1f);
		ret = T32_SHOW_PRINTK(qp[4], v >> 31);
	}
	return ret;
}

/* ---- tmo_adr_reg_params_print (0x9d2b8): the ADR tables of the TMO parameters ---- */
enum { T32_TPR_S16, T32_TPR_S8, T32_TPR_U8 };
static const struct {
	u16 line, n, off;
	u8 asit, type;		/* asit: the second table (t32_data_e35b0), else the first (e35e0) */
	const char *name;
} t32_tmo_adr_tab[] __maybe_unused = {
	{ 6107,  16,   344, 0, T32_TPR_S16, "ttp_tmp->adr_ctc_kneepoint_y" },
	{ 6108,  21,   376, 0, T32_TPR_S16, "ttp_tmp->adr_ctc1_y" },
	{ 6109,   4,   636, 0, T32_TPR_S8,  "ttp_tmp->adr_modules_en" },
	{ 6110,  32,   640, 0, T32_TPR_S8,  "ttp_tmp->adr_weight_lut_20" },
	{ 6111,  32,   672, 0, T32_TPR_S8,  "ttp_tmp->adr_weight_lut_02" },
	{ 6112,  32,   704, 0, T32_TPR_S8,  "ttp_tmp->adr_weight_lut_12" },
	{ 6113,  32,   736, 0, T32_TPR_S8,  "ttp_tmp->adr_weight_lut_22" },
	{ 6114,  32,   768, 0, T32_TPR_S8,  "ttp_tmp->adr_weight_lut_21" },
	{ 6115,  31,   574, 0, T32_TPR_S16, "ttp_tmp->adr_centre_w_distance" },
	{ 6116,   5,   320, 0, T32_TPR_S16, "ttp_tmp->adr_block_coord_m" },
	{ 6117,   7,   330, 0, T32_TPR_S16, "ttp_tmp->adr_block_coord_n" },
	{ 6118,   4,  9952, 0, T32_TPR_S16, "ttp_tmp->adr_other_pars" },
	{ 6119,  16,    68, 1, T32_TPR_S16, "asit_tmp->ctc_kneepoint_y" },
	{ 6120,  14,   100, 1, T32_TPR_S16, "asit_tmp->adr_min_kneepoint_y" },
	{ 6121, 336,   128, 1, T32_TPR_S16, "asit_tmp->adr_map_kneepoint_y" },
	{ 6122,   4, 10042, 0, T32_TPR_U8,  "ttp_tmp->tmo_mode_para" },
};

/* ttp / asit = t32_data_e35e0 / e35b0 (per channel a pointer to the parameter pointer); idx = t32_data_e3514 */
static inline int t32_tmo_adr_reg_params_print(const u32 *ttp, const u32 *asit, u32 ch, u16 *idx)
{
	const u8 *base[2];
	u32 t;

	base[0] = T32_PTR(t32_g_rd32(T32_PTR(ttp[ch]), 0));
	base[1] = T32_PTR(t32_g_rd32(T32_PTR(asit[ch]), 0));
	for (t = 0; t < sizeof(t32_tmo_adr_tab) / sizeof(t32_tmo_adr_tab[0]); t++) {
		const u8 *a = base[t32_tmo_adr_tab[t].asit] + t32_tmo_adr_tab[t].off;
		int v;

		isp_printf(1, "\n < (%23s %5d)%18s[%2d] > : \n        ", "tmo_adr_reg_params_print",
			   t32_tmo_adr_tab[t].line, t32_tmo_adr_tab[t].name, t32_tmo_adr_tab[t].n);
		for (*idx = 0; *idx < t32_tmo_adr_tab[t].n; (*idx)++) {
			if (t32_tmo_adr_tab[t].type == T32_TPR_S16)
				v = (int16_t)t32_g_rd16(a, 2U * *idx);
			else if (t32_tmo_adr_tab[t].type == T32_TPR_S8)
				v = (int8_t)a[*idx];
			else
				v = a[*idx];
			isp_printf(1, " %7d", v);
		}
		if (t + 1 == sizeof(t32_tmo_adr_tab) / sizeof(t32_tmo_adr_tab[0]))
			return isp_printf(1, "\n");
		isp_printf(1, "\n");
	}
	return 0;
}

/* ---- tisp_params_print (0x3a6a4) / tisp_params_print_detect (0x3b1a8): the AWB parameters of a channel ---- */
/* the array printers of tx_isp_t32_dbg.h (print_array_awb_8 / 16 / 32: " %d," per element, "]\n") */
#ifndef T32_DBG_PRINTK
#define T32_DBG_PRINTK		T32_SHOW_PRINTK
#endif
#include "tx_isp_t32_dbg.h"

struct t32_tpr_arr { const char *label; u8 bits; u16 off, n; };
struct t32_tpr_val { const char *label; u8 bits; u32 off; };

static inline void t32_tpr_array(const u8 *a, u32 bits, u32 n)
{
	if (bits == 32)
		t32_print_array_awb_32((const u32 *)a, (int)n);
	else if (bits == 16)
		t32_print_array_awb_16((const u16 *)a, (int)n);
	else
		t32_print_array_awb_8(a, (int)n);
}

/* tisp_params_print, part 1: printk(label) and the array at the first parameter block + off */
static const struct t32_tpr_arr t32_awb_par_arr[] __maybe_unused = {
	{ "awb_zone_para            :",  16,  3184,   59 },
	{ "awb_pixel_cnt_th         :",  32,     4,    1 },
	{ "awb_lowlight_rg_th       :",  16,  3302,    2 },
	{ "au16PointPos             :",  16,  3306,    2 },
	{ "awb_cof                  :",  16,  3310,    6 },
	{ "awb_mf_para              :",  32,     8,    6 },
	{ "awb_scene_mode           :",  32,    32,    3 },
	{ "awb_ct                   :",  32,    44,    1 },
	{ "awb_ct_last              :",  32,    48,    1 },
	{ "awb_ref_ct_gain          :",  32,    52,    2 },
	{ "awb_extra_light_add      :",  16,  3322,   20 },
	{ "awb_extra_light_rem      :",  16,  3362,   20 },
	{ "awb_extra_light_num      :",   8,  5702,    2 },
	{ "awb_rg_pos               :",  32,    60,   15 },
	{ "awb_bg_pos               :",  32,   120,   15 },
	{ "awb_scene_ct_ot_h        :",  32,   180,    4 },
	{ "awb_scene_ct_ot_l        :",  32,   196,    4 },
	{ "awb_scene_ct_in          :",  32,   212,    4 },
	{ "awb_scene_wgt_ot         :",  32,   228,    2 },
	{ "awb_scene_wgt_in         :",  32,   236,    2 },
	{ "awb_dist_w               :",  32,   244,    3 },
	{ "awb_ev_list              :",  32,   256,    9 },
	{ "awb_y_wgt_adjust_list    :",  16,  3402,   10 },
	{ "awb_stat_y_wgt_para      :",  16,  3422,    8 },
	{ "awb_stat_zone_sat_para   :",  16,  3438,    5 },
	{ "awb_cluster_para         :",  32,   292,    8 },
	{ "awb_gw_para              :",  16,  3448,    6 },
	{ "awb_spec_point_rem_en    :",   8,  5704,   10 },
	{ "awb_spec_point_rem_th    :",  32,   324,   10 },
	{ "awb_spec_point_rem_rg    :",  16,  3460,   10 },
	{ "awb_spec_point_rem_bg    :",  16,  3480,   10 },
	{ "awb_rgbg_wgt             :",  32,   364,  225 },
	{ "awb_rgbg_wgt_ot          :",  32,  1264,  225 },
	{ "awb_ct_mesh              :",  32,  2164,  225 },
	{ "awb_ls_wgt_lut           :",  16,  3500,  514 },
	{ "awb_zone_wgt             :",   8,  5714, 1024 },
	{ "awb_res0                 :",  32,  3064,   10 },
	{ "awb_res1                 :",  32,  3104,   10 },
	{ "awb_res2                 :",  32,  3144,   10 },
};
/* part 2: isp_printf(1, label) and isp_printf(1, " %u\n", value) of the second block (zone statistics) + off */
static const struct t32_tpr_val t32_awb_par_val[] __maybe_unused = {
	{ "awb_zone_rgbg            :", 32,      0 },
	{ "awb_zone_pix_cnt         :", 32,  16384 },
	{ "awb_zone_sat_w           :", 32,  24576 },
	{ "awb_scene_wgt_ot_tra     :", 32,  28672 },
	{ "awb_scene_wgt_in_tra     :", 32,  28680 },
	{ "awb_extra_light_num_tra  :",  8,  28688 },
	{ "awb_sensor_inform         :", 16,  28690 },
	{ "awb_spec_face_para           :",  8,  28706 },
	{ "awb_spec_point_rem_en_inter   :",  8,  28708 },
	{ "awb_spec_point_rem_rg_stat :", 16,  28718 },
	{ "awb_spec_point_rem_bg_stat :", 16,  28738 },
	{ "awb_sel_para                :",  8,  28758 },
	{ "awb_ct_lsat_offset         :", 32,  28760 },
	{ "awb_ls_wgt_lut_inter        :", 16,  28764 },
	{ "awb_offset           :", 32,  29792 },
	{ "r_sum_global           :", 32,  38056 },
	{ "g_sum_global           :", 32,  42152 },
	{ "b_sum_global           :", 32,  46248 },
	{ "ir_sum_global          :", 32,  50344 },
	{ "pix_cnt_global         :", 32,  54440 },
	{ "r_sum_planck           :", 32,  58536 },
	{ "g_sum_planck           :", 32,  62632 },
	{ "b_sum_planck           :", 32,  66728 },
	{ "ir_sum_planck          :", 32,  70824 },
	{ "pix_cnt_planck         :", 32,  74920 },
	{ "au32SpecStat           :", 32,  79016 },
	{ "vu8FlagFirstFrame      :",  8,  79036 },
	{ "vu8FlagDNRefresh       :",  8,  79037 },
	{ "vu32FlagFrameCnt       :", 32,  79040 },
	{ "vu8FlagStable          :",  8,  79044 },
	{ "au32Irg                :", 32,  79048 },
	{ "au32Ibg                :", 32,  79108 },
	{ "au32Ict                :", 32,  79168 },
	{ "au16ClusterRgBgIndexNum:", 16,  80796 },
	{ "au32ClusterRgBgIndexMax:", 32,  81188 },
	{ "au32ClusterRgBgValue1  :", 32,  81332 },
	{ "au32ClusterRgBgValue2  :", 32,  82052 },
	{ "au32ClusterRgBgWeight  :", 32,  83012 },
	{ "au32RgBgWgt            :", 32,  87108 },
	{ "au32RgBgDis            :", 32,  91204 },
	{ "au32RgBgDwgt           :", 32,  95300 },
	{ "au32ZonePixWgt         :", 32,  99396 },
	{ "vu32Ev                 :", 32, 107588 },
	{ "vu32Tgain              :", 32, 107592 },
	{ "vu8FlagTrig            :",  8, 107596 },
	{ "vu8FlagFreez           :",  8, 107597 },
	{ "vu8ManualMode          :",  8, 107598 },
	{ "vu32CtManual           :", 32, 107600 },
	{ "au16ManualRgBg         :", 16, 107604 },
	{ "au16GlobalRgBg         :", 16, 107608 },
	{ "au16WeightRgBg         :", 16, 107620 },
	{ "au16ZoneEveR           :", 32, 107628 },
	{ "au16ZoneEveG           :", 32, 111724 },
	{ "au16ZoneEveB           :", 32, 115820 },
};

/* par = t32_data_e16b4 (per channel: { first block, second block }); result: the last isp_printf's */
static inline int t32_tisp_params_print(const u32 *par, u32 ch)
{
	const u8 *blk = T32_PTR(par[ch]);
	const u8 *p = T32_PTR(t32_g_rd32(blk, 0)), *q = T32_PTR(t32_g_rd32(blk, 4));
	u32 i, v;
	int ret = 0;

	for (i = 0; i < sizeof(t32_awb_par_arr) / sizeof(t32_awb_par_arr[0]); i++) {
		T32_SHOW_PRINTK(t32_awb_par_arr[i].label);
		t32_tpr_array(p + t32_awb_par_arr[i].off, t32_awb_par_arr[i].bits, t32_awb_par_arr[i].n);
	}
	for (i = 0; i < sizeof(t32_awb_par_val) / sizeof(t32_awb_par_val[0]); i++) {
		const u8 *a = q + t32_awb_par_val[i].off;

		isp_printf(1, t32_awb_par_val[i].label);
		v = t32_awb_par_val[i].bits == 32 ? t32_g_rd32(a, 0) : t32_awb_par_val[i].bits == 16 ? t32_g_rd16(a, 0) : *a;
		ret = isp_printf(1, " %u\n", v);
	}
	return ret;
}

/* tisp_params_print_detect: isp_printf(1, label) and the array the detect block's pointer at off points to */
static const struct t32_tpr_arr t32_awb_det_arr[] __maybe_unused = {
	{ "au32RgBgWeight     = [", 32,  16, 1024 },
	{ "au32SceneCt        = [", 32,   8,    4 },
	{ "au32SceneWgt       = [", 32,  12,    2 },
	{ "au16ExtraLightadd  = [", 16,   0,   20 },
	{ "au8ExtraLightNum   = [",  8,   4,   20 },
	{ "au16ZoneParm       = [", 16,  20,   55 },
	{ "au32RgPos          = [", 32,  24,   15 },
	{ "au32BgPos          = [", 32,  28,   15 },
	{ "au32CtMesh         = [", 32,  32,  225 },
	{ "au8ZoneWeight      = [",  8,  36,  225 },
	{ "au32DisTw          = [", 32,  40,    3 },
	{ "vu32Ct             = [", 32,  44,    1 },
	{ "au16LsWgtLut       = [", 16,  48,  514 },
	{ "au16PointPos       = [", 16,  52,    2 },
	{ "au16GrayWorldPar   = [", 16,  56,    6 },
	{ "au16StatZoneSatPar = [", 16,  60,    5 },
	{ "au32ClusterPar     = [", 32,  64,    8 },
	{ "au16ExtraLightRem  = [", 16,  68,   20 },
	{ "au8SpecPointRemEn  = [",  8,  72,   10 },
	{ "au32SpecPointRemTh = [", 32,  76,   10 },
	{ "au16SpecPointRemRg = [", 16,  80,   10 },
	{ "au16SpecPointRemBg = [", 16,  84,   10 },
	{ "au32ZoneRgBg       = [", 32, 132, 4096 },
	{ "au32ZonePixCnt     = [", 32, 136, 2048 },
	{ "au32ZoneSatW       = [", 32, 140, 1024 },
};

/* det = t32_data_e16a4 (per channel the detect parameter block) */
static inline void t32_tisp_params_print_detect(const u32 *det, u32 ch)
{
	u32 i;

	for (i = 0; i < sizeof(t32_awb_det_arr) / sizeof(t32_awb_det_arr[0]); i++) {
		isp_printf(1, t32_awb_det_arr[i].label);
		t32_tpr_array(T32_PTR(t32_g_rd32(T32_PTR(det[ch]), t32_awb_det_arr[i].off)), t32_awb_det_arr[i].bits,
			      t32_awb_det_arr[i].n);
	}
}

#endif
