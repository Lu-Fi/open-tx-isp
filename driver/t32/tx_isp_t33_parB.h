/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) CDNS parameter refresh (vendor tisp_cdns_par_refresh).
 *
 * The vendor keeps the CDNS block state in cdnsInfo[ch] (word array) and the
 * IQ bin section pointer at byte 432 of the state.  The refresh interpolates
 * 53 u8 tables of the section with the 16.16 gain at byte 436, copies one mode
 * byte to word 38, runs func_cdns_para_update, and when the DMA is idle writes
 * the register list of module 21 through tisp_reg_write_t33 (cfg mode 0:
 * register addresses, else list slots).  The gain is kept at the previous
 * one when it moved by less than the hysteresis.
 *
 * Returns the state pointer (v0 of the vendor code), and sets *refresh to
 * 1 after the list write or 2 when the DMA is busy.
 */
#ifndef TX_ISP_T33_PARB_H
#define TX_ISP_T33_PARB_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_swcdns.h"
#include "tx_isp_t33_fd_cdns.h"

#define T33_PARB_MOD	21

static inline u32 t33_cdns_par_refresh(u32 ch, u32 gain, u32 hyst, u8 *s,
				       u8 mode, u8 *refresh,
				       int (*dma_state)(u32), void (*reg_write)(u32, u32, u32, u32),
				       void (*para_update)(u32))
{
	u32 *inf = (u32 *)s;
	const u8 *sec = (const u8 *)(unsigned long)T33_CDNS_W(T33_CDNS_SEC_PTR_OFF);
	u32 id = ch & 0xff, g = gain, prev = T33_CDNS_W(T33_CDNS_GAIN_OFF);
	u32 i, f;
	unsigned int k;

	if (prev != 0xffffffff) {
		u32 d = g >= prev ? g - prev : prev - g;

		if (d < hyst) {
			T33_CDNS_W(T33_CDNS_GAIN_OFF) = prev;
			return (u32)(unsigned long)s;
		}
	}
	i = g >> 16;
	f = g & 0xffff;
	T33_CDNS_W(152) = sec[422];
	for (k = 0; k < 38; k++)
		inf[k] = t32_tp_intp_u8(i, f, sec + 4 + 11 * k);
	for (k = 0; k < 15; k++)
		inf[39 + k] = t32_tp_intp_u8(i, f, sec + 423 + 11 * k);

	para_update(id);

	if (dma_state(T33_PARB_MOD)) {
		T33_CDNS_W(T33_CDNS_GAIN_OFF) = g;
		*refresh = 2;
		return (u32)(unsigned long)s;
	}

	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 3, 0x40), T33_CDNS_W(216) & 7);
	for (k = 0; k < 8; k++)
		reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 5 + k, 0x50 + 4 * k),
			  t33_cdns_pack3(s, 220 + 12 * k));
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 13, 0x70),
		  s[316] | T33_CDNS_W(328) << 24 | (T33_CDNS_W(320) & 0x3f) << 8 |
		  (T33_CDNS_W(324) & 0x3f) << 16);
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 14, 0x74),
		  (T33_CDNS_W(336) & 0x3f) << 8 | (T33_CDNS_W(340) & 0x3f) << 16 |
		  (T33_CDNS_W(332) & 7));
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 15, 0x78), T33_CDNS_W(344) & 3);
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 16, 0x7c),
		  s[348] | (T33_CDNS_W(352) & 0xf) << 16);
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 17, 0x80),
		  (T33_CDNS_W(360) & 0x3f) << 8 | (T33_CDNS_W(364) & 0x3f) << 16 |
		  (T33_CDNS_W(356) & 7));
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 18, 0x84), t33_cdns_v18(s));
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 19, 0x88),
		  (T33_CDNS_W(404) & 0x1f) << 8 | (T33_CDNS_W(408) & 0x3f) << 16 |
		  (T33_CDNS_W(400) & 0x3f) | (T33_CDNS_W(412) & 0x3f) << 24);
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 20, 0x8c),
		  ((T33_CDNS_W(420) << 8) & 0xffff) | (u32)s[428] << 16 |
		  (T33_CDNS_W(416) & 0x1f) | (T33_CDNS_W(424) & 1) << 24);
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 21, 0x90),
		  (T33_CDNS_W(388) & 0x3f) << 8 | (T33_CDNS_W(392) & 0x3f) << 16 |
		  (T33_CDNS_W(384) & 0x3f) | (T33_CDNS_W(396) & 0x3f) << 24);
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 22, 0x94),
		  (T33_CDNS_W(380) & 0x3f) << 8 | (T33_CDNS_W(376) & 0x3f));
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 18, 0x84), t33_cdns_v18(s));
	reg_write(id, T33_PARB_MOD, t33_cdns_reg(mode, 1, 0x10), 1);

	T33_CDNS_W(T33_CDNS_GAIN_OFF) = g;
	*refresh = 1;
	return (u32)(unsigned long)s;
}

#endif /* TX_ISP_T33_PARB_H */
