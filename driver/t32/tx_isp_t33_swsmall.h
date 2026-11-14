/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) day/night IQ-bin section switch of the SDNS, YDNS and DPC
 * blocks (vendor tisp_sdns_switch_para / tisp_ydns_switch_para /
 * tisp_dpc_switch_para).  Each one points the block's info at the new bin
 * section, interpolates the gain tables into the info words (the vendor's
 * tisp_simple_intp_uint8_t / _uint16_t, index = signed gain >> 16), lets the
 * block's para_update run and writes the register list (slot numbers when
 * <blk>CfgMode is set, register addresses otherwise).
 */
#ifndef TX_ISP_T33_SWSMALL_H
#define TX_ISP_T33_SWSMALL_H

#include "tx_isp_t33_blk.h"

/*
 * The vendor names the port uses.  The driver (tx_isp_t33_swglue.h) maps them
 * onto its block state and defines T33_SW_GLUE; the binary oracle
 * (tests/t33_binoracle/sw-small.c) defines them itself.
 */
#ifndef T33_SW_GLUE
extern void tisp_reg_write_t33(u32 id, u32 list, u32 slot, u32 val);
extern int tisp_rcfg_get_dma_state(u32 id);
extern u32 tisp_simple_intp_uint8_t(int idx, u32 frac, const u8 *tab);
extern u32 tisp_simple_intp_uint16_t(int idx, u32 frac, const u8 *tab);

extern u8 sdnsCfgMode, ydnsCfgMode, dpcCfgMode;
extern u8 tispSdnsRefresh, tispYdnsRefresh, tispDpcRefresh;
extern u32 *sdnsInfo[], *ydnsInfo[], *dpcInfo[];
extern u32 *tparamsP[];

extern void tisp_sdns_other_params_transform(u32 ch);
extern void func_sdns_para_update(u32 ch);
extern void func_ydns_bin_para_update(u32 ch);
extern void func_ydns_para_update(u32 ch);
extern void tisp_dpc_bin_para_update(u32 ch);
extern void func_dpc_para_update(u32 ch);
extern void func_ctr_para_update(u32 ch);
extern void func_cac_para_update(u32 ch);
#endif

#define T33_I8(o)	tisp_simple_intp_uint8_t(i, f, p + (o))
#define T33_I16(o)	tisp_simple_intp_uint16_t(i, f, p + (o))

static inline u32 t33_sw_slot(u8 cfg, u32 cfg_slot, u32 addr)
{
	return cfg ? cfg_slot : addr;
}

/* four bytes of four words: a | b << 8 | c << 16 | d << 24 */
static inline u32 t33_sw_b4(u32 a, u32 b, u32 c, u32 d)
{
	return (a & 0xff) | (b & 0xff) << 8 | (c & 0xff) << 16 | d << 24;
}

/* two masked fields: lo | hi << 16 */
static inline u32 t33_sw_pair(u32 lo, u32 hi, u32 m)
{
	return (lo & m) | (hi & m) << 16;
}

/* tisp_sdns_switch_para */
static inline int t33_sdns_switch(u32 ch)
{
	u32 c = ch & 0xff, *inf, f, v;
	const u8 *p;
	int i;

	inf = sdnsInfo[ch];
	inf[36] = (u32)(unsigned long)tparamsP[ch] + 0x9b20;
	tisp_sdns_other_params_transform(c);
	inf = sdnsInfo[c];
	p = (const u8 *)(unsigned long)inf[36];
	i = (int)inf[37] >> 16;
	f = inf[37] & 0xffff;
	inf[1] = T33_I8(5);
	inf[2] = T33_I8(16);
	inf[4] = T33_I8(28);
	inf[5] = T33_I8(39);
	inf[6] = T33_I8(50);
	inf[7] = T33_I8(61);
	inf[8] = T33_I8(72);
	inf[9] = T33_I8(83);
	inf[10] = T33_I8(94);
	inf[11] = T33_I8(105);
	inf[13] = T33_I8(117);
	inf[14] = T33_I8(128);
	inf[16] = T33_I8(140);
	func_sdns_para_update(c);
	if (tisp_rcfg_get_dma_state(20)) {
		tispSdnsRefresh = 2;
		return 2;
	}
	inf = sdnsInfo[c];
	v = t33_sw_b4(inf[19], inf[20], inf[21], inf[22]);
	tisp_reg_write_t33(c, 20, t33_sw_slot(sdnsCfgMode, 5, 0x13028), v);
	v = t33_sw_b4(inf[23], inf[24], inf[25], inf[26]);
	tisp_reg_write_t33(c, 20, t33_sw_slot(sdnsCfgMode, 6, 0x1302c), v);
	v = (inf[27] & 0x3f) | (inf[28] & 0xff) << 8 | (inf[29] & 0xff) << 16 | (inf[30] & 0x3f) << 24;
	tisp_reg_write_t33(c, 20, t33_sw_slot(sdnsCfgMode, 7, 0x13030), v);
	tisp_reg_write_t33(c, 20, t33_sw_slot(sdnsCfgMode, 8, 0x13034), inf[31] & 0x1f);
	v = t33_sw_b4(inf[32], inf[33], inf[34], inf[35]);
	tisp_reg_write_t33(c, 20, t33_sw_slot(sdnsCfgMode, 9, 0x13038), v);
	inf = sdnsInfo[c];
	tisp_reg_write_t33(c, 20, t33_sw_slot(sdnsCfgMode, 3, 0x13020), inf[17] & 3);
	tisp_reg_write_t33(c, 20, t33_sw_slot(sdnsCfgMode, 4, 0x13024), inf[18] & 3);
	v = (inf[27] & 0x3f) | (inf[28] & 0xff) << 8 | (inf[29] & 0xff) << 16 | (inf[30] & 0x3f) << 24;
	tisp_reg_write_t33(c, 20, t33_sw_slot(sdnsCfgMode, 7, 0x13030), v);
	tisp_reg_write_t33(c, 20, t33_sw_slot(sdnsCfgMode, 1, 0x13010), 1);
	tispSdnsRefresh = 1;
	return 1;
}

