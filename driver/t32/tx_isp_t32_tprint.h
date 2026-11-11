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
 *    two parameter tables (t32_data_e35e0 / e35b0).
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

#endif
