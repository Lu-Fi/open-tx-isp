/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) block "i9H": tisp_rdns_par_refresh and tisp_dpc_l_gain_update (module block state arrays
 * rdnsInfo[] / dpcInfo[], word indexed).  Binary oracle: tests/t33_binoracle/i9H.conf.
 */
#ifndef TX_ISP_T33_I9H_H
#define TX_ISP_T33_I9H_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_rdns.h"

#ifndef T33_I9H_GLUE
extern u32 *rdnsInfo[4];
extern u32 *dpcInfo[4];
extern u8 rdnsCfgMode;
extern u8 dpcCfgMode;
extern u8 tispRdnsRefresh;
extern u8 tispDpcRefresh;
extern void func_rdns_bin_para_update(u32 ch);
extern void func_rdns_para_update(u32 ch);
extern void tisp_dpc_bin_para_update(u32 ch);
extern void func_dpc_para_update(u32 ch);
extern void func_ctr_para_update(u32 ch);
extern void func_cac_para_update(u32 ch);
extern int tisp_rcfg_get_dma_state(u32 id);
extern void tisp_reg_write_t33(u32 ch, u32 id, u32 slot_or_addr, u32 val);
#endif

#define T33_I9H_GAIN	209	/* rdnsInfo word: last gain (byte 836), -1 = none */

/* rdns writes in vendor order: list slot (cfg mode) or register address */
struct t33_i9h_rw {
	u32 addr;
	u8 slot;
};

/* tisp_rdns_par_refresh */
static inline void t33_rdns_par_refresh_i9h(u32 ch, u32 gain, u32 thr)
{
	static const struct t33_i9h_rw t[] = {
		{ 0x260b4, 8 }, { 0x260b8, 9 }, { 0x260bc, 10 }, { 0x260c0, 11 }, { 0x260c4, 12 },
		{ 0x260c8, 13 }, { 0x26044, 4 },
		{ 0x26048, 15 }, { 0x26048, 16 }, { 0x26048, 17 }, { 0x26048, 18 }, { 0x26048, 19 },
		{ 0x26048, 20 }, { 0x26048, 21 }, { 0x26048, 22 },
		{ 0x26068, 24 }, { 0x26068, 25 }, { 0x26068, 26 }, { 0x26068, 27 }, { 0x26068, 28 },
		{ 0x26068, 29 }, { 0x26068, 30 }, { 0x26068, 31 },
		{ 0x26088, 33 }, { 0x26088, 34 }, { 0x26088, 35 }, { 0x26088, 36 }, { 0x26088, 37 },
		{ 0x26088, 38 }, { 0x26040, 3 }, { 0x260a0, 6 },
		{ 0x260a4, 40 }, { 0x260a4, 41 }, { 0x260a4, 42 }, { 0x260a4, 43 }, { 0x26020, 1 },
	};
	u32 *v = rdnsInfo[ch];
	u32 old = v[T33_I9H_GAIN], diff, ch8, l[48] = { 0 };
	const u8 *sec;
	unsigned int k;

	if (old != 0xffffffffU) {
		diff = gain < old ? old - gain : gain - old;
		if (diff < thr)
			return;
	}
	ch8 = ch & 0xff;
	v[T33_I9H_GAIN] = gain;
	func_rdns_bin_para_update(ch8);
	v = rdnsInfo[ch8];
	sec = (const u8 *)(unsigned long)v[208];
	t33_vb_evintp(v, sec, v[210], sec + 924, t33_rdns_evintp, ARRAY_SIZE(t33_rdns_evintp));
	t33_vb_intp(v, sec, v[T33_I9H_GAIN], t33_rdns_intp + 8, ARRAY_SIZE(t33_rdns_intp) - 8);
	func_rdns_para_update(ch8);
	if (tisp_rcfg_get_dma_state(3)) {
		tispRdnsRefresh = 2;
		return;
	}
	t33_rdns_store(v, l);
	for (k = 0; k < ARRAY_SIZE(t); k++)
		tisp_reg_write_t33(ch8, 3, rdnsCfgMode ? t[k].slot : t[k].addr, l[t[k].slot + 1]);
	tispRdnsRefresh = 1;
}