/* tisp_ydns_switch_para (no defined return value) */
static inline void t33_ydns_switch(u32 ch)
{
	u32 c = ch & 0xff, *inf, gain, f, v;
	const u8 *p;
	int i, k;

	inf = ydnsInfo[c];
	inf[24] = (u32)(unsigned long)tparamsP[c] + 0x88ac;
	gain = inf[26];
	func_ydns_bin_para_update(c);
	inf = ydnsInfo[c];
	p = (const u8 *)(unsigned long)inf[24];
	i = (int)gain >> 16;
	f = gain & 0xffff;
	inf[1] = T33_I8(5);
	inf[2] = T33_I8(16);
	inf[3] = T33_I8(27);
	inf[4] = T33_I8(38);
	inf[5] = T33_I8(49);
	inf[6] = T33_I8(60);
	inf[7] = T33_I8(71);
	inf[8] = T33_I8(82);
	inf[9] = T33_I8(93);
	inf[10] = T33_I8(104);
	inf[11] = T33_I8(115);
	func_ydns_para_update(c);
	if (tisp_rcfg_get_dma_state(17)) {
		tispYdnsRefresh = 2;
		return;
	}
	inf = ydnsInfo[c];
	v = (inf[14] & 3) << 8 | (inf[15] & 3) << 12 | (inf[13] & 0xff);
	for (k = 0; k < 3; k++)		/* the vendor writes this word three times */
		tisp_reg_write_t33(c, 17, t33_sw_slot(ydnsCfgMode, 4, 0xe054), v);
	v = t33_sw_b4(inf[16], inf[17], inf[18], inf[19]);
	tisp_reg_write_t33(c, 17, t33_sw_slot(ydnsCfgMode, 5, 0xe058), v);
	v = t33_sw_b4(inf[20], inf[21], inf[22], inf[23]);
	tisp_reg_write_t33(c, 17, t33_sw_slot(ydnsCfgMode, 6, 0xe05c), v);
	tisp_reg_write_t33(c, 17, t33_sw_slot(ydnsCfgMode, 3, 0xe050), inf[12] & 1);
	tisp_reg_write_t33(c, 17, t33_sw_slot(ydnsCfgMode, 1, 0xe010), 1);
	tispYdnsRefresh = 1;
}

