/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 TMO block init: tisp_tmo_init (binary oracle block "tmoR").
 *
 * Registers list 8, allocates the block state (tmoInfo[ch], 10848 bytes) and
 * the V table buffer, interpolates the section values (EV or BV tables),
 * writes the block registers (list slot when tmoCfgMode, else the register)
 * and installs the interrupt handlers.  Everything the function calls that
 * is ported elsewhere or touches hardware is reached through t33_tmoR_ops.
 */
#ifndef TX_ISP_T33_TMOR_H
#define TX_ISP_T33_TMOR_H

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif
#include "tx_isp_t33_blk.h"

struct t33_tmoR_ops {
	int (*module_register)(u32 ch, u32 id);			/* tisp_rcfg_module_register */
	void (*warn_register)(void);				/* printk of a failed register */
	void *(*kmalloc)(u32 size, u32 flags);			/* private_kmalloc */
	void (*warn_alloc)(void);				/* isp_printf of a failed alloc */
	void (*reg_write)(u32 ch, u32 blk, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	void (*sys_write)(u32 reg, u32 val);			/* system_reg_write */
	void (*size_update)(u32 ch);				/* tisp_tmo_tmo_size_update */
	void (*modechange)(u32 ch);				/* tisp_tmo_modechange_stat_refresh */
	void (*bin_update)(u32 ch);				/* func_bin_para_update */
	u32 (*intp[2][3])(u32 x, const u8 *knots, const u8 *tab); /* [bv][u8, s8, u16] */
	void (*lumcurve)(u32 ch);
	void (*satcurve)(u32 ch);
	void (*ic_para)(u32 ch);				/* func_tmo_ic_para_update */
	void (*ramy)(u32 ch);					/* tisp_tmo_ramY_vi0_reg_refresh */
	void (*ddrv_init)(u32 ch);				/* tisp_tmo_s2s3_ddrV_init */
	void (*vi23_ctrl)(u32 ch);
	void (*vi23_refresh)(u32 ch);
	void (*disv)(u32 ch);					/* tisp_tmo_rcfg_reg_num_disV */
	void (*vi0_refresh)(u32 ch);
	void (*irq_set)(u32 ch, u32 irq);			/* system_irq_func_set */
};

static inline u32 t33_tmoR_pair(const u32 *v, u32 lo, u32 hi, u32 m)
{
	return ((v[hi / 4] & m) << 16) | (v[lo / 4] & m);
}

/* tisp_tmo_init: par the parameter block (words 920 / 924 = the EV start value) */
static inline int t33_tmoR_init(const struct t33_tmoR_ops *o, u32 ch, const u8 *par, u8 **info,
				u8 *const *tparams, const u8 *cfg, u8 *refresh, const u8 *initpar,
				u32 *iadam, u16 *regnum)
{
	/* destination byte offset in the state, section offset, kind 0 u8 / 1 s8 / 2 u16 */
	static const struct { u16 dst, src; u8 kind; } ip[] = {
		{ 16, 12, 0 }, { 20, 23, 0 }, { 24, 34, 0 }, { 28, 45, 0 }, { 32, 56, 0 }, { 36, 67, 0 },
		{ 40, 78, 1 }, { 44, 89, 1 }, { 48, 100, 2 }, { 52, 122, 2 }, { 56, 144, 0 }, { 60, 155, 0 },
		{ 64, 166, 0 }, { 68, 177, 0 }, { 72, 188, 0 }, { 76, 199, 0 }, { 80, 210, 2 }, { 84, 232, 2 },
		{ 88, 254, 2 }, { 92, 276, 2 }, { 96, 298, 2 }, { 100, 320, 2 }, { 124, 352, 2 },
		{ 128, 374, 2 }, { 132, 396, 2 }, { 1256, 5488, 2 }, { 1260, 5510, 2 }, { 1268, 5533, 0 },
		{ 1352, 5584, 0 }, { 1356, 5595, 0 }, { 1360, 5606, 0 }, { 1364, 5618, 2 },
	};
	u32 c = ch & 0xff, a3, x, mode;
	unsigned int i, bv;
	const u8 *knots;
	u8 *sec;
	u32 *v;

#define W(off)	v[(off) / 4]
#define T33_TMOR_WR(s, a, val)	o->reg_write(c, 8, *cfg ? (s) : (a), (val))
	*iadam = 10000;
	*regnum = 1701;
	if (o->module_register(c, 8) < 0)
		o->warn_register();
	info[c] = (u8 *)o->kmalloc(10848, 208);
	if (!info[c])
		o->warn_alloc();
	v = (u32 *)info[c];
	memset(v, 0, 10848);		/* (the vendor's further clears of parts of it change nothing) */
	sec = tparams[c] + 10140;
	W(2732) = (u32)(unsigned long)sec;
	W(10808) = t33_le32(par + 924) << 22 | t33_le32(par + 920) >> 10;
	W(10812) = t33_le32(par + 924) >> 10;
	o->size_update(c);
	W(1372) = W(4);
	W(1380) = W(12);
	W(1368) = W(0);
	W(1376) = W(8);
	a3 = ((W(4) << 8) & 0xffff) | ((W(8) & 0xfff) << 16) | (W(0) & 0xff);
	T33_TMOR_WR(3, 28704, a3);
	T33_TMOR_WR(4, 28708, W(1380) & 0x1f);
	T33_TMOR_WR(1, 28688, 0xffffffffU);
	o->modechange(c);
	o->bin_update(c);

	/* EV (section byte 0x1660 clear) or BV interpolations of the 11-point tables */
	sec = (u8 *)(unsigned long)W(2732);
	if (!sec[5728]) {
		bv = 0;
		x = W(10808);
		knots = sec + 5732;
	} else {
		bv = 1;
		x = W(10816);
		knots = sec + 5640;
	}
	for (i = 0; i < ARRAY_SIZE(ip); i++)
		W(ip[i].dst) = o->intp[bv][ip[i].kind](x, knots, sec + ip[i].src);
	o->lumcurve(c);
	o->satcurve(c);
	o->ic_para(c);

	a3 = t33_tmoR_pair(v, 1384, 1388, 0x7f);
	T33_TMOR_WR(5, 28712, a3);
	a3 = t33_tmoR_pair(v, 1392, 1396, 0x7f);
	T33_TMOR_WR(6, 28716, a3);
	a3 = ((W(1404) << 8) & 0xffff) | ((W(1408) & 0x7f) << 16) | (W(1400) & 0xff) | ((W(1412) & 0x7f) << 24);
	T33_TMOR_WR(7, 28720, a3);
	a3 = t33_tmoR_pair(v, 1420, 1416, 0x1fff);
	T33_TMOR_WR(8, 28724, a3);
	a3 = (W(1424) & 0x7f) | (W(1436) << 24) | ((W(1428) & 0x7f) << 8) | ((W(1432) & 0xff) << 16);
	T33_TMOR_WR(9, 28728, a3);
	a3 = (W(1440) & 0xff) | ((W(1444) << 8) & 0xffff);
	T33_TMOR_WR(10, 28732, a3);
	a3 = (W(1448) & 0xffff) | (W(1452) << 16);
	T33_TMOR_WR(11, 28736, a3);
	a3 = (W(1456) & 0xffff) | (W(1460) << 16);
	T33_TMOR_WR(12, 28740, a3);
	a3 = t33_tmoR_pair(v, 1464, 1468, 0xfff);
	T33_TMOR_WR(13, 28744, a3);
	a3 = ((W(1496) & 0xfff) << 12) | (W(1492) & 0xfff);
	T33_TMOR_WR(18, 28764, a3);
	T33_TMOR_WR(19, 28768, W(1500) & 0x3ff);
	for (i = 0; i < 12; i++)
		T33_TMOR_WR(36 + i, 28776, t33_tmoR_pair(v, 1508 + 8 * i, 1512 + 8 * i, 0x7ff));
	T33_TMOR_WR(48, 28776, W(1604) & 0x7ff);
	a3 = (W(2624) & 0xfff) | (W(2636) << 28) | ((W(2628) & 0xfff) << 16);
	T33_TMOR_WR(25, 28804, a3);
	a3 = ((W(2724) << 4) & 0xff) | (W(2720) & 0xf);
	T33_TMOR_WR(28, 28816, a3);
	T33_TMOR_WR(29, 28820, W(2728) & 1);
	o->sys_write(28820, 1);
	T33_TMOR_WR(1, 28688, 0xffffffffU);

	if (!info[c][10844])
		for (i = 0; i < 8; i++)
			W(1640 + 4 * i) = 256;
	T33_TMOR_WR(14, 28748, t33_tmoR_pair(v, 1476, 1480, 0xfff));
	T33_TMOR_WR(15, 28752, W(1484) & 0xfff);
	T33_TMOR_WR(16, 28756, W(1472) & 3);
	T33_TMOR_WR(17, 28760, (W(1488) & 3) << 4);
	T33_TMOR_WR(20, 28772, W(1504) & 7);
	for (i = 0; i < 4; i++)
		T33_TMOR_WR(50 + i, 28780, t33_tmoR_pair(v, 1608 + 8 * i, 1612 + 8 * i, 0x1fff));
	for (i = 0; i < 4; i++)
		T33_TMOR_WR(55 + i, 28784, t33_tmoR_pair(v, 1640 + 8 * i, 1644 + 8 * i, 0x1ff));
	a3 = (W(1672) & 0xf) | ((W(1676) & 0xf) << 8) | ((W(1680) & 0xf) << 16) | ((W(1684) & 0xf) << 23);
	T33_TMOR_WR(60, 28788, a3);
	a3 = (W(1688) & 0xf) | ((W(1692) & 0xf) << 8) | ((W(1696) & 0xf) << 16);
	T33_TMOR_WR(61, 28788, a3);
	a3 = (W(1700) & 1) | ((W(1704) & 1) << 4) | ((W(1708) & 1) << 8) | ((W(1712) & 0xfff) << 12);
	T33_TMOR_WR(22, 28792, a3);
	T33_TMOR_WR(24, 28800, W(2620) & 1);
	T33_TMOR_WR(26, 28808, W(2632) & 1);
	for (i = 0; i < 10; i++)
		T33_TMOR_WR(77 + i, 28812, t33_tmoR_pair(v, 2640 + 8 * i, 2644 + 8 * i, 0xfff));
	for (i = 0; i < 12; i++)
		T33_TMOR_WR(63 + i, 28796, t33_tmoR_pair(v, 1716 + 8 * i, 1720 + 8 * i, 0xfff));
	T33_TMOR_WR(75, 28796, W(1812) & 0xfff);
	o->ramy(c);
	T33_TMOR_WR(1, 28688, 0xffffffffU);
	*refresh = 1;

	/* the V table buffer and its DMA address */
	W(10832) = (u32)(unsigned long)o->kmalloc(6000, 208);
	W(10836) = W(10832) + 0x80000000U;
	o->ddrv_init(c);
	mode = t33_le32(initpar + 904);
	if (mode >= 3) {
		o->vi23_ctrl(c);
		o->vi23_refresh(c);
	} else {
		o->disv(c);
		o->vi0_refresh(c);
	}
	o->ramy(c);
	o->irq_set(c, 9);
	o->irq_set(c, 42);
	o->irq_set(c, 11);
	return 0;
#undef T33_TMOR_WR
#undef W
}

#endif /* TX_ISP_T33_TMOR_H */
