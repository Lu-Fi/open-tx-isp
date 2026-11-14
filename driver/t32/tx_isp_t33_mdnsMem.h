/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MDNS memory configuration: tisp_mdns_t33_set_mem_cfg.
 *
 * The reference frame, statistics plane and motion plane addresses / strides
 * go to the MDNS list (tisp_reg_write_t33: slot in the CfgMode path, the
 * register otherwise).  With the WDR flag of the memory info set, the
 * block's parameter words (mdnsInfo, word index = byte offset / 4) are
 * written as well.  Hardware access goes through ops, the module statics
 * through env.
 */
#ifndef TX_ISP_T33_MDNSMEM_H
#define TX_ISP_T33_MDNSMEM_H

#include "tx_isp_t33_blk.h"

struct t33_mdns_mem_ops {
	void (*reg_write)(u32 chn, u32 blk, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	void (*sys_write)(u32 reg, u32 val);			/* system_reg_write */
	void (*trigger)(u32 chn, u32 blk);			/* tisp_rcfg_module_cfg_trig */
	void (*log)(int line);					/* printk of a RISC-V parameter mismatch */
};

struct t33_mdns_mem_env {
	const u8 *init_par;		/* stIspInitPar (1008 bytes per channel) */
	u8 *mem_info;			/* stMemInfo (76 bytes per channel) */
	const u32 *const *mdns_info;	/* mdnsInfo[chn] */
	const u8 *cfg_mode;		/* mdnsCfgMode */
	const u8 *riscv_en;		/* riscvEnable */
	const u8 *riscv_par;		/* stRiscvPara (12800 bytes per channel) */
};

#define T33_MM_W(slot, reg, val)	ops->reg_write(chn, 15, mode ? (slot) : (reg), (val))
#define T33_MM_B(i)			(v[i] & 0xff)
#define T33_MM_P3(x, y, z)		((T33_MM_B(x) << 8) | T33_MM_B(y) | (T33_MM_B(z) << 16))
#define T33_MM_L9(hi, lo)		(((v[hi] & 0x1ff) << 12) | (v[lo] & 0x1ff))

/* the WDR part of tisp_mdns_t33_set_mem_cfg: block words of mdnsInfo[chn] */
static inline void t33_mdns_mem_words(u32 chn, u32 mode, const u32 *v, const struct t33_mdns_mem_ops *ops)
{
	unsigned int j;

	T33_MM_W(3, 0xd01c, T33_MM_B(0x3b9));
	T33_MM_W(4, 0xd020, ((v[0x4f4] & 1) << 4) | ((v[0x3ba] & 1) << 16) | (v[0x3b2] & 1) |
			    ((v[0x3bb] & 1) << 17) | ((v[0x3bc] & 1) << 18) | ((v[0x4f7] & 1) << 20) |
			    ((v[0x4f8] & 1) << 21) | ((v[0x4f9] & 1) << 22) | ((v[0x4fa] & 1) << 23) |
			    ((v[0x3bd] & 1) << 24) | ((v[0x3be] & 1) << 25) | ((v[0x3bf] & 1) << 26) |
			    ((v[0x3c0] & 1) << 27));
	T33_MM_W(5, 0xd024, (v[0x3b3] & 1) | (v[0x3c1] << 31) | (v[0x88e] == 1 ? 0x10000000 : 0) |
			    ((v[0x3b4] & 1) << 4) | ((v[0x4f5] & 1) << 8) | ((v[0x4f6] & 1) << 12) |
			    ((v[0x3b6] & 1) << 21) | ((v[0x3b7] & 1) << 22) | ((v[0x3b8] & 1) << 23) |
			    ((v[0x4e7] & 1) << 29));
	T33_MM_W(59, 0xd200, (T33_MM_B(0x622) << 4) | ((v[0x623] & 1) << 12) | (v[0x621] & 1) |
			     ((v[0x624] & 1) << 16) | ((v[0x625] & 1) << 20) | ((v[0x626] & 1) << 24) |
			     ((v[0x627] & 1) << 28));
	T33_MM_W(60, 0xd204, ((v[0x629] & 1) << 4) | ((v[0x632] & 0xf) << 8) | (v[0x628] & 7) |
			     ((v[0x633] & 0xf) << 12) | ((v[0x634] & 0xf) << 16) | ((v[0x635] & 0xf) << 20));
	T33_MM_W(61, 0xd208, ((v[0x637] & 7) << 8) | ((v[0x640] & 1) << 12) | T33_MM_B(0x636) |
			     (T33_MM_B(0x641) << 16) | ((v[0x642] & 7) << 24));
	T33_MM_W(64, 0xd214, ((v[0x67b] & 0xf) << 12) | (v[0x67a] & 0x1ff));
	T33_MM_W(254, 0xd220, T33_MM_L9(0x5b8, 0x58d));
	for (j = 0; j < 15; j++)
		T33_MM_W(255 + j, 0xd220, T33_MM_L9(0x5ba + 2 * j, 0x5b9 + 2 * j));
	T33_MM_W(270, 0xd220, v[0x5d7] & 0x1ff);
	T33_MM_W(66, 0xd224, ((v[0x572] & 3) << 4) | ((v[0x573] & 7) << 8) | (v[0x571] & 3) |
			     ((v[0x574] & 1) << 12) | ((v[0x575] & 1) << 16) | ((v[0x576] & 0x1f) << 20));
	T33_MM_W(68, 0xd22c, ((v[0x57c] & 3) << 8) | ((v[0x57d] & 3) << 12) | (v[0x57b] & 0x1f) |
			     ((v[0x57e] & 3) << 16) | ((v[0x5ad] & 0x1ff) << 20));
	T33_MM_W(69, 0xd230, (T33_MM_B(0x5af) << 4) | (T33_MM_B(0x5b0) << 12) | (v[0x5ae] & 0xf) |
			     (T33_MM_B(0x5b1) << 20));
	T33_MM_W(70, 0xd234, ((v[0x5b3] & 0x1f) << 8) | ((v[0x5b4] & 7) << 16) | T33_MM_B(0x5b2) |
			     ((v[0x5b5] & 1) << 20));
	T33_MM_W(71, 0xd238, (T33_MM_B(0x5b7) << 8) | ((v[0x5d8] & 1) << 16) | T33_MM_B(0x5b6) |
			     ((v[0x5d9] & 1) << 20));
	T33_MM_W(287, 0xd240, T33_MM_P3(0x60e, 0x60d, 0x60f));
	T33_MM_W(288, 0xd240, (T33_MM_B(0x611) << 8) | T33_MM_B(0x610));
	T33_MM_W(73, 0xd244, ((v[0x5db] & 0xf) << 4) | ((v[0x5dc] & 0x1f) << 8) | (v[0x5da] & 0xf) |
			     ((v[0x5dd] & 0x3f) << 16) | ((v[0x5de] & 0x3f) << 24));
	T33_MM_W(74, 0xd248, ((v[0x612] & 0x1ff) << 8) | (v[0x5df] & 0x3f) | ((v[0x613] & 0x1ff) << 20));
	T33_MM_W(75, 0xd24c, ((v[0x615] & 0xf) << 12) | ((v[0x616] & 0xf) << 16) | (v[0x614] & 0x1ff) |
			     ((v[0x617] & 0xf) << 20) | ((v[0x618] & 0x1f) << 24));
	T33_MM_W(294, 0xd254, T33_MM_P3(0x69a, 0x699, 0x69b));
	T33_MM_W(295, 0xd254, T33_MM_P3(0x69d, 0x69c, 0x6a4));
	T33_MM_W(297, 0xd254, T33_MM_P3(0x6a9, 0x6a8, 0x6aa));
	T33_MM_W(298, 0xd254, T33_MM_P3(0x6b2, 0x6ab, 0x6b4));
	T33_MM_W(77, 0xd258, ((v[0x67d] & 0x3ff) << 4) | ((v[0x67e] & 0xf) << 16) | (v[0x67c] & 0xf) |
			     ((v[0x67f] & 0x3ff) << 20));
	T33_MM_W(78, 0xd25c, ((v[0x681] & 0x3ff) << 4) | ((v[0x682] & 0xf) << 16) | (v[0x680] & 0xf) |
			     ((v[0x683] & 0xf) << 20) | ((v[0x684] & 0x1f) << 24));
	T33_MM_W(79, 0xd260, ((v[0x686] & 0x1f) << 8) | ((v[0x687] & 3) << 16) | (v[0x685] & 0x1f) |
			     ((v[0x688] & 1) << 20) | ((v[0x689] & 1) << 24) | ((v[0x68a] & 1) << 28));
	T33_MM_W(80, 0xd264, ((v[0x68c] & 0x7f) << 4) | ((v[0x69e] & 0x1ff) << 12) | (v[0x68b] & 1));
	T33_MM_W(81, 0xd268, ((v[0x6a0] & 0x1ff) << 12) | (v[0x69f] & 0x1ff) | ((v[0x6a1] & 0xf) << 24));
	T33_MM_W(82, 0xd26c, ((v[0x6a3] & 0xf) << 4) | ((v[0x6ac] & 0x1ff) << 8) | (v[0x6a2] & 0xf) |
			     ((v[0x6ad] & 0x1ff) << 20));
	T33_MM_W(83, 0xd270, ((v[0x6af] & 0xf) << 12) | ((v[0x6b0] & 0xf) << 16) | (v[0x6ae] & 0x1ff) |
			     ((v[0x6b1] & 0xf) << 20));
	T33_MM_W(84, 0xd274, ((v[0x6b7] & 0xf) << 12) | (v[0x6b6] & 0x1ff) | ((v[0x6b8] & 0x1f) << 16));
	T33_MM_W(86, 0xd284, ((v[0x7c4] & 1) << 12) | (T33_MM_B(0x7c3) << 4) | (v[0x7c2] & 1) |
			     ((v[0x7c5] & 1) << 16) | ((v[0x7c6] & 1) << 20) | ((v[0x7c7] & 1) << 24) |
			     ((v[0x7c8] & 1) << 28));
	T33_MM_W(87, 0xd288, ((v[0x7ca] & 1) << 4) | ((v[0x7d3] & 0xf) << 8) | (v[0x7c9] & 7) |
			     ((v[0x7d4] & 0xf) << 12) | ((v[0x7d5] & 0xf) << 16) | ((v[0x7d6] & 0xf) << 20));
	T33_MM_W(88, 0xd28c, ((v[0x7d8] & 7) << 8) | ((v[0x7e1] & 1) << 12) | T33_MM_B(0x7d7) |
			     (T33_MM_B(0x7e2) << 16) | ((v[0x7e3] & 7) << 24));
	T33_MM_W(91, 0xd298, ((v[0x81c] & 0xf) << 12) | (v[0x81b] & 0x1ff));
	T33_MM_W(340, 0xd2a0, T33_MM_P3(0x72b, 0x72a, 0x72c));
	T33_MM_W(341, 0xd2a0, T33_MM_P3(0x72e, 0x72d, 0x72f));
	T33_MM_W(342, 0xd2a0, T33_MM_P3(0x731, 0x730, 0x732));
	T33_MM_W(353, 0xd2a4, T33_MM_L9(0x749, 0x6ff));
	for (j = 0; j < 15; j++)
		T33_MM_W(354 + j, 0xd2a4, T33_MM_L9(0x74b + 2 * j, 0x74a + 2 * j));
	T33_MM_W(369, 0xd2a4, v[0x768] & 0x1ff);
	T33_MM_W(93, 0xd2ac, ((v[0x6ba] & 3) << 4) | ((v[0x6bb] & 7) << 8) | (v[0x6b9] & 3) |
			     ((v[0x6bc] & 1) << 12) | ((v[0x6bd] & 1) << 16) | ((v[0x6ee] & 3) << 20) |
			     ((v[0x6ef] & 3) << 24) | ((v[0x6f0] & 3) << 28));
	T33_MM_W(95, 0xd2b4, ((v[0x73f] & 0xf) << 12) | T33_MM_B(0x740) * 0x10000 | (v[0x73e] & 0x1ff));
	T33_MM_W(97, 0xd2bc, ((v[0x746] & 1) << 4) | ((v[0x769] & 1) << 8) | (v[0x745] & 7) |
			     ((v[0x76a] & 1) << 12));
	T33_MM_W(404, 0xd2c4, T33_MM_P3(0x7ae, 0x7ad, 0x7af));
	T33_MM_W(405, 0xd2c4, T33_MM_P3(0x7b1, 0x7b0, 0x7b2));
	T33_MM_W(99, 0xd2c8, ((v[0x76c] & 0xf) << 4) | ((v[0x76d] & 0x1f) << 8) | (v[0x76b] & 0xf) |
			     ((v[0x76e] & 0x3f) << 16) | ((v[0x76f] & 0x3f) << 24));
	T33_MM_W(100, 0xd2cc, ((v[0x7b3] & 0x1ff) << 8) | (v[0x770] & 0x3f) | ((v[0x7b4] & 0x1ff) << 20));
	T33_MM_W(101, 0xd2d0, ((v[0x7b6] & 0xf) << 12) | ((v[0x7b7] & 0xf) << 16) | (v[0x7b5] & 0x1ff) |
			      ((v[0x7b8] & 0xf) << 20) | ((v[0x7b9] & 0x1f) << 24));
	T33_MM_W(411, 0xd2d8, T33_MM_P3(0x83b, 0x83a, 0x83c));
	T33_MM_W(412, 0xd2d8, T33_MM_P3(0x83e, 0x83d, 0x845));
	T33_MM_W(414, 0xd2d8, T33_MM_P3(0x84a, 0x849, 0x84b));
	T33_MM_W(415, 0xd2d8, T33_MM_P3(0x853, 0x84c, 0x85b));
	T33_MM_W(416, 0xd2d8, T33_MM_P3(0x86b, 0x863, 0x854));
	T33_MM_W(417, 0xd2d8, T33_MM_P3(0x864, 0x85c, 0x86c));
	T33_MM_W(419, 0xd2d8, T33_MM_P3(0x856, 0x86d, 0x85e));
	T33_MM_W(420, 0xd2d8, T33_MM_P3(0x86e, 0x866, 0x857));
	T33_MM_W(421, 0xd2d8, T33_MM_P3(0x867, 0x85f, 0x86f));
	T33_MM_W(423, 0xd2d8, T33_MM_P3(0x859, 0x870, 0x861));
	T33_MM_W(424, 0xd2d8, T33_MM_P3(0x871, 0x869, 0x85a));
	T33_MM_W(425, 0xd2d8, T33_MM_P3(0x86a, 0x862, 0x872));
	T33_MM_W(427, 0xd2dc, T33_MM_L9(0x840, 0x83f));
	T33_MM_W(428, 0xd2dc, T33_MM_L9(0x84d, 0x841));
	T33_MM_W(429, 0xd2dc, T33_MM_L9(0x84f, 0x84e));
	T33_MM_W(430, 0xd2dc, T33_MM_L9(0x874, 0x873));
	T33_MM_W(431, 0xd2dc, T33_MM_L9(0x876, 0x875));
	T33_MM_W(432, 0xd2dc, T33_MM_L9(0x878, 0x877));
	T33_MM_W(433, 0xd2dc, T33_MM_L9(0x87a, 0x879));
	T33_MM_W(103, 0xd2e0, ((v[0x843] & 0xf) << 4) | ((v[0x844] & 0xf) << 8) | (v[0x842] & 0xf) |
			      ((v[0x850] & 0xf) << 12) | ((v[0x851] & 0xf) << 16) | ((v[0x852] & 0xf) << 20) |
			      ((v[0x87b] & 0xf) << 24));
	T33_MM_W(104, 0xd2e4, ((v[0x87d] & 0xf) << 4) | ((v[0x87e] & 0xf) << 8) | (v[0x87c] & 0xf) |
			      ((v[0x87f] & 0xf) << 12) | ((v[0x880] & 0xf) << 16) | ((v[0x881] & 0xf) << 20) |
			      ((v[0x882] & 0xf) << 24));
	T33_MM_W(105, 0xd2e8, ((v[0x81e] & 0x3ff) << 4) | ((v[0x81f] & 0xf) << 16) | (v[0x81d] & 0xf) |
			      ((v[0x820] & 0x3ff) << 20));
	T33_MM_W(106, 0xd2ec, ((v[0x822] & 0x3ff) << 4) | ((v[0x823] & 0xf) << 16) | (v[0x821] & 0xf) |
			      ((v[0x824] & 0xf) << 20) | ((v[0x825] & 0x1f) << 24));
	T33_MM_W(107, 0xd2f0, ((v[0x827] & 0x1f) << 8) | ((v[0x828] & 3) << 16) | (v[0x826] & 0x1f) |
			      ((v[0x829] & 1) << 20) | ((v[0x82a] & 1) << 24) | ((v[0x82b] & 1) << 28));
	T33_MM_W(108, 0xd2f4, ((v[0x82d] & 0x7f) << 4) | ((v[0x883] & 0x1f) << 12) | (v[0x82c] & 1));
}

/* tisp_mdns_t33_set_mem_cfg: chn, ISP memory base pa */
static inline void t33_mdns_set_mem_cfg(u8 chn, u32 pa, const struct t33_mdns_mem_env *e,
					const struct t33_mdns_mem_ops *ops)
{
	const u8 *ip = e->init_par + chn * 1008;
	u8 *mi = e->mem_info + chn * 76;
	u32 w = ip[12] | ip[13] << 8, h = ip[14] | ip[15] << 8;
	u32 r = mi[8] | mi[9] << 8;
	u32 mode = *e->cfg_mode, sw904 = t33_le32(e->init_par + 904);
	u32 bytes, off, units, wl, st, s7;

	mi[16] = pa & 0xff;
	mi[17] = pa >> 8 & 0xff;
	mi[18] = pa >> 16 & 0xff;
	mi[19] = pa >> 24;
	if (r <= 1000)
		bytes = (w * h * 3) >> 1;
	else
		bytes = (((((w + 31) >> 5) * (0x5dc00 / r) * h) + 63) >> 6) << 3;
	off = (((bytes + 255) >> 8) + 2) << 8;
	units = off >> 8;
	ops->reg_write(chn, 15, mode ? 9 : 0xd040, pa);
	if (sw904 == 1)
		ops->sys_write(0xd080, units);
	else
		ops->reg_write(chn, 15, mode ? 18 : 0xd080, units);
	wl = (w + 7) >> 3;
	ops->reg_write(chn, 15, mode ? 10 : 0xd044, wl << 3);
	ops->reg_write(chn, 15, mode ? 12 : 0xd04c, wl << 3);
	st = ((wl + 15) >> 4) << 3;
	ops->reg_write(chn, 15, mode ? 13 : 0xd050, pa + off);
	ops->reg_write(chn, 15, mode ? 14 : 0xd054, st);
	off += ((((h + 7) * st) >> 3) + 1023) >> 10 << 10;
	if (mi[11] != 1) {
		s7 = ((w >> 1) + 7) & 0xfff8;
		ops->reg_write(chn, 15, mode ? 15 : 0xd058, pa + off);
		ops->reg_write(chn, 15, mode ? 16 : 0xd05c, s7);
		off += (((h * s7) >> 1) + 1023) >> 10 << 10;
	} else {
		t33_mdns_mem_words(chn, mode, e->mdns_info[chn], ops);
	}
	if (e->riscv_en[chn] == 1) {
		const u8 *rp = e->riscv_par + chn * 12800;

		if (pa != t33_le32(rp + 12616))
			ops->log(7122);
		else if (off != t33_le32(rp + 12620))
			ops->log(7125);
	}
	if ((u32)(sw904 - 1) == chn)
		ops->trigger(0, 15);
}

#undef T33_MM_W
#undef T33_MM_B
#undef T33_MM_P3
#undef T33_MM_L9

#endif /* TX_ISP_T33_MDNSMEM_H */