/* dpcInfo gain interpolations: { bits, section offset, word } */
static const struct t33_vmap t33_dpc_lg_intp[] = {
	{ 8, 14, 10 }, { 16, 32, 18 }, { 16, 54, 19 }, { 16, 76, 20 }, { 16, 98, 21 },
	{ 8, 120, 22 }, { 8, 131, 23 }, { 16, 142, 24 }, { 16, 164, 25 }, { 16, 186, 26 },
	{ 16, 208, 27 }, { 16, 230, 28 }, { 16, 252, 29 }, { 16, 274, 30 }, { 16, 296, 31 },
	{ 16, 318, 32 }, { 16, 340, 33 }, { 16, 362, 34 }, { 16, 384, 35 }, { 16, 406, 36 },
	{ 16, 428, 37 }, { 16, 452, 39 }, { 16, 476, 41 }, { 16, 500, 43 }, { 16, 526, 47 },
	{ 16, 548, 48 },
};

static inline void t33_i9h_dw(u32 ch, u32 slot, u32 addr, u32 val)
{
	tisp_reg_write_t33(ch, 2, dpcCfgMode ? slot : addr, val);
}

#define I9H_P12(h, l)	((((w)[h] & 0xfff) << 16) | ((w)[l] & 0xfff))

/* tisp_dpc_l_gain_update */
static inline void t33_dpc_l_gain_update(u32 ch, u32 gain)
{
	u32 ch8 = ch & 0xff, *w;
	const u8 *s;

	dpcInfo[ch][0xab] = gain;
	tisp_dpc_bin_para_update(ch8);
	w = dpcInfo[ch8];
	s = (const u8 *)(unsigned long)w[0xaa];
	t33_vb_intp(w, s, w[0xab], t33_dpc_lg_intp, ARRAY_SIZE(t33_dpc_lg_intp));
	func_dpc_para_update(ch8);
	func_ctr_para_update(ch8);
	func_cac_para_update(ch8);
	w = dpcInfo[ch8];
	t33_i9h_dw(ch8, 6, 8268, (w[81] & 1) | (w[82] & 1) << 1 | (w[83] & 1) << 2 | (w[84] & 1) << 3 |
		   (w[89] & 1) << 4 | (w[90] & 1) << 5 | (w[91] & 1) << 6 | (w[95] & 1) << 8 |
		   (w[96] & 1) << 9 | (w[99] & 1) << 12 | (w[85] & 1) << 16 | (w[86] & 1) << 17 |
		   (w[87] & 1) << 18 | (w[88] & 1) << 19 | (w[92] & 1) << 20 | (w[93] & 1) << 21 |
		   (w[94] & 1) << 22 | (w[97] & 1) << 24 | (w[98] & 1) << 25 | (w[100] & 1) << 28);
	t33_i9h_dw(ch8, 10, 8284, I9H_P12(108, 107));
	t33_i9h_dw(ch8, 11, 8288, ((w[110] << 8) & 0xffff) | (w[109] & 0xff));
	t33_i9h_dw(ch8, 36, 8292, I9H_P12(112, 111));
	t33_i9h_dw(ch8, 37, 8292, I9H_P12(114, 113));
	t33_i9h_dw(ch8, 39, 8300, I9H_P12(116, 115));
	t33_i9h_dw(ch8, 40, 8300, I9H_P12(118, 117));
	t33_i9h_dw(ch8, 13, 8308, I9H_P12(120, 119));
	t33_i9h_dw(ch8, 14, 8312, I9H_P12(123, 122));
	t33_i9h_dw(ch8, 15, 8316, ((w[125] << 8) & 0xffff) | (w[124] & 0xff));
	t33_i9h_dw(ch8, 42, 8320, I9H_P12(127, 126));
	t33_i9h_dw(ch8, 43, 8320, I9H_P12(129, 128));
	t33_i9h_dw(ch8, 45, 8328, I9H_P12(131, 130));
	t33_i9h_dw(ch8, 46, 8328, I9H_P12(133, 132));
	t33_i9h_dw(ch8, 17, 8336, I9H_P12(135, 134));
	t33_i9h_dw(ch8, 18, 8340, (w[141] & 0x1ff) << 16 | (w[140] & 1));
	t33_i9h_dw(ch8, 19, 8344, (w[144] & 3) << 8 | (w[143] & 0xfff) << 16 | (w[142] & 1));
	t33_i9h_dw(ch8, 21, 8352, I9H_P12(148, 147));
	t33_i9h_dw(ch8, 34, 8560, (w[137] & 0x1ff) << 16 | (w[136] & 3));
	t33_i9h_dw(ch8, 3, 8256, (w[74] & 1) << 8 | (w[75] & 1) << 12 | (w[72] & 1) | (w[138] & 0x7ff) << 16);
	t33_i9h_dw(ch8, 4, 8260, (w[76] & 0x1f) << 8 | (w[106] & 0x3f) << 16 | (w[73] & 0x1f) |
		   (w[121] & 0x3f) << 24);
	t33_i9h_dw(ch8, 5, 8264, (w[78] & 1) << 8 | (w[79] & 1) << 16 | (w[77] & 1) | (w[80] & 3) << 24);
	t33_i9h_dw(ch8, 8, 8276, (w[102] & 3) << 4 | (w[103] & 3) << 8 | (w[101] & 3));
	t33_i9h_dw(ch8, 9, 8280, (w[105] & 3) << 4 | (w[104] & 3));
	t33_i9h_dw(ch8, 18, 8340, (w[141] & 0x1ff) << 16 | (w[140] & 1));
	t33_i9h_dw(ch8, 19, 8344, (w[144] & 3) << 8 | (w[143] & 0xfff) << 16 | (w[142] & 1));
	t33_i9h_dw(ch8, 20, 8348, (w[146] & 3) << 8 | (w[149] & 0x3f) << 16 | (w[145] & 3));
	t33_i9h_dw(ch8, 22, 8356, (w[150] & 0xff) | w[153] << 24 | ((w[151] << 8) & 0xffff) |
		   (w[152] & 0xff) << 16);
	t33_i9h_dw(ch8, 24, 8372, (w[155] & 0x3f) << 8 | (w[156] & 0x3f) << 16 | (w[157] & 0x3f) << 24 |
		   (w[154] & 0x3f));
	t33_i9h_dw(ch8, 26, 8388, w[71] & 7);
	t33_i9h_dw(ch8, 27, 8392, (w[159] & 0x1ff) << 4 | (w[160] & 0x1ff) << 16 | (w[158] & 1));
	t33_i9h_dw(ch8, 28, 8396, (w[162] & 0x1ff) << 12 | (w[161] & 0x1ff));
	t33_i9h_dw(ch8, 30, 8416, (w[164] & 0x1ff) << 12 | (w[163] & 0x1ff));
	t33_i9h_dw(ch8, 31, 8420, (w[166] & 0x7ff) << 12 | (w[167] & 0x1f) << 24 | (w[165] & 0xfff));
	t33_i9h_dw(ch8, 32, 8424, (w[169] & 0xfff) << 8 | (w[168] & 0x1f));
	t33_i9h_dw(ch8, 34, 8560, (w[137] & 0x1ff) << 16 | (w[136] & 3));
	t33_i9h_dw(ch8, 1, 8208, 1);
	if (tisp_rcfg_get_dma_state(2))
		tispDpcRefresh = 2;
	else
		tispDpcRefresh = 1;
}

#undef I9H_P12

#endif /* TX_ISP_T33_I9H_H */
