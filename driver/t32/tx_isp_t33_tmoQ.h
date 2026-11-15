/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 TMO block Q: tisp_tmo_ev_update, tisp_tmo_deinit and
 * tisp_tmo_tmo_soft_process (the per-frame soft process).
 *
 * The block state is the vendor's tmoInfo[ch] byte block; its word at 2732
 * points at the bin section, section + 5640 / 5732 are the BV / EV knot
 * tables.  The register writes go through tisp_reg_write_t33 (list 8; the
 * tmoCfgMode path picks the list slot instead of the register).
 */
#ifndef TX_ISP_T33_TMOQ_H
#define TX_ISP_T33_TMOQ_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_intp.h"
#include "tx_isp_t33_intp2.h"

/* tmoInfo[ch] byte offsets */
#define T33_TMOQ_SEC_OFF	2732	/* bin section pointer */
#define T33_TMOQ_SEL		5728	/* section byte: non-zero selects BV, zero EV */
#define T33_TMOQ_EVKNOT		5732
#define T33_TMOQ_BVKNOT		5640
#define T33_TMOQ_EV		10808
#define T33_TMOQ_BV		10812
#define T33_TMOQ_GAIN		10816
#define T33_TMOQ_DDRV		10832	/* V table buffer pointer */
#define T33_TMOQ_FLAG		10844	/* soft process "done" flag */

