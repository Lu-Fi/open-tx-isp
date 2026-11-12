/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) AWB statistics set-up (module list 29) and white balance
 * gains (AWBG, module list 7), as tisp_awb_init leaves them on the AWB
 * section (IQ bin section 2):
 *  - tisp_awb_malloc_info: four 0x7400 byte zone statistics buffers, 4 - 1
 *    in use (slots 11..15);
 *  - tisp_awb_params_refresh: zone counts from 0xc72 / 0xc76, raised until
 *    a zone is at most 0x7e pixel pairs wide / high; the zone sizes go to
 *    the section words 0xc78.. (x) and 0xc96.. (y) as the vendor keeps them;
 *  - tisp_awb_set_hardware_param: zone start / count, the statistics
 *    control word, the zone size, tisp_awb_set_regional_threshold and
 *    tisp_awb_set_lum_th_freq at the AWB's interpolation point (word
 *    0x10434 of the AWB state: 0x400 after init, i.e. point 0, 0x400/65536);
 *  - tisp_awb_set_gain on the section's start gains: R / B from 0x18 / 0x1c
 *    scaled by 0x34 / 0x38 (fix_point_mult2_32, Q from 0xcea, shift 0xcec),
 *    the CT offset pair for the start CT 0x2c (0xbf8.. thresholds 0xc10 /
 *    0xc14), clamped to 0x3fff.  The R word carries 0x4000000; the B word
 *    does not: tisp_awb_gain_reg sets the bit in both, but tisp_awb_set_gain
 *    passes the B word through a 16 bit local (vendor module: lhu of the
 *    gain_reg result's second word before tisp_reg_write_t33(7, 4)).
 *    Vendor trace 0x6044 = 0x740 confirms, so the vendor value is kept.
 * QEMU trace of the vendor module (openimp #389): lists 29 and 7 word-equal
 * on day / night / cust / dw70; 0x160fc (slot 0x2c) has no store in the
 * vendor code (stale list memory, 0x2480 / 0x80509290 in the traces).
 */
#ifndef TX_ISP_T33_AWB_H
#define TX_ISP_T33_AWB_H

#include "tx_isp_t33_blk.h"

#define T33_MOD_AWB		29
#define T33_MOD_AWBG		7
#define T33_SEC_AWB		2
#define T33_AWB_SEC_MIN		0x1598U		/* highest table read: 0x1582 + 22 */
#define T33_AWB_STAT_ONE	0x7400U		/* one zone statistics buffer */
#define T33_AWB_STAT_BYTES	(4 * T33_AWB_STAT_ONE)
#define T33_AWB_INIT_TGAIN	0x400U		/* AWB state 0x10434 after tisp_awb_init */
#define T33_AWB_CT_OFF_INIT	0x400U		/* AWB state 0x7460 / 0x7464 after init */

static const struct t33_tcmd t33_awb_tmpl[] = {
	{ 0x16010, 1, 0 }, { 0x16040, 2, 0 }, { 0x16064, 39, 0 },
};

static const struct t33_tcmd t33_awbg_tmpl[] = {
	{ 0x6010, 1, 0 }, { 0x6040, 2, 0 },
};

static inline u32 t33_awb_s16(const u8 *sec, u32 o)
{
	return sec[o] | (u32)sec[o + 1] << 8;
}

/* fix_point_mult2_32: (a * b) >> q in 32 bit pieces, as the vendor does */
static inline u32 t33_fpm2_32(u32 q, u32 a, u32 b)
{
	u32 m = 0xffffffffU >> ((0U - q) & 31), ah = a >> (q & 31), bh = b >> (q & 31);
	u32 al = a & m, bl = b & m;

	return al * bh + ah * bl + ((ah * bh) << (q & 31)) + ((al * bl) >> (q & 31));
}

/* zone count from c, raised until ((size - start + 1) / 2) / count <= 0x7e; *zs = that zone size */
static inline u32 t33_awb_zones(u32 size, u32 start, u32 c, u32 *zs)
{
	u32 z;

	if (!c)
		c = 1;
	for (;;) {
		z = (u32)(((int)(size - start + 1) / 2) / (int)c);
		if ((z & 0xffff) <= 0x7e)
			break;
		c = (c + 1) & 0xffff;
	}
	*zs = z & 0xffff;
	return c;
}

/* word of two interpolated u16 tables: lo | hi << 16 */
static inline u32 t33_awb_ip2(const u8 *sec, u32 lo, u32 hi, u32 i, u32 f)
{
	return t32_tp_intp_u16(i, f, sec + lo) | (u32)t32_tp_intp_u16(i, f, sec + hi) << 16;
}

/*
 * AWB list for a w x h picture from the AWB section sec at interpolation
 * point tgain (T33_AWB_INIT_TGAIN after init); stat: the physical address
 * of the T33_AWB_STAT_BYTES statistics buffers.  Returns the list length.
 */
static inline int t33_awb_init(const u8 *sec, u32 w, u32 h, u32 tgain, u32 stat, u32 *l, u32 cap)
{
	int len = t33_rcfg_tmpl_init(l, cap, t33_awb_tmpl, ARRAY_SIZE(t33_awb_tmpl));
	u32 y0 = t33_awb_s16(sec, 0xc70), x0 = t33_awb_s16(sec, 0xc74), zx, zy, nx, ny, k;
	u32 i = tgain >> 16, f = tgain & 0xffff;

	if (len <= 0)
		return len;
	nx = t33_awb_zones(w, x0, t33_awb_s16(sec, 0xc76), &zx);
	ny = t33_awb_zones(h, y0, t33_awb_s16(sec, 0xc72), &zy);
	/* tisp_awb_malloc_info */
	for (k = 0; k < 4; k++)
		T33_SW(l, 11 + k, stat + T33_AWB_STAT_ONE * k);
	/* tisp_awb_set_hardware_param */
	T33_SW(l, 3, (x0 & 0xfff) << 16 | (y0 & 0xfff));
	T33_SW(l, 4, (ny & 0x3f) << 8 | (nx & 0x3f));
	T33_SW(l, 0xf, 3 | 0x1010100);
	T33_SW(l, 0x2a, (zy & 0x7f) << 16 | (zx & 0x7f));
	/* tisp_awb_set_regional_threshold */
	T33_SW(l, 6, t33_awb_ip2(sec, 0x11ec, 0x1202, i, f));
	T33_SW(l, 7, t33_awb_ip2(sec, 0x1218, 0x122e, i, f));
	T33_SW(l, 8, t33_awb_ip2(sec, 0x1244, 0x125a, i, f));
	T33_SW(l, 9, t33_awb_ip2(sec, 0x1270, 0x1286, i, f));
	/* tisp_awb_set_lum_th_freq */
	T33_SW(l, 10, t32_tp_intp_u16(i, f, sec + 0x129c) | (u32)t32_tp_intp_u16(i, f, sec + 0x12b2) << 8 |
		      (u32)t32_tp_intp_u16(i, f, sec + 0x12c8) << 16 | t33_awb_s16(sec, 0xcca) << 24);
	T33_SW(l, 0x28, (t33_awb_s16(sec, 0xcd0) & 3) << 16 | (t33_awb_s16(sec, 0xcce) & 1));
	T33_SW(l, 0x29, (t32_tp_intp_u16(i, f, sec + 0x12de) & 0xf) | (t32_tp_intp_u16(i, f, sec + 0x12f4) & 0xf) << 8 |
			(t32_tp_intp_u16(i, f, sec + 0x130a) & 0xf) << 16 |
			(u32)(t32_tp_intp_u16(i, f, sec + 0x1320) & 0xf) << 24);
	T33_SW(l, 0x2b, (t32_tp_intp_u16(i, f, sec + 0x1336) & 0xf) | (t32_tp_intp_u16(i, f, sec + 0x134c) & 0xf) << 8 |
			(t32_tp_intp_u16(i, f, sec + 0x1362) & 0xf) << 16 |
			(u32)(t32_tp_intp_u16(i, f, sec + 0x1378) & 0xf) << 24);
	T33_SW(l, 1, 1);
	return len;
}

/*
 * tisp_awb_set_gain's start path (AWB mode 0, no manual gain): the section's
 * start R / B gains, *r / *b in the AWBG word format.  last_ct: the CT of the
 * previous run (0 after init); the CT offsets change only on a 200 K step.
 */
static inline void t33_awb_start_gain(const u8 *sec, u32 last_ct, u32 *r, u32 *b)
{
	u32 q = t33_awb_s16(sec, 0xcea), s = t33_awb_s16(sec, 0xcec), ct = t33_le32(sec + 0x2c);
	u32 rr, bb, offr = T33_AWB_CT_OFF_INIT, offb = T33_AWB_CT_OFF_INIT, rnd = 1U << ((s - 1) & 31);

	rr = (t33_fpm2_32(q, t33_le32(sec + 0x18) << (s & 31), t33_le32(sec + 0x34) << ((q - 10) & 31)) + rnd) >>
	     (s & 31);
	bb = (t33_fpm2_32(q, t33_le32(sec + 0x1c) << (s & 31), t33_le32(sec + 0x38) << ((q - 10) & 31)) + rnd) >>
	     (s & 31);
	if ((ct > last_ct ? ct - last_ct : last_ct - ct) >= 200) {
		if (ct >= t33_le32(sec + 0xc14)) {
			offr = t33_le32(sec + 0xbf8);
			offb = t33_le32(sec + 0xbfc);
		} else if (t33_le32(sec + 0xc10) < ct) {
			offr = t33_le32(sec + 0xc00);
			offb = t33_le32(sec + 0xc04);
		} else {
			offr = t33_le32(sec + 0xc08);
			offb = t33_le32(sec + 0xc0c);
		}
	}
	rr = rr * 4 + offr - 0x400;
	bb = bb * 4 + offb - 0x400;
	*r = (rr > 0x3fff ? 0x3fff : rr) | 0x4000000;
	*b = (bb > 0x3fff ? 0x3fff : bb) & 0xffff;	/* the vendor's 16 bit B word */
}

/* AWBG list: R / B words from t33_awb_start_gain (or the AWB algorithm) */
static inline int t33_awbg_init(u32 r, u32 b, u32 *l, u32 cap)
{
	int len = t33_rcfg_tmpl_init(l, cap, t33_awbg_tmpl, ARRAY_SIZE(t33_awbg_tmpl));

	if (len <= 0)
		return len;
	T33_SW(l, 3, r);
	T33_SW(l, 4, b);
	T33_SW(l, 1, 1);
	return len;
}

#endif /* TX_ISP_T33_AWB_H */
