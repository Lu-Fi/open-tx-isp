/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) TMO day/night switch: tisp_tmo_switch_para.
 *
 * Refreshes the block state from the IQ section (EV or BV interpolations of
 * the 11-point tables), calls the curve / register-copy helpers and writes
 * the block registers (tisp_reg_write_t33 on module 8: slot index in the
 * list mode, register address otherwise).  The helpers and register
 * accessors are reached through t33_tmosw_ops.
 */
#ifndef TX_ISP_T33_TMOSW_H
#define TX_ISP_T33_TMOSW_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_tmo.h"

struct t33_tmosw_ops {
	void (*bin_update)(u32 ch);		/* func_bin_para_update */
	void (*lum_curve)(u32 ch);		/* tisp_tmo_tmo_lumcurve_ev_interp */
	void (*sat_curve)(u32 ch);		/* tisp_tmo_tmo_satcurve_ev_interp */
	void (*ic_update)(u32 ch);		/* func_tmo_ic_para_update */
	void (*y_refresh)(u32 ch);		/* tisp_tmo_ramY_vi0_reg_refresh */
	void (*reg_write)(u32 ch, u32 blk, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	void (*sys_write)(u32 reg, u32 val);	/* system_reg_write */
};

#define T33_TMOSW_SEC_OFF	10140U

static inline u32 t33_tmosw_pair(const u32 *v, u32 lo, u32 hi, u32 m)
{
	return ((v[hi / 4] & m) << 16) | (v[lo / 4] & m);
}

static inline void t33_tmosw_wr(const struct t33_tmosw_ops *o, u32 ch, u8 cfg, u32 slot, u32 addr, u32 val)
{
	o->reg_write(ch, 8, cfg ? slot : addr, val);
}

/* tisp_tmo_switch_para: tp = tparamsP[ch], v = tmoInfo[ch] (words), cfg = tmoCfgMode */
static inline int t33_tmo_switch_para(const struct t33_tmosw_ops *o, u32 ch, u8 *tp, u32 *v, u8 cfg,
				      u8 *refresh)
{
	static const struct { u16 dst, src; u8 bits, sgn; } ip[] = {
		{ 16, 12, 8, 0 }, { 20, 23, 8, 0 }, { 24, 34, 8, 0 }, { 28, 45, 8, 0 },
		{ 32, 56, 8, 0 }, { 36, 67, 8, 0 }, { 40, 78, 8, 1 }, { 44, 89, 8, 1 },
		{ 48, 100, 16, 0 }, { 52, 122, 16, 0 }, { 56, 144, 8, 0 }, { 60, 155, 8, 0 },
		{ 64, 166, 8, 0 }, { 68, 177, 8, 0 }, { 72, 188, 8, 0 }, { 76, 199, 8, 0 },
		{ 80, 210, 16, 0 }, { 84, 232, 16, 0 }, { 88, 254, 16, 0 }, { 92, 276, 16, 0 },
		{ 96, 298, 16, 0 }, { 100, 320, 16, 0 }, { 124, 352, 16, 0 }, { 128, 374, 16, 0 },
		{ 132, 396, 16, 0 }, { 1256, 5488, 16, 0 }, { 1260, 5510, 16, 0 }, { 1268, 5533, 8, 0 },
		{ 1352, 5584, 8, 0 }, { 1356, 5595, 8, 0 }, { 1360, 5606, 8, 0 }, { 1364, 5618, 16, 0 },
	};
	const u8 *sec = tp + T33_TMOSW_SEC_OFF;
	const u8 *e;
	u32 x;
	unsigned int i;
	int isbv;

#define W(o_)	v[(o_) / 4]
	W(2732) = (u32)(unsigned long)sec;
	o->bin_update(ch);
	isbv = sec[0x1660] != 0;
	x = isbv ? W(10816) : W(10808);
	e = sec + (isbv ? 5640 : 5732);
	for (i = 0; i < ARRAY_SIZE(ip); i++)
		W(ip[i].dst) = (u32)t33_tmo_intp(x, e, sec + ip[i].src, ip[i].bits, ip[i].sgn, isbv);
	o->lum_curve(ch);
	o->sat_curve(ch);
	o->ic_update(ch);

	t33_tmosw_wr(o, ch, cfg, 5, 28712, t33_tmosw_pair(v, 1384, 1388, 0x7f));
	t33_tmosw_wr(o, ch, cfg, 6, 28716, t33_tmosw_pair(v, 1392, 1396, 0x7f));
	t33_tmosw_wr(o, ch, cfg, 7, 28720, ((W(1404) << 8) & 0xffff) | ((W(1408) & 0x7f) << 16) |
		     (W(1400) & 0xff) | ((W(1412) & 0x7f) << 24));
	t33_tmosw_wr(o, ch, cfg, 8, 28724, t33_tmosw_pair(v, 1420, 1416, 0x1fff));
	t33_tmosw_wr(o, ch, cfg, 9, 28728, (W(1424) & 0x7f) | (W(1436) << 24) | ((W(1428) & 0x7f) << 8) |
		     ((W(1432) & 0xff) << 16));
	t33_tmosw_wr(o, ch, cfg, 10, 28732, (W(1440) & 0xff) | ((W(1444) << 8) & 0xffff));
	t33_tmosw_wr(o, ch, cfg, 11, 28736, (W(1448) & 0xffff) | (W(1452) << 16));
	t33_tmosw_wr(o, ch, cfg, 12, 28740, (W(1456) & 0xffff) | (W(1460) << 16));
	t33_tmosw_wr(o, ch, cfg, 13, 28744, t33_tmosw_pair(v, 1464, 1468, 0xfff));
	t33_tmosw_wr(o, ch, cfg, 18, 28764, ((W(1496) & 0xfff) << 12) | (W(1492) & 0xfff));
	t33_tmosw_wr(o, ch, cfg, 19, 28768, W(1500) & 0x3ff);
	for (i = 0; i < 12; i++)
		t33_tmosw_wr(o, ch, cfg, 36 + i, 28776, t33_tmosw_pair(v, 1508 + 8 * i, 1512 + 8 * i, 0x7ff));
	t33_tmosw_wr(o, ch, cfg, 48, 28776, W(1604) & 0x7ff);
	t33_tmosw_wr(o, ch, cfg, 25, 28804, (W(2624) & 0xfff) | (W(2636) << 28) | ((W(2628) & 0xfff) << 16));
	t33_tmosw_wr(o, ch, cfg, 28, 28816, ((W(2724) << 4) & 0xff) | (W(2720) & 0xf));
	t33_tmosw_wr(o, ch, cfg, 29, 28820, W(2728) & 1);
	o->sys_write(28820, 1);
	t33_tmosw_wr(o, ch, cfg, 1, 28688, 0xffffffffU);

	if (!(W(10844) & 0xff))
		for (i = 0; i < 8; i++)
			W(1640 + 4 * i) = 0x100;
	t33_tmosw_wr(o, ch, cfg, 14, 28748, t33_tmosw_pair(v, 1476, 1480, 0xfff));
	t33_tmosw_wr(o, ch, cfg, 15, 28752, W(1484) & 0xfff);
	t33_tmosw_wr(o, ch, cfg, 16, 28756, W(1472) & 3);
	t33_tmosw_wr(o, ch, cfg, 17, 28760, (W(1488) & 3) << 4);
	t33_tmosw_wr(o, ch, cfg, 20, 28772, W(1504) & 7);
	for (i = 0; i < 4; i++)
		t33_tmosw_wr(o, ch, cfg, 50 + i, 28780, t33_tmosw_pair(v, 1608 + 8 * i, 1612 + 8 * i, 0x1fff));
	for (i = 0; i < 4; i++)
		t33_tmosw_wr(o, ch, cfg, 55 + i, 28784, t33_tmosw_pair(v, 1640 + 8 * i, 1644 + 8 * i, 0x1ff));
	t33_tmosw_wr(o, ch, cfg, 60, 28788, (W(1672) & 0xf) | ((W(1676) & 0xf) << 8) |
		     ((W(1680) & 0xf) << 16) | ((W(1684) & 0xf) << 23));
	t33_tmosw_wr(o, ch, cfg, 61, 28788, (W(1688) & 0xf) | ((W(1692) & 0xf) << 8) | ((W(1696) & 0xf) << 16));
	t33_tmosw_wr(o, ch, cfg, 22, 28792, (W(1700) & 1) | ((W(1704) & 1) << 4) | ((W(1708) & 1) << 8) |
		     ((W(1712) & 0xfff) << 12));
	t33_tmosw_wr(o, ch, cfg, 24, 28800, W(2620) & 1);
	t33_tmosw_wr(o, ch, cfg, 26, 28808, W(2632) & 1);
	for (i = 0; i < 10; i++)
		t33_tmosw_wr(o, ch, cfg, 77 + i, 28812, t33_tmosw_pair(v, 2640 + 8 * i, 2644 + 8 * i, 0xfff));
	for (i = 0; i < 12; i++)
		t33_tmosw_wr(o, ch, cfg, 63 + i, 28796, t33_tmosw_pair(v, 1716 + 8 * i, 1720 + 8 * i, 0xfff));
	t33_tmosw_wr(o, ch, cfg, 75, 28796, W(1812) & 0xfff);
	o->y_refresh(ch);
	t33_tmosw_wr(o, ch, cfg, 1, 28688, 0xffffffffU);
	*refresh = 1;
#undef W
	return 0;
}

#endif /* TX_ISP_T33_TMOSW_H */
