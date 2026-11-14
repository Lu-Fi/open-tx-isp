/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MDNS frame-done interrupt and compare-register writer.
 * Register access / rcfg trigger are passed in as callbacks (they are
 * ported elsewhere): wr(id, block, reg_or_slot, value), trig(id, block).
 */
#ifndef TX_ISP_T33_MDNSA_H
#define TX_ISP_T33_MDNSA_H

#include "tx_isp_t33_blk.h"

typedef int (*t33_mdnsa_wr_fn)(u32 id, u32 blk, u32 reg, u32 val);
typedef int (*t33_mdnsa_trig_fn)(u32 id, u32 blk);

/* register address, or the list slot number when the list (cfg mode) is used */
static inline u32 t33_mdnsa_target(u8 cfg, u32 slot, u32 reg)
{
	return cfg ? slot : reg;
}

/* tisp_mdns_t33_frame_done_interrupt */
static inline int t33_mdns_frame_done(u32 id, u32 init_par, u8 *init, u8 *refresh, u8 *start, u8 cfg,
				      t33_mdnsa_wr_fn wr, t33_mdnsa_trig_fn trig)
{
	if (init_par != 1)
		return 1;
	if (*init == 1) {
		wr(id, 15, t33_mdnsa_target(cfg, 2, 0xd018), 0);
		*init = 0;
	}
	if (*refresh == 1) {
		trig(id & 0xff, 15);
		*refresh = 0;
	}
	if (*start == 1) {
		wr(id, 15, t33_mdnsa_target(cfg, 2, 0xd018), 0);
		*start = 0;
	}
	return 1;
}

#define T33_MDNSA_W(slot, reg, val) wr(id, 15, t33_mdnsa_target(cfg, (slot), (reg)), (val))

/* tisp_mdns_t33_cmp_reg_write: info = the channel's block state words */
static inline void t33_mdns_cmp_reg_write(u32 id, const u32 *info, u8 cfg, t33_mdnsa_wr_fn wr)
{
	T33_MDNSA_W(451, 0xd500, (info[2193] << 1) | 0x20);
	T33_MDNSA_W(452, 0xd504, info[2192]);
	T33_MDNSA_W(453, 0xd508, 0);
	T33_MDNSA_W(454, 0xd50c, info[2194]);
	T33_MDNSA_W(455, 0xd510, 0xa03c00a0);
	T33_MDNSA_W(456, 0xd514, 0x33180204);
	T33_MDNSA_W(457, 0xd518, 0x031a0203);
	T33_MDNSA_W(458, 0xd51c, info[2195]);
	T33_MDNSA_W(459, 0xd520, info[2196]);
	T33_MDNSA_W(460, 0xd524, 0x47382312);
	T33_MDNSA_W(461, 0xd528, 0x746c6255);
	T33_MDNSA_W(462, 0xd52c, 0x007b7977);
	T33_MDNSA_W(463, 0xd530, 0x31202020);
	T33_MDNSA_W(464, 0xd534, 0x52424141);
	T33_MDNSA_W(465, 0xd538, 0x63535252);
	T33_MDNSA_W(466, 0xd53c, 0xfcfe0002);
	T33_MDNSA_W(467, 0xd540, 0xf6f8fafa);
	T33_MDNSA_W(468, 0xd544, 0xf4f6f6f6);
	T33_MDNSA_W(469, 0xd548, 0);
	T33_MDNSA_W(470, 0xd54c, 0x77654433);
	T33_MDNSA_W(471, 0xd550, info[2198]);
	T33_MDNSA_W(472, 0xd554, 0);
	T33_MDNSA_W(473, 0xd558, 0);
	T33_MDNSA_W(474, 0xd55c, 0);
	T33_MDNSA_W(475, 0xd560, info[2199]);
	T33_MDNSA_W(476, 0xd564, info[2191]);
	T33_MDNSA_W(477, 0xd568, info[2200]);
	T33_MDNSA_W(478, 0xd56c, 0x000a1407);
	T33_MDNSA_W(480, 0xd600, (info[2193] << 1) | 0x20);
	T33_MDNSA_W(481, 0xd604, info[2192]);
	T33_MDNSA_W(482, 0xd608, info[2191]);
}

#undef T33_MDNSA_W

#endif /* TX_ISP_T33_MDNSA_H */
