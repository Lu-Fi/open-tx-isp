/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) RDNS EV update: port of the vendor's tisp_rdns_ev_update.
 * The vendor function stores the EV, runs func_rdns_bin_para_update, the EV
 * interpolations, the gain interpolations, func_rdns_para_update and then,
 * when the RCFG DMA is idle, writes the block's list slots (or registers).
 * The phases are separate functions; the caller sequences them with the
 * module callees in between.
 */
#ifndef TX_ISP_T33_RDNSEV_H
#define TX_ISP_T33_RDNSEV_H

#include "tx_isp_t33_rdns.h"

#define T33_RDNS_EV_GAIN_WORD	0xd1	/* rdnsInfo word: gain (16.16) */
#define T33_RDNS_EV_WORD	0xd2	/* rdnsInfo word: EV */
#define T33_RDNS_EV_LIST	48
#define T33_RDNS_EV_NWR		36

/* tisp_rdns_ev_update: EV = bits 10..41 of the 64-bit argument */
static inline u32 t33_rdns_ev_value(unsigned long long ev64)
{
	return (u32)(ev64 >> 10);
}

/* the EV and gain interpolations of tisp_rdns_ev_update */
static inline void t33_rdns_ev_interp(u32 *v, const u8 *sec)
{
	u32 gain = v[T33_RDNS_EV_GAIN_WORD];
	u32 i = (u32)((int)gain >> 16), f = gain & 0xffff;
	unsigned int k;

	t33_vb_evintp(v, sec, v[T33_RDNS_EV_WORD], sec + 0x39c, t33_rdns_evintp, ARRAY_SIZE(t33_rdns_evintp));
	for (k = 8; k < ARRAY_SIZE(t33_rdns_intp); k++)
		v[t33_rdns_intp[k].word] = t32_tp_intp_u8(i, f, sec + t33_rdns_intp[k].off);
}

struct t33_rdns_ev_slot {
	u8 slot;
	u32 reg;
};

/* write order of the vendor: list slot and the register of the direct (rdnsCfgMode 0) path */
static const struct t33_rdns_ev_slot t33_rdns_ev_slots[T33_RDNS_EV_NWR] = {
	{ 8, 0x260b4 }, { 9, 0x260b8 }, { 10, 0x260bc }, { 11, 0x260c0 }, { 12, 0x260c4 }, { 13, 0x260c8 },
	{ 4, 0x26044 },
	{ 15, 0x26048 }, { 16, 0x26048 }, { 17, 0x26048 }, { 18, 0x26048 },
	{ 19, 0x26048 }, { 20, 0x26048 }, { 21, 0x26048 }, { 22, 0x26048 },
	{ 24, 0x26068 }, { 25, 0x26068 }, { 26, 0x26068 }, { 27, 0x26068 },
	{ 28, 0x26068 }, { 29, 0x26068 }, { 30, 0x26068 }, { 31, 0x26068 },
	{ 33, 0x26088 }, { 34, 0x26088 }, { 35, 0x26088 }, { 36, 0x26088 }, { 37, 0x26088 }, { 38, 0x26088 },
	{ 3, 0x26040 }, { 6, 0x260a0 },
	{ 40, 0x260a4 }, { 41, 0x260a4 }, { 42, 0x260a4 }, { 43, 0x260a4 },
	{ 1, 0x26020 },
};

struct t33_rdns_ev_wr {
	u32 sel;	/* 3rd argument of tisp_reg_write_t33: slot (cfg mode) or register */
	u32 val;
};

/* the 36 tisp_reg_write_t33 calls (module 3) in vendor order */
static inline unsigned int t33_rdns_ev_writes(const u32 *v, u32 cfgmode, struct t33_rdns_ev_wr *w)
{
	u32 l[T33_RDNS_EV_LIST];
	unsigned int k;

	for (k = 0; k < T33_RDNS_EV_LIST; k++)
		l[k] = 0;
	t33_rdns_store(v, l);
	for (k = 0; k < T33_RDNS_EV_NWR; k++) {
		w[k].sel = cfgmode ? t33_rdns_ev_slots[k].slot : t33_rdns_ev_slots[k].reg;
		w[k].val = l[t33_rdns_ev_slots[k].slot + 1];
	}
	return T33_RDNS_EV_NWR;
}

/* tispRdnsRefresh: 2 when the RCFG DMA was busy (no writes), else 1 */
static inline u8 t33_rdns_ev_flag(int dma_busy)
{
	return dma_busy ? 2 : 1;
}

#endif /* TX_ISP_T33_RDNSEV_H */