/* hardware / other-block callees */
struct t33_tmoq_ops {
	int (*reg_write)(u32 ch, u32 a1, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	void (*sys_write)(u32 addr, u32 val);			/* system_reg_write */
	void (*lumcurve)(u32 ch);				/* tisp_tmo_tmo_lumcurve_ev_interp */
	void (*satcurve)(u32 ch);				/* tisp_tmo_tmo_satcurve_ev_interp */
	void (*ramy)(u32 ch);					/* tisp_tmo_ramY_vi0_reg_refresh */
	void (*ic_para)(u32 ch);				/* func_tmo_ic_para_update */
	void (*kfree)(void *p);					/* private_kfree */
	int (*module_release)(u32 ch, u32 n);			/* tisp_rcfg_module_release */
	void (*release_err)(int ret);				/* printk of the failed release */
};

static inline u32 t33_tmoq_w(const u8 *v, u32 off)
{
	return *(const u32 *)(v + off);
}

/* ((hi & m) << 16) | (lo & m) with the words at byte offsets lo / lo + 4 */
static inline u32 t33_tmoq_pair(const u8 *v, u32 lo, u32 m)
{
	return ((t33_tmoq_w(v, lo + 4) & m) << 16) | (t33_tmoq_w(v, lo) & m);
}

/* tisp_tmo_ev_update: ev (64 bit EV word), gain; interpolations, curves, ic update, register writes */
static inline void t33_tmo_ev_update(u8 *const *info, u8 cfgmode, u8 *refresh, u32 ch, unsigned long long ev,
				     u32 gain, const struct t33_tmoq_ops *o)
{
	u8 *v0 = info[ch];
	u32 c = ch & 0xff;
	u8 *v, *s;
	u32 x, y, val, a3;

	*(u32 *)(v0 + T33_TMOQ_GAIN) = gain;
	*(u32 *)(v0 + T33_TMOQ_EV) = (u32)(ev >> 10);
	*(u32 *)(v0 + T33_TMOQ_BV) = (u32)(ev >> 42);
	v = info[c];
	s = *(u8 **)(v + T33_TMOQ_SEC_OFF);
	x = t33_tmoq_w(v, T33_TMOQ_EV);
	y = t33_tmoq_w(v, T33_TMOQ_GAIN);
	if (!s[T33_TMOQ_SEL]) {
		const u8 *k = s + T33_TMOQ_EVKNOT;

		*(u32 *)(v + 16) = t33_ev_intp_u8(x, k, s + 12);
		*(u32 *)(v + 20) = t33_ev_intp_u8(x, k, s + 23);
		*(u32 *)(v + 24) = t33_ev_intp_u8(x, k, s + 34);
		*(u32 *)(v + 28) = t33_ev_intp_u8(x, k, s + 45);
		*(u32 *)(v + 32) = t33_ev_intp_u8(x, k, s + 56);
		*(u32 *)(v + 36) = t33_ev_intp_u8(x, k, s + 67);
		*(u32 *)(v + 40) = (u32)(int)t33_ev_intp_s8(x, k, s + 78);
		*(u32 *)(v + 44) = (u32)(int)t33_ev_intp_s8(x, k, s + 89);
		*(u32 *)(v + 48) = (u32)t33_ev_intp_u16(x, k, s + 100);
		*(u32 *)(v + 52) = (u32)t33_ev_intp_u16(x, k, s + 122);
		*(u32 *)(v + 56) = t33_ev_intp_u8(x, k, s + 144);
		*(u32 *)(v + 60) = t33_ev_intp_u8(x, k, s + 155);
		*(u32 *)(v + 64) = t33_ev_intp_u8(x, k, s + 166);
		*(u32 *)(v + 68) = t33_ev_intp_u8(x, k, s + 177);
		*(u32 *)(v + 72) = t33_ev_intp_u8(x, k, s + 188);
		*(u32 *)(v + 76) = t33_ev_intp_u8(x, k, s + 199);
		*(u32 *)(v + 80) = (u32)t33_ev_intp_u16(x, k, s + 210);
		*(u32 *)(v + 84) = (u32)t33_ev_intp_u16(x, k, s + 232);
		*(u32 *)(v + 88) = (u32)t33_ev_intp_u16(x, k, s + 254);
		*(u32 *)(v + 92) = (u32)t33_ev_intp_u16(x, k, s + 276);
		*(u32 *)(v + 96) = (u32)t33_ev_intp_u16(x, k, s + 298);
		*(u32 *)(v + 100) = (u32)t33_ev_intp_u16(x, k, s + 320);
		*(u32 *)(v + 124) = (u32)t33_ev_intp_u16(x, k, s + 352);
		*(u32 *)(v + 128) = (u32)t33_ev_intp_u16(x, k, s + 374);
		*(u32 *)(v + 132) = (u32)t33_ev_intp_u16(x, k, s + 396);
		*(u32 *)(v + 1256) = (u32)t33_ev_intp_u16(x, k, s + 5488);
		*(u32 *)(v + 1260) = (u32)t33_ev_intp_u16(x, k, s + 5510);
		*(u32 *)(v + 1268) = t33_ev_intp_u8(x, k, s + 5533);
		*(u32 *)(v + 1352) = t33_ev_intp_u8(x, k, s + 5584);
		*(u32 *)(v + 1356) = t33_ev_intp_u8(x, k, s + 5595);
		*(u32 *)(v + 1360) = t33_ev_intp_u8(x, k, s + 5606);
		*(u32 *)(v + 1364) = (u32)t33_ev_intp_u16(x, k, s + 5618);
	} else {
		const u8 *k = s + T33_TMOQ_BVKNOT;

		*(u32 *)(v + 16) = t33_bv_intp_u8(y, k, s + 12);
		*(u32 *)(v + 20) = t33_bv_intp_u8(y, k, s + 23);
		*(u32 *)(v + 24) = t33_bv_intp_u8(y, k, s + 34);
		*(u32 *)(v + 28) = t33_bv_intp_u8(y, k, s + 45);
		*(u32 *)(v + 32) = t33_bv_intp_u8(y, k, s + 56);
		*(u32 *)(v + 36) = t33_bv_intp_u8(y, k, s + 67);
		*(u32 *)(v + 40) = (u32)(int)t33_bv_intp_s8(y, k, s + 78);
		*(u32 *)(v + 44) = (u32)(int)t33_bv_intp_s8(y, k, s + 89);
		*(u32 *)(v + 48) = (u32)t33_bv_intp_u16(y, k, s + 100);
		*(u32 *)(v + 52) = (u32)t33_bv_intp_u16(y, k, s + 122);
		*(u32 *)(v + 56) = t33_bv_intp_u8(y, k, s + 144);
		*(u32 *)(v + 60) = t33_bv_intp_u8(y, k, s + 155);
		*(u32 *)(v + 64) = t33_bv_intp_u8(y, k, s + 166);
		*(u32 *)(v + 68) = t33_bv_intp_u8(y, k, s + 177);
		*(u32 *)(v + 72) = t33_bv_intp_u8(y, k, s + 188);
		*(u32 *)(v + 76) = t33_bv_intp_u8(y, k, s + 199);
		*(u32 *)(v + 80) = (u32)t33_bv_intp_u16(y, k, s + 210);
		*(u32 *)(v + 84) = (u32)t33_bv_intp_u16(y, k, s + 232);
		*(u32 *)(v + 88) = (u32)t33_bv_intp_u16(y, k, s + 254);
		*(u32 *)(v + 92) = (u32)t33_bv_intp_u16(y, k, s + 276);
		*(u32 *)(v + 96) = (u32)t33_bv_intp_u16(y, k, s + 298);
		*(u32 *)(v + 100) = (u32)t33_bv_intp_u16(y, k, s + 320);
		*(u32 *)(v + 124) = (u32)t33_bv_intp_u16(y, k, s + 352);
		*(u32 *)(v + 128) = (u32)t33_bv_intp_u16(y, k, s + 374);
		*(u32 *)(v + 132) = (u32)t33_bv_intp_u16(y, k, s + 396);
		*(u32 *)(v + 1256) = (u32)t33_bv_intp_u16(y, k, s + 5488);
		*(u32 *)(v + 1260) = (u32)t33_bv_intp_u16(y, k, s + 5510);
		*(u32 *)(v + 1268) = t33_bv_intp_u8(y, k, s + 5533);
		*(u32 *)(v + 1352) = t33_bv_intp_u8(y, k, s + 5584);
		*(u32 *)(v + 1356) = t33_bv_intp_u8(y, k, s + 5595);
		*(u32 *)(v + 1360) = t33_bv_intp_u8(y, k, s + 5606);
		*(u32 *)(v + 1364) = (u32)t33_bv_intp_u16(y, k, s + 5618);
	}
	o->lumcurve(ch);
	o->satcurve(ch);
	o->ic_para(c);
	v = info[c];
	a3 = ((t33_tmoq_w(v, 1388) & 0x7f) << 16) | (t33_tmoq_w(v, 1384) & 0x7f);
	o->reg_write(c, 8, cfgmode ? 5 : 28712, a3);
	a3 = ((t33_tmoq_w(v, 1396) & 0x7f) << 16) | (t33_tmoq_w(v, 1392) & 0x7f);
	o->reg_write(c, 8, cfgmode ? 6 : 28716, a3);
	a3 = ((t33_tmoq_w(v, 1404) << 8) & 0xffff) | ((t33_tmoq_w(v, 1408) & 0x7f) << 16) |
	     (t33_tmoq_w(v, 1400) & 0xff) | ((t33_tmoq_w(v, 1412) & 0x7f) << 24);
	o->reg_write(c, 8, cfgmode ? 7 : 28720, a3);
	a3 = ((t33_tmoq_w(v, 1416) & 0x1fff) << 16) | (t33_tmoq_w(v, 1420) & 0x1fff);
	o->reg_write(c, 8, cfgmode ? 8 : 28724, a3);
	a3 = (t33_tmoq_w(v, 1424) & 0x7f) | (t33_tmoq_w(v, 1436) << 24) | ((t33_tmoq_w(v, 1428) & 0x7f) << 8) |
	     ((t33_tmoq_w(v, 1432) & 0xff) << 16);
	o->reg_write(c, 8, cfgmode ? 9 : 28728, a3);
	a3 = ((t33_tmoq_w(v, 1444) << 8) & 0xffff) | (t33_tmoq_w(v, 1440) & 0xff);
	o->reg_write(c, 8, cfgmode ? 10 : 28732, a3);
	a3 = (t33_tmoq_w(v, 1448) & 0xffff) | (t33_tmoq_w(v, 1452) << 16);
	o->reg_write(c, 8, cfgmode ? 11 : 28736, a3);
	a3 = (t33_tmoq_w(v, 1456) & 0xffff) | (t33_tmoq_w(v, 1460) << 16);
	o->reg_write(c, 8, cfgmode ? 12 : 28740, a3);
	a3 = ((t33_tmoq_w(v, 1468) & 0xfff) << 16) | (t33_tmoq_w(v, 1464) & 0xfff);
	o->reg_write(c, 8, cfgmode ? 13 : 28744, a3);
	a3 = ((t33_tmoq_w(v, 1496) & 0xfff) << 12) | (t33_tmoq_w(v, 1492) & 0xfff);
	o->reg_write(c, 8, cfgmode ? 18 : 28764, a3);
	a3 = t33_tmoq_w(v, 1500) & 0x3ff;
	o->reg_write(c, 8, cfgmode ? 19 : 28768, a3);
	o->reg_write(c, 8, cfgmode ? 36 : 28776, t33_tmoq_pair(v, 1508, 0x7ff) & ~0U);
	o->reg_write(c, 8, cfgmode ? 37 : 28776, ((t33_tmoq_w(v, 1520) & 0x7ff) << 16) | (t33_tmoq_w(v, 1516) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 38 : 28776, ((t33_tmoq_w(v, 1528) & 0x7ff) << 16) | (t33_tmoq_w(v, 1524) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 39 : 28776, ((t33_tmoq_w(v, 1536) & 0x7ff) << 16) | (t33_tmoq_w(v, 1532) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 40 : 28776, ((t33_tmoq_w(v, 1544) & 0x7ff) << 16) | (t33_tmoq_w(v, 1540) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 41 : 28776, ((t33_tmoq_w(v, 1552) & 0x7ff) << 16) | (t33_tmoq_w(v, 1548) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 42 : 28776, ((t33_tmoq_w(v, 1560) & 0x7ff) << 16) | (t33_tmoq_w(v, 1556) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 43 : 28776, ((t33_tmoq_w(v, 1568) & 0x7ff) << 16) | (t33_tmoq_w(v, 1564) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 44 : 28776, ((t33_tmoq_w(v, 1576) & 0x7ff) << 16) | (t33_tmoq_w(v, 1572) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 45 : 28776, ((t33_tmoq_w(v, 1584) & 0x7ff) << 16) | (t33_tmoq_w(v, 1580) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 46 : 28776, ((t33_tmoq_w(v, 1592) & 0x7ff) << 16) | (t33_tmoq_w(v, 1588) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 47 : 28776, ((t33_tmoq_w(v, 1600) & 0x7ff) << 16) | (t33_tmoq_w(v, 1596) & 0x7ff));
	o->reg_write(c, 8, cfgmode ? 48 : 28776, t33_tmoq_w(v, 1604) & 0x7ff);
	val = t33_tmoq_w(v, 2624) & 0xfff;
	a3 = val | (t33_tmoq_w(v, 2636) << 28) | ((t33_tmoq_w(v, 2628) & 0xfff) << 16);
	o->reg_write(c, 8, cfgmode ? 25 : 28804, a3);
	a3 = ((t33_tmoq_w(v, 2724) << 4) & 0xff) | (t33_tmoq_w(v, 2720) & 0xf);
	o->reg_write(c, 8, cfgmode ? 28 : 28816, a3);
	a3 = t33_tmoq_w(v, 2728) & 1;
	o->reg_write(c, 8, cfgmode ? 29 : 28820, a3);
	o->sys_write(28820, 1);
	o->reg_write(c, 8, cfgmode ? 1 : 28688, 0xffffffffU);
	o->ramy(ch);
	o->reg_write(ch, 8, cfgmode ? 1 : 28688, 0xffffffffU);
	*refresh = 1;
}

/* tisp_tmo_deinit */
static inline void t33_tmo_deinit(u8 **info, u8 *mulsensor, const struct t33_tmoq_ops *o, u32 ch)
{
	u32 c = ch & 0xff;
	u8 *v = info[c];
	int r;

	if (v) {
		if (*(void **)(v + T33_TMOQ_DDRV)) {
			o->kfree(*(void **)(v + T33_TMOQ_DDRV));
			*(void **)(info[c] + T33_TMOQ_DDRV) = 0;
		}
		o->kfree(info[c]);
		info[c] = 0;
	}
	*mulsensor = 0;
	r = o->module_release(c, 8);
	if (r < 0)
		o->release_err(r);
}

/* tisp_tmo_tmo_soft_process */
static inline void t33_tmo_soft_process(u8 *const *info, u8 cfgmode, u8 *refresh, const struct t33_tmoq_ops *o,
					u32 ch)
{
	u8 *v = info[ch];
	u32 c = ch & 0xff, a3;
	unsigned int i;

	if (v[T33_TMOQ_FLAG])
		return;
	v[T33_TMOQ_FLAG] = 1;
	o->ic_para(c);
	v = info[c];
	if (!v[T33_TMOQ_FLAG]) {
		for (i = 0; i < 8; i++)
			*(u32 *)(v + 1640 + 4 * i) = 256;
	}
	a3 = ((t33_tmoq_w(v, 1480) & 0xfff) << 16) | (t33_tmoq_w(v, 1476) & 0xfff);
	o->reg_write(c, 8, cfgmode ? 14 : 28748, a3);
	a3 = t33_tmoq_w(v, 1484) & 0xfff;
	o->reg_write(c, 8, cfgmode ? 15 : 28752, a3);
	a3 = t33_tmoq_w(v, 1472) & 3;
	o->reg_write(c, 8, cfgmode ? 16 : 28756, a3);
	a3 = (t33_tmoq_w(v, 1488) & 3) << 4;
	o->reg_write(c, 8, cfgmode ? 17 : 28760, a3);
	a3 = t33_tmoq_w(v, 1504) & 7;
	o->reg_write(c, 8, cfgmode ? 20 : 28772, a3);
	o->reg_write(c, 8, cfgmode ? 50 : 28780, t33_tmoq_pair(v, 1608, 0x1fff) & ~0U);
	o->reg_write(c, 8, cfgmode ? 51 : 28780, ((t33_tmoq_w(v, 1620) & 0x1fff) << 16) | (t33_tmoq_w(v, 1616) & 0x1fff));
	o->reg_write(c, 8, cfgmode ? 52 : 28780, ((t33_tmoq_w(v, 1628) & 0x1fff) << 16) | (t33_tmoq_w(v, 1624) & 0x1fff));
	o->reg_write(c, 8, cfgmode ? 53 : 28780, ((t33_tmoq_w(v, 1636) & 0x1fff) << 16) | (t33_tmoq_w(v, 1632) & 0x1fff));
	o->reg_write(c, 8, cfgmode ? 55 : 28784, ((t33_tmoq_w(v, 1644) & 0x1ff) << 16) | (t33_tmoq_w(v, 1640) & 0x1ff));
	o->reg_write(c, 8, cfgmode ? 56 : 28784, ((t33_tmoq_w(v, 1652) & 0x1ff) << 16) | (t33_tmoq_w(v, 1648) & 0x1ff));
	o->reg_write(c, 8, cfgmode ? 57 : 28784, ((t33_tmoq_w(v, 1660) & 0x1ff) << 16) | (t33_tmoq_w(v, 1656) & 0x1ff));
	o->reg_write(c, 8, cfgmode ? 58 : 28784, ((t33_tmoq_w(v, 1668) & 0x1ff) << 16) | (t33_tmoq_w(v, 1664) & 0x1ff));
	a3 = ((t33_tmoq_w(v, 1676) & 0xf) << 8) | ((t33_tmoq_w(v, 1680) & 0xf) << 16) | (t33_tmoq_w(v, 1672) & 0xf) |
	     ((t33_tmoq_w(v, 1684) & 0xf) << 23);
	o->reg_write(c, 8, cfgmode ? 60 : 28788, a3);
	a3 = ((t33_tmoq_w(v, 1692) & 0xf) << 8) | ((t33_tmoq_w(v, 1696) & 0xf) << 16) | (t33_tmoq_w(v, 1688) & 0xf);
	o->reg_write(c, 8, cfgmode ? 61 : 28788, a3);
	a3 = ((t33_tmoq_w(v, 1704) & 1) << 4) | ((t33_tmoq_w(v, 1708) & 1) << 8) | (t33_tmoq_w(v, 1700) & 1) |
	     ((t33_tmoq_w(v, 1712) & 0xfff) << 12);
	o->reg_write(c, 8, cfgmode ? 22 : 28792, a3);
	a3 = t33_tmoq_w(v, 2620) & 1;
	o->reg_write(c, 8, cfgmode ? 24 : 28800, a3);
	a3 = t33_tmoq_w(v, 2632) & 1;
	o->reg_write(c, 8, cfgmode ? 26 : 28808, a3);
	o->reg_write(c, 8, cfgmode ? 77 : 28812, t33_tmoq_pair(v, 2640, 0xfff) & ~0U);
	o->reg_write(c, 8, cfgmode ? 78 : 28812, ((t33_tmoq_w(v, 2652) & 0xfff) << 16) | (t33_tmoq_w(v, 2648) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 79 : 28812, ((t33_tmoq_w(v, 2660) & 0xfff) << 16) | (t33_tmoq_w(v, 2656) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 80 : 28812, ((t33_tmoq_w(v, 2668) & 0xfff) << 16) | (t33_tmoq_w(v, 2664) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 81 : 28812, ((t33_tmoq_w(v, 2676) & 0xfff) << 16) | (t33_tmoq_w(v, 2672) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 82 : 28812, ((t33_tmoq_w(v, 2684) & 0xfff) << 16) | (t33_tmoq_w(v, 2680) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 83 : 28812, ((t33_tmoq_w(v, 2692) & 0xfff) << 16) | (t33_tmoq_w(v, 2688) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 84 : 28812, ((t33_tmoq_w(v, 2700) & 0xfff) << 16) | (t33_tmoq_w(v, 2696) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 85 : 28812, ((t33_tmoq_w(v, 2708) & 0xfff) << 16) | (t33_tmoq_w(v, 2704) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 86 : 28812, ((t33_tmoq_w(v, 2716) & 0xfff) << 16) | (t33_tmoq_w(v, 2712) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 63 : 28796, ((t33_tmoq_w(v, 1720) & 0xfff) << 16) | (t33_tmoq_w(v, 1716) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 64 : 28796, ((t33_tmoq_w(v, 1728) & 0xfff) << 16) | (t33_tmoq_w(v, 1724) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 65 : 28796, ((t33_tmoq_w(v, 1736) & 0xfff) << 16) | (t33_tmoq_w(v, 1732) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 66 : 28796, ((t33_tmoq_w(v, 1744) & 0xfff) << 16) | (t33_tmoq_w(v, 1740) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 67 : 28796, ((t33_tmoq_w(v, 1752) & 0xfff) << 16) | (t33_tmoq_w(v, 1748) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 68 : 28796, ((t33_tmoq_w(v, 1760) & 0xfff) << 16) | (t33_tmoq_w(v, 1756) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 69 : 28796, ((t33_tmoq_w(v, 1768) & 0xfff) << 16) | (t33_tmoq_w(v, 1764) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 70 : 28796, ((t33_tmoq_w(v, 1776) & 0xfff) << 16) | (t33_tmoq_w(v, 1772) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 71 : 28796, ((t33_tmoq_w(v, 1784) & 0xfff) << 16) | (t33_tmoq_w(v, 1780) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 72 : 28796, ((t33_tmoq_w(v, 1792) & 0xfff) << 16) | (t33_tmoq_w(v, 1788) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 73 : 28796, ((t33_tmoq_w(v, 1800) & 0xfff) << 16) | (t33_tmoq_w(v, 1796) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 74 : 28796, ((t33_tmoq_w(v, 1808) & 0xfff) << 16) | (t33_tmoq_w(v, 1804) & 0xfff));
	o->reg_write(c, 8, cfgmode ? 75 : 28796, t33_tmoq_w(v, 1812) & 0xfff);
	o->reg_write(ch, 8, cfgmode ? 1 : 28688, 0xffffffffU);
	*refresh = 1;
}

#endif /* TX_ISP_T33_TMOQ_H */
