/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) RDNS day/night switch (vendor tisp_rdns_switch_para).
 *
 * rdnsInfo[ch] points at the RDNS block state (a word array): word 208 is
 * the IQ section pointer, 209 the gain (16.16), 210 the EV.  The switch
 * reloads the section (func_rdns_bin_para_update), interpolates over EV
 * (words 90..97, tables at section +834, EV points at +924) and gain
 * (words 2..75, tables at section +6, 11 bytes each), derives the register
 * values (func_rdns_para_update) and writes the RDNS list (module 3): to the
 * list slot when rdnsCfgMode is set, else to register 0x20000 + addr.
 * tispRdnsRefresh: 1 written, 2 DMA busy (nothing written).
 */
#ifndef TX_ISP_T33_SWRDNS_H
#define TX_ISP_T33_SWRDNS_H

#include "tx_isp_t33_blk.h"

extern u32 *rdnsInfo[4];
extern u8 rdnsCfgMode;
extern u8 tispRdnsRefresh;
extern void func_rdns_bin_para_update(u32 ch);
extern void func_rdns_para_update(u32 ch);
extern u32 tisp_ev_intp_uint8_t(u32 ev, const u8 *e, const u8 *t);
extern u32 tisp_simple_intp_uint8_t(int i, u32 f, const u8 *t);
extern u32 tisp_rcfg_get_dma_state(u32 id);
extern void tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val);

static inline void t33_rdns_wr(u32 ch, u32 slot, u32 addr, u32 val)
{
	tisp_reg_write_t33(ch, 3, rdnsCfgMode ? slot : 0x20000 + addr, val);
}

/* four 6-bit fields, low byte first */
static inline u32 t33_rdns_pack6(const u32 *w)
{
	return ((w[1] & 0x3f) << 8) | ((w[2] & 0x3f) << 16) | (w[0] & 0x3f) | ((w[3] & 0x3f) << 24);
}

/* four 8-bit fields, low byte first */
static inline u32 t33_rdns_pack8(const u32 *w)
{
	return (w[0] & 0xff) | (w[3] << 24) | ((w[1] << 8) & 0xffff) | ((w[2] & 0xff) << 16);
}

/* tisp_rdns_switch_para */
static inline void t33_rdns_switch_para(u32 ch)
{
	u32 c = ch & 0xff, *inf, *v;
	const u8 *sec, *ev;
	u32 gain, f;
	int i;
	unsigned int k;

	func_rdns_bin_para_update(c);
	inf = rdnsInfo[c];
	sec = (const u8 *)(unsigned long)inf[208];
	ev = sec + 924;
	for (k = 0; k < 8; k++)
		inf[90 + k] = tisp_ev_intp_uint8_t(inf[210], ev, sec + 834 + 11 * k);

	inf = rdnsInfo[c];
	sec = (const u8 *)(unsigned long)inf[208];
	gain = inf[209];
	i = (int)gain >> 16;
	f = gain & 0xffff;
	for (k = 0; k < 74; k++)
		inf[2 + k] = tisp_simple_intp_uint8_t(i, f, sec + 6 + 11 * k);

	func_rdns_para_update(c);

	if (tisp_rcfg_get_dma_state(3)) {
		tispRdnsRefresh = 2;
		return;
	}

	v = rdnsInfo[c];
	t33_rdns_wr(c, 8, 0x60b4, ((v[197] & 0xff) << 16) | (v[196] & 0xff));
	t33_rdns_wr(c, 9, 0x60b8, ((v[199] & 0xf) << 16) | (v[198] & 0xf));
	t33_rdns_wr(c, 10, 0x60bc, ((v[202] & 0xff) << 16) | (v[201] & 0xff));
	t33_rdns_wr(c, 11, 0x60c0, ((v[204] & 0x1f) << 16) | (v[203] & 0x1f));
	t33_rdns_wr(c, 12, 0x60c4, ((v[205] & 0x3f) << 16) | (v[200] & 0x1f));
	t33_rdns_wr(c, 13, 0x60c8, ((v[207] & 0xf) << 16) | (v[206] & 0xf));
	t33_rdns_wr(c, 4, 0x6044, ((v[101] & 7) << 8) | ((v[102] & 3) << 16) | (v[100] & 7) | ((v[103] & 3) << 24));
	for (k = 0; k < 8; k++)
		t33_rdns_wr(c, 15 + k, 0x6048, t33_rdns_pack6(v + 104 + 4 * k));
	for (k = 0; k < 8; k++)
		t33_rdns_wr(c, 24 + k, 0x6068, t33_rdns_pack6(v + 136 + 4 * k));
	for (k = 0; k < 6; k++)
		t33_rdns_wr(c, 33 + k, 0x6088, ((v[169 + 2 * k] & 0xfff) << 16) | (v[168 + 2 * k] & 0xfff));
	t33_rdns_wr(c, 3, 0x6040, ((v[99] & 3) << 16) | ((v[180] & 1) << 24) | (v[98] & 7));
	t33_rdns_wr(c, 6, 0x60a0, ((v[182] & 0x1fff) << 16) | (v[181] & 0x1ff));
	for (k = 0; k < 3; k++)
		t33_rdns_wr(c, 40 + k, 0x60a4, t33_rdns_pack8(v + 183 + 4 * k));
	t33_rdns_wr(c, 43, 0x60a4, v[195] & 0xff);
	t33_rdns_wr(c, 1, 0x6020, 1);
	tispRdnsRefresh = 1;
}

#endif /* TX_ISP_T33_SWRDNS_H */