/* tisp_dpc_switch_para */
static inline void t33_dpc_switch(u32 ch)
{
	u32 c = ch & 0xff, *inf, f, v;
	const u8 *p;
	int i;
	u8 m;

	tisp_dpc_bin_para_update(c);
	inf = dpcInfo[c];
	p = (const u8 *)(unsigned long)inf[170];
	i = (int)inf[171] >> 16;
	f = inf[171] & 0xffff;
	inf[10] = T33_I8(14);
	inf[18] = T33_I16(32);
	inf[19] = T33_I16(54);
	inf[20] = T33_I16(76);
	inf[21] = T33_I16(98);
	inf[22] = T33_I8(120);
	inf[23] = T33_I8(131);
	inf[24] = T33_I16(142);
	inf[25] = T33_I16(164);
	inf[26] = T33_I16(186);
	inf[27] = T33_I16(208);
	inf[28] = T33_I16(230);
	inf[29] = T33_I16(252);
	inf[30] = T33_I16(274);
	inf[31] = T33_I16(296);
	inf[32] = T33_I16(318);
	inf[33] = T33_I16(340);
	inf[34] = T33_I16(362);
	inf[35] = T33_I16(384);
	inf[36] = T33_I16(406);
	inf[37] = T33_I16(428);
	inf[39] = T33_I16(452);
	inf[41] = T33_I16(476);
	inf[43] = T33_I16(500);
	inf[47] = T33_I16(526);
	inf[48] = T33_I16(548);
	func_dpc_para_update(c);
	func_ctr_para_update(c);
	func_cac_para_update(c);
	inf = dpcInfo[c];
	m = dpcCfgMode;
	v = (inf[81] & 1) | (inf[82] & 1) << 1 | (inf[83] & 1) << 2 | (inf[84] & 1) << 3 |
	    (inf[89] & 1) << 4 | (inf[90] & 1) << 5 | (inf[91] & 1) << 6 | (inf[95] & 1) << 8 |
	    (inf[96] & 1) << 9 | (inf[99] & 1) << 12 | (inf[85] & 1) << 16 | (inf[86] & 1) << 17 |
	    (inf[87] & 1) << 18 | (inf[88] & 1) << 19 | (inf[92] & 1) << 20 | (inf[93] & 1) << 21 |
	    (inf[94] & 1) << 22 | (inf[97] & 1) << 24 | (inf[98] & 1) << 25 | (inf[100] & 1) << 28;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 6, 0x204c), v);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 10, 0x205c), t33_sw_pair(inf[107], inf[108], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 11, 0x2060), (inf[109] & 0xff) | (inf[110] & 0xff) << 8);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 36, 0x2064), t33_sw_pair(inf[111], inf[112], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 37, 0x2064), t33_sw_pair(inf[113], inf[114], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 39, 0x206c), t33_sw_pair(inf[115], inf[116], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 40, 0x206c), t33_sw_pair(inf[117], inf[118], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 13, 0x2074), t33_sw_pair(inf[119], inf[120], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 14, 0x2078), t33_sw_pair(inf[122], inf[123], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 15, 0x207c), (inf[124] & 0xff) | (inf[125] & 0xff) << 8);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 42, 0x2080), t33_sw_pair(inf[126], inf[127], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 43, 0x2080), t33_sw_pair(inf[128], inf[129], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 45, 0x2088), t33_sw_pair(inf[130], inf[131], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 46, 0x2088), t33_sw_pair(inf[132], inf[133], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 17, 0x2090), t33_sw_pair(inf[134], inf[135], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 18, 0x2094), (inf[140] & 1) | (inf[141] & 0x1ff) << 16);
	v = (inf[142] & 1) | (inf[144] & 3) << 8 | (inf[143] & 0xfff) << 16;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 19, 0x2098), v);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 21, 0x20a0), t33_sw_pair(inf[147], inf[148], 0xfff));
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 34, 0x2170), (inf[136] & 3) | (inf[137] & 0x1ff) << 16);
	inf = dpcInfo[c];
	v = (inf[72] & 1) | (inf[74] & 1) << 8 | (inf[75] & 1) << 12 | (inf[138] & 0x7ff) << 16;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 3, 0x2040), v);
	v = (inf[73] & 0x1f) | (inf[76] & 0x1f) << 8 | (inf[106] & 0x3f) << 16 | (inf[121] & 0x3f) << 24;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 4, 0x2044), v);
	v = (inf[77] & 1) | (inf[78] & 1) << 8 | (inf[79] & 1) << 16 | (inf[80] & 3) << 24;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 5, 0x2048), v);
	v = (inf[101] & 3) | (inf[102] & 3) << 4 | (inf[103] & 3) << 8;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 8, 0x2054), v);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 9, 0x2058), (inf[104] & 3) | (inf[105] & 3) << 4);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 18, 0x2094), (inf[140] & 1) | (inf[141] & 0x1ff) << 16);
	v = (inf[142] & 1) | (inf[144] & 3) << 8 | (inf[143] & 0xfff) << 16;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 19, 0x2098), v);
	v = (inf[145] & 3) | (inf[146] & 3) << 8 | (inf[149] & 0x3f) << 16;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 20, 0x209c), v);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 22, 0x20a4), t33_sw_b4(inf[150], inf[151], inf[152], inf[153]));
	v = (inf[154] & 0x3f) | (inf[155] & 0x3f) << 8 | (inf[156] & 0x3f) << 16 | (inf[157] & 0x3f) << 24;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 24, 0x20b4), v);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 26, 0x20c4), inf[71] & 7);
	v = (inf[158] & 1) | (inf[159] & 0x1ff) << 4 | (inf[160] & 0x1ff) << 16;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 27, 0x20c8), v);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 28, 0x20cc), (inf[161] & 0x1ff) | (inf[162] & 0x1ff) << 12);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 30, 0x20e0), (inf[163] & 0x1ff) | (inf[164] & 0x1ff) << 12);
	v = (inf[165] & 0xfff) | (inf[166] & 0x7ff) << 12 | (inf[167] & 0x1f) << 24;
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 31, 0x20e4), v);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 32, 0x20e8), (inf[168] & 0x1f) | (inf[169] & 0xfff) << 8);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 34, 0x2170), (inf[136] & 3) | (inf[137] & 0x1ff) << 16);
	tisp_reg_write_t33(c, 2, t33_sw_slot(m, 1, 0x2010), 1);
	tispDpcRefresh = tisp_rcfg_get_dma_state(2) ? 2 : 1;
}

#undef T33_I8
#undef T33_I16

#endif /* TX_ISP_T33_SWSMALL_H */
