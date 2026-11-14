/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) CDNS day/night IQ-bin section switch (tisp_cdns_switch_para).
 *
 * The vendor keeps the CDNS block state in cdnsInfo[ch] (byte offsets below)
 * and the IQ bin section pointer in tparamsP[ch]; the CDNS section starts at
 * tparamsP[ch] + 0x892c.  The switch interpolates 53 u8 tables of the
 * section with the 16.16 gain at byte 436 of the state, copies one mode byte,
 * runs func_cdns_para_update and (when the DMA is idle) writes the register
 * list of module 21 through tisp_reg_write_t33.
 */
#ifndef TX_ISP_T33_SWCDNS_H
#define TX_ISP_T33_SWCDNS_H

#include "tx_isp_t33_blk.h"

#define T33_CDNS_SEC_OFF	0x892c	/* tparamsP[ch] + this = CDNS section */
#define T33_CDNS_GAIN_OFF	436	/* state byte offset: 16.16 gain */
#define T33_CDNS_SEC_PTR_OFF	432	/* state byte offset: section pointer */
#define T33_CDNS_MODE_OFF	0x1a6	/* mode byte within the section */

/* state word at byte offset b */
#define T33_CDNS_W(b)	(inf[(b) / 4])

/*
 * tisp_cdns_switch_para: returns 1 when the list was written, 2 when the DMA
 * state check reports busy.  mode is cdnsCfgMode.
 */
static inline int t33_cdns_switch(u32 ch, u32 *inf, const u8 *tp, u8 mode,
				  int (*dma_state)(u32), void (*reg_write)(u32, u32, u32, u32),
				  void (*para_update)(u32))
{
	const u8 *sec = tp + T33_CDNS_SEC_OFF;
	u32 gain = T33_CDNS_W(T33_CDNS_GAIN_OFF), i = gain >> 16, f = gain & 0xffff;
	unsigned int k;

	T33_CDNS_W(T33_CDNS_SEC_PTR_OFF) = (u32)(unsigned long)sec;
	T33_CDNS_W(152) = sec[T33_CDNS_MODE_OFF];
	for (k = 0; k < 38; k++)
		inf[k] = t32_tp_intp_u8(i, f, sec + 4 + 11 * k);
	for (k = 0; k < 15; k++)
		inf[39 + k] = t32_tp_intp_u8(i, f, sec + 423 + 11 * k);

	para_update(ch);

	if (dma_state(21))
		return 2;

	reg_write(ch, 21, mode ? 3 : 0x12040, T33_CDNS_W(216) & 7);
	reg_write(ch, 21, mode ? 5 : 0x12050,
		  (T33_CDNS_W(220) & 0xff) | (T33_CDNS_W(224) & 0xff) << 8 |
		  (T33_CDNS_W(228) & 0xff) << 16);
	reg_write(ch, 21, mode ? 6 : 0x12054,
		  (T33_CDNS_W(232) & 0xff) | (T33_CDNS_W(236) & 0xff) << 8 |
		  (T33_CDNS_W(240) & 0xff) << 16);
	reg_write(ch, 21, mode ? 7 : 0x12058,
		  (T33_CDNS_W(244) & 0xff) | (T33_CDNS_W(248) & 0xff) << 8 |
		  (T33_CDNS_W(252) & 0xff) << 16);
	reg_write(ch, 21, mode ? 8 : 0x1205c,
		  (T33_CDNS_W(256) & 0xff) | (T33_CDNS_W(260) & 0xff) << 8 |
		  (T33_CDNS_W(264) & 0xff) << 16);
	reg_write(ch, 21, mode ? 9 : 0x12060,
		  (T33_CDNS_W(268) & 0xff) | (T33_CDNS_W(272) & 0xff) << 8 |
		  (T33_CDNS_W(276) & 0xff) << 16);
	reg_write(ch, 21, mode ? 10 : 0x12064,
		  (T33_CDNS_W(280) & 0xff) | (T33_CDNS_W(284) & 0xff) << 8 |
		  (T33_CDNS_W(288) & 0xff) << 16);
	reg_write(ch, 21, mode ? 11 : 0x12068,
		  (T33_CDNS_W(292) & 0xff) | (T33_CDNS_W(296) & 0xff) << 8 |
		  (T33_CDNS_W(300) & 0xff) << 16);
	reg_write(ch, 21, mode ? 12 : 0x1206c,
		  (T33_CDNS_W(304) & 0xff) | (T33_CDNS_W(308) & 0xff) << 8 |
		  (T33_CDNS_W(312) & 0xff) << 16);
	reg_write(ch, 21, mode ? 13 : 0x12070,
		  (T33_CDNS_W(316) & 0xff) | (T33_CDNS_W(320) & 0x3f) << 8 |
		  (T33_CDNS_W(324) & 0x3f) << 16 | T33_CDNS_W(328) << 24);
	reg_write(ch, 21, mode ? 14 : 0x12074,
		  (T33_CDNS_W(332) & 7) | (T33_CDNS_W(336) & 0x3f) << 8 |
		  (T33_CDNS_W(340) & 0x3f) << 16);
	reg_write(ch, 21, mode ? 15 : 0x12078, T33_CDNS_W(344) & 3);
	reg_write(ch, 21, mode ? 16 : 0x1207c,
		  (T33_CDNS_W(348) & 0xff) | (T33_CDNS_W(352) & 0xf) << 16);
	reg_write(ch, 21, mode ? 17 : 0x12080,
		  (T33_CDNS_W(356) & 7) | (T33_CDNS_W(360) & 0x3f) << 8 |
		  (T33_CDNS_W(364) & 0x3f) << 16);
	reg_write(ch, 21, mode ? 18 : 0x12084,
		  (T33_CDNS_W(368) & 1) | (T33_CDNS_W(372) & 0xff) << 4);
	reg_write(ch, 21, mode ? 19 : 0x12088,
		  (T33_CDNS_W(400) & 0x3f) | (T33_CDNS_W(404) & 0x1f) << 8 |
		  (T33_CDNS_W(408) & 0x3f) << 16 | (T33_CDNS_W(412) & 0x3f) << 24);
	reg_write(ch, 21, mode ? 20 : 0x1208c,
		  (T33_CDNS_W(416) & 0x1f) | (T33_CDNS_W(420) & 0xff) << 8 |
		  (T33_CDNS_W(428) & 0xff) << 16 | (T33_CDNS_W(424) & 1) << 24);
	reg_write(ch, 21, mode ? 21 : 0x12090,
		  (T33_CDNS_W(384) & 0x3f) | (T33_CDNS_W(388) & 0x3f) << 8 |
		  (T33_CDNS_W(392) & 0x3f) << 16 | (T33_CDNS_W(396) & 0x3f) << 24);
	reg_write(ch, 21, mode ? 22 : 0x12094,
		  (T33_CDNS_W(376) & 0x3f) | (T33_CDNS_W(380) & 0x3f) << 8);
	reg_write(ch, 21, mode ? 18 : 0x12084,
		  (T33_CDNS_W(368) & 1) | (T33_CDNS_W(372) & 0xff) << 4);
	reg_write(ch, 21, mode ? 1 : 0x12010, 1);

	return 1;
}

#endif /* TX_ISP_T33_SWCDNS_H */
