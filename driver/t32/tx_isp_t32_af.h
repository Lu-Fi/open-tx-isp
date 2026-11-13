/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 autofocus statistics block (AF, registers 0x18010 .. 0x18128).
 *
 * Vendor functions: tisp_af_malloc_cfg, tisp_af_get_statistics,
 * tisp_af_interrupt_static, tisp_af_tgain_update, tisp_af_par_init,
 * tisp_af_all_reg_write, tisp_af_alogrithm, tisp_af_calc_process,
 * tisp_af_init, tisp_af_deinit, tisp_af_param_array_get/_set,
 * tisp_af_api_set_weight/_get_weight/_get_metric/_get_attr/_set_attr/
 * _get_statis_info, tisp_af_wake_up (+ the fix_point_mult2_32 / _div_32 they use).
 *
 * IQ data: 800 bytes at tuning base + 0xec30 (AF parameter block "par"; the
 * IQ section number is not recorded in the reference).  Per channel the vendor
 * keeps: par (800), blk (6432: statistics arrays of 225 words at 0, 900, 1800,
 * 2700, 3600, 4500 and a 255 entry metric ring at 5404), regs (222: register
 * image read by tisp_af_all_reg_write) and a 16 KiB statistics DMA buffer.
 *
 * Vendor quirks kept on purpose: the par_init range clamp writes RNG0_LO /
 * RNG0_HI where RNG1 was tested, zone widths/heights skip entry 7 when
 * copied to the register image (entry 6 twice), tisp_af_interrupt_static
 * always uses the DMA slot of channel 0.
 *
 * NOT ported: debug prints (isp_printf), NULL-allocation continuation, the
 * contents of the vendor tables t32_data_d6014 / d5c90 (supplied by the
 * caller through struct t32_af_state), the initial attribute table d5c60,
 * the event / irq plumbing (reached through struct t32_af_ops), the return
 * value of tisp_af_deinit and of the memcpy tail calls.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_af_oracle).
 */
#ifndef TX_ISP_T32_AF_H
#define TX_ISP_T32_AF_H

#include "tx_isp_t32_tp.h"

#define T32_AF_CHANNELS		4
#define T32_AF_PAR_BYTES	800U
#define T32_AF_BLK_BYTES	6432U
#define T32_AF_REGS_BYTES	222U
#define T32_AF_WEIGHT_BYTES	225U
#define T32_AF_ATTR_BYTES	24U
#define T32_AF_STATIS_BYTES	5400U
#define T32_AF_DMA_BYTES	16384U
#define T32_AF_DMA_FLAGS	0x24000c0U
#define T32_AF_DMA_STEP		4096U
#define T32_AF_PAR_IQ_OFF	0xec30U
#define T32_AF_PHYS_BIAS	0x80000000U
#define T32_AF_MAX_WRITES	60

#define T32_AF_REG_TRIG		0x18010U
#define T32_AF_REG_DMA0		0x18108U
#define T32_AF_REG_DMA1		0x1810cU
#define T32_AF_REG_DMA2		0x18110U
#define T32_AF_REG_DMA3		0x18114U
#define T32_AF_REG_DMA_CTRL	0x18118U
#define T32_AF_DMA_CTRL_VAL	0x100dfU
#define T32_AF_REG_STAT_RD_0	0x1811cU
#define T32_AF_REG_STAT_RD_N	18314U
#define T32_AF_EVENT_STAT	15U
#define T32_AF_IRQ_STAT		6U

/* par offsets */
#define T32_AF_P_V1		60U
#define T32_AF_P_V2		64U
#define T32_AF_P_V1N		68U
#define T32_AF_P_FOCUS		72U
#define T32_AF_P_Q2N		76U
#define T32_AF_P_Y0		80U
#define T32_AF_P_ROWS		82U
#define T32_AF_P_X0		84U
#define T32_AF_P_COLS		86U
#define T32_AF_P_COLW		88U
#define T32_AF_P_ROWH		118U
#define T32_AF_P_RNG2_LO	284U
#define T32_AF_P_RNG2_HI	286U
#define T32_AF_P_RNG3_LO	326U
#define T32_AF_P_RNG3_HI	328U
#define T32_AF_P_RNG0_LO	368U
#define T32_AF_P_RNG0_HI	370U
#define T32_AF_P_RNG1_LO	424U
#define T32_AF_P_RNG1_HI	426U
#define T32_AF_P_PARM_A		480U
#define T32_AF_P_PARM_B		482U
#define T32_AF_P_PARM_C		484U
#define T32_AF_P_PARM_G		486U
#define T32_AF_P_PARM_SH	488U
#define T32_AF_P_WEIGHT		543U
#define T32_AF_P_MODE		768U
#define T32_AF_P_GIDX_A		773U
#define T32_AF_P_GIDX_C		786U

struct t32_af_ops {
	void *(*vmalloc)(u32 size);
	void *(*kmalloc)(u32 size, u32 flags);
	void (*vfree)(void *p);
	void (*kfree)(void *p);
	u32 (*reg_read)(u32 reg);
	void (*dma_sync)(u32 dev, void *addr, u32 size, u32 dir);
	void (*event_push)(u32 ch, const u32 *ev);
	void (*irq_set)(u32 ch, u32 irq, void *handler);
	void (*event_set_cb)(u32 ch, u32 ev, void *cb);
};

struct t32_af_ctx {			/* t32_data_e1620[ch] */
	u8 *blk;
	u8 *regs;
	u32 rsvd;
	u8 *par;
	u16 width;
	u16 height;
};

struct t32_af_dma {			/* t32_data_e1610[ch] */
	u32 count;
	u8 *buf;
	u32 phys;
};

struct t32_af_state {
	const struct t32_af_ops *ops;
	struct t32_af_ctx *ctx[T32_AF_CHANNELS];
	struct t32_af_dma *dma[T32_AF_CHANNELS];
	u32 tgain[T32_AF_CHANNELS];		/* t32_data_e1600 */
	u32 last_focus[T32_AF_CHANNELS];	/* t32_data_e15f0 */
	u8 attr[T32_AF_CHANNELS][T32_AF_ATTR_BYTES];	/* t32_data_d5c60 */
	const u8 *tbl_row12;			/* t32_data_d6014: rows of 12 words */
	const u8 *tbl_row5;			/* t32_data_d5c90: rows of 5 words */
};

static inline u32 t32_af_r16(const u8 *p, u32 o)
{
	return (u32)p[o] | ((u32)p[o + 1U] << 8);
}

static inline u32 t32_af_r32(const u8 *p, u32 o)
{
	return (u32)p[o] | ((u32)p[o + 1U] << 8) | ((u32)p[o + 2U] << 16) | ((u32)p[o + 3U] << 24);
}

static inline void t32_af_w16(u8 *p, u32 o, u32 v)
{
	p[o] = (u8)v;
	p[o + 1U] = (u8)(v >> 8);
}

static inline void t32_af_w32(u8 *p, u32 o, u32 v)
{
	t32_af_w16(p, o, v);
	t32_af_w16(p, o + 2U, v >> 16);
}

/* fix_point_mult2_32 */
static inline u32 t32_af_mult2(u32 sh, u32 x, u32 y)
{
	u32 lo = 0xffffffffU >> ((0U - sh) & 31U);
	u32 xh = x >> (sh & 31U);
	u32 yh = y >> (sh & 31U);
	u32 xl = x & lo;
	u32 yl = y & lo;

	return ((xh * yh) << (sh & 31U)) + (xl * yh + xh * yl) + ((xl * yl) >> (sh & 31U));
}

/* fix_point_div_32 (16, x, y) */
static inline u32 t32_af_div16(u32 x, u32 y)
{
	u32 q = y ? x / y : 0xffffffffU;
	u32 r = x - y * q;
	u32 frac = 0, i;

	for (i = 0; i < 16U; i++) {
		r <<= 1;
		frac <<= 1;
		if (y < r) {
			frac |= 1U;
			r -= y;
		} else if (y == r) {
			frac |= 1U;
			frac <<= (15U - i);
			break;
		}
	}
	return (q << 16) | frac;
}

/* tisp_af_all_reg_write: register image -> registers */
#define T32_AF_S8(o)	((u32)s[o])
#define T32_AF_S16(o)	t32_af_r16(s, o)
#define T32_AF_S32(o)	t32_af_r32(s, o)
#define T32_AF_P9(o)	((T32_AF_S16(o) & 0x1ffU) | ((T32_AF_S16((o) + 2U) & 0x1ffU) << 16))
#define T32_AF_P11(o)	((T32_AF_S16(o) & 0x7ffU) | ((T32_AF_S16((o) + 2U) & 0x7ffU) << 16))
#define T32_AF_P13(o)	((T32_AF_S16(o) & 0x1fffU) | ((T32_AF_S16((o) + 2U) & 0x1fffU) << 16))
#define T32_AF_H12(h, l)	(((T32_AF_S16(h) & 0xfffU) << 16) | (l))

static inline void t32_af_all_reg_write_n(const u8 *s, struct t32_front_write *w, unsigned int *n)
{
	T32_FWR(w, *n, 0x18040, (T32_AF_S16(34) & 0xfffU) | (T32_AF_S8(0) << 28) |
		((T32_AF_S8(1) << 12) & 0xffffU) | ((T32_AF_S16(2) & 0xfffU) << 16));
	T32_FWR(w, *n, 0x18044, T32_AF_P9(4));
	T32_FWR(w, *n, 0x18048, T32_AF_P9(8));
	T32_FWR(w, *n, 0x1804c, T32_AF_P9(12));
	T32_FWR(w, *n, 0x18050, T32_AF_P9(16));
	T32_FWR(w, *n, 0x18054, T32_AF_P9(20));
	T32_FWR(w, *n, 0x18058, T32_AF_P9(24));
	T32_FWR(w, *n, 0x1805c, T32_AF_P9(28));
	T32_FWR(w, *n, 0x18060, T32_AF_S16(32) & 0x1ffU);
	T32_FWR(w, *n, 0x18064, T32_AF_S32(36));
	T32_FWR(w, *n, 0x18068, T32_AF_S32(40));
	T32_FWR(w, *n, 0x1806c, T32_AF_S32(44));
	T32_FWR(w, *n, 0x18070, T32_AF_S8(48) | (T32_AF_S8(49) << 8) | (T32_AF_S8(50) << 16));
	T32_FWR(w, *n, 0x18074, (T32_AF_S8(220) & 0xfU) | (T32_AF_S8(183) << 8) |
		((T32_AF_S8(72) << 4) & 0xffU) | ((T32_AF_S8(221) & 1U) << 16));
	T32_FWR(w, *n, 0x18078, (T32_AF_S8(114) & 1U) | ((T32_AF_S8(127) & 1U) << 4) |
		((T32_AF_S8(139) & 1U) << 8) | ((T32_AF_S8(151) & 1U) << 12) |
		((T32_AF_S8(163) & 1U) << 16) | ((T32_AF_S8(168) & 1U) << 20) |
		((T32_AF_S8(173) & 1U) << 24) | ((T32_AF_S8(178) & 1U) << 28));
	T32_FWR(w, *n, 0x1807c, T32_AF_P11(52));
	T32_FWR(w, *n, 0x18080, T32_AF_P11(56));
	T32_FWR(w, *n, 0x18084, T32_AF_S16(60) & 0x7ffU);
	T32_FWR(w, *n, 0x18088, T32_AF_P11(62));
	T32_FWR(w, *n, 0x1808c, T32_AF_P11(66));
	T32_FWR(w, *n, 0x18090, T32_AF_S16(70) & 0x7ffU);
	T32_FWR(w, *n, 0x18094, (T32_AF_S16(74) & 0x1ffU) | ((T32_AF_S16(76) & 0x7ffU) << 16));
	T32_FWR(w, *n, 0x18098, T32_AF_P11(78));
	T32_FWR(w, *n, 0x1809c, T32_AF_P11(82));
	T32_FWR(w, *n, 0x180a0, T32_AF_P11(86));
	T32_FWR(w, *n, 0x18124, T32_AF_P11(90));
	T32_FWR(w, *n, 0x180a4, (T32_AF_S16(94) & 0x1ffU) | ((T32_AF_S16(96) & 0x7ffU) << 16));
	T32_FWR(w, *n, 0x180a8, T32_AF_P11(98));
	T32_FWR(w, *n, 0x180ac, T32_AF_P11(102));
	T32_FWR(w, *n, 0x180b0, T32_AF_P11(106));
	T32_FWR(w, *n, 0x18128, T32_AF_P11(110));
	T32_FWR(w, *n, 0x180b4, T32_AF_H12(118, (T32_AF_S8(116) << 8) | T32_AF_S8(115)));
	T32_FWR(w, *n, 0x180b8, T32_AF_H12(124, (T32_AF_S8(122) << 8) | T32_AF_S8(121)));
	T32_FWR(w, *n, 0x180bc, T32_AF_H12(130, T32_AF_S16(128)));
	T32_FWR(w, *n, 0x180c0, T32_AF_H12(136, (T32_AF_S8(134) << 8) | T32_AF_S8(133)));
	T32_FWR(w, *n, 0x180c4, T32_AF_H12(142, T32_AF_S16(140)));
	T32_FWR(w, *n, 0x180c8, T32_AF_H12(148, (T32_AF_S8(146) << 8) | T32_AF_S8(145)));
	T32_FWR(w, *n, 0x180cc, T32_AF_H12(154, T32_AF_S16(152)));
	T32_FWR(w, *n, 0x180d0, T32_AF_H12(160, (T32_AF_S8(158) << 8) | T32_AF_S8(157)));
	T32_FWR(w, *n, 0x180d4, (T32_AF_S8(126) << 8) | (T32_AF_S8(132) << 16) |
		T32_AF_S8(120) | (T32_AF_S8(138) << 24));
	T32_FWR(w, *n, 0x180d8, (T32_AF_S8(150) << 8) | (T32_AF_S8(156) << 16) |
		T32_AF_S8(144) | (T32_AF_S8(162) << 24));
	T32_FWR(w, *n, 0x180dc, T32_AF_S32(164));
	T32_FWR(w, *n, 0x180e0, T32_AF_S32(169));
	T32_FWR(w, *n, 0x180e4, T32_AF_S32(174));
	T32_FWR(w, *n, 0x180e8, T32_AF_S32(179));
	T32_FWR(w, *n, 0x180ec, (T32_AF_S8(184) & 0xfU) | ((T32_AF_S8(185) << 4) & 0xffU) |
		((T32_AF_S8(186) & 0xfU) << 8) | ((T32_AF_S8(187) << 12) & 0xffffU) |
		((T32_AF_S8(188) & 1U) << 16) | ((T32_AF_S8(196) & 1U) << 20) |
		((T32_AF_S8(204) & 1U) << 24) | ((T32_AF_S8(212) & 1U) << 28));
	T32_FWR(w, *n, 0x180f0, T32_AF_S8(189) | (T32_AF_S8(190) << 8) |
		(T32_AF_S8(197) << 16) | (T32_AF_S8(198) << 24));
	T32_FWR(w, *n, 0x180f4, T32_AF_S8(205) | (T32_AF_S8(206) << 8) |
		(T32_AF_S8(213) << 16) | (T32_AF_S8(214) << 24));
	T32_FWR(w, *n, 0x180f8, T32_AF_P13(192));
	T32_FWR(w, *n, 0x180fc, T32_AF_P13(200));
	T32_FWR(w, *n, 0x18100, T32_AF_P13(208));
	T32_FWR(w, *n, 0x18104, T32_AF_P13(216));
	T32_FWR(w, *n, 0x18124, (T32_AF_S16(90) & 0x7ffU) | ((T32_AF_S16(92) & 0x1ffU) << 16));
	T32_FWR(w, *n, 0x18128, (T32_AF_S16(110) & 0x7ffU) | ((T32_AF_S16(112) & 0x1ffU) << 16));
	T32_FWR(w, *n, T32_AF_REG_TRIG, 1);
}

#undef T32_AF_S8
#undef T32_AF_S16
#undef T32_AF_S32
#undef T32_AF_P9
#undef T32_AF_P11
#undef T32_AF_P13
#undef T32_AF_H12

static inline int t32_af_all_reg_write(const u8 *regs, struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_af_all_reg_write_n(regs, w, &n);
	return (int)n;
}

/* tisp_af_malloc_cfg */
static inline void t32_af_malloc_cfg_n(struct t32_af_state *st, u32 ch,
				       struct t32_front_write *w, unsigned int *n)
{
	struct t32_af_dma *d;
	u8 *buf;
	u32 phys;

	d = (struct t32_af_dma *)st->ops->vmalloc((u32)sizeof(*d));
	st->dma[ch] = d;
	buf = (u8 *)st->ops->kmalloc(T32_AF_DMA_BYTES, T32_AF_DMA_FLAGS);
	phys = (u32)(unsigned long)buf + T32_AF_PHYS_BIAS;
	T32_FWR(w, *n, T32_AF_REG_DMA0, phys);
	T32_FWR(w, *n, T32_AF_REG_DMA1, phys + T32_AF_DMA_STEP);
	T32_FWR(w, *n, T32_AF_REG_DMA2, phys + 2U * T32_AF_DMA_STEP);
	T32_FWR(w, *n, T32_AF_REG_DMA3, phys + 3U * T32_AF_DMA_STEP);
	d->count = 4;
	d->buf = buf;
	d->phys = phys;
	T32_FWR(w, *n, T32_AF_REG_DMA_CTRL, T32_AF_DMA_CTRL_VAL);
}

static inline int t32_af_malloc_cfg(struct t32_af_state *st, u32 ch, struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_af_malloc_cfg_n(st, ch, w, &n);
	return (int)n;
}

/* tisp_af_get_statistics: DMA record -> blk arrays */
static const u8 t32_af_nib_ofs[8] = { 32, 28, 24, 20, 17, 13, 9, 5 };

static inline void t32_af_get_statistics(struct t32_af_state *st, u32 ch, const u8 *src)
{
	struct t32_af_ctx *c = st->ctx[ch & 0xffU];
	u8 *b = c->blk;
	const u8 *s = c->regs;
	u32 cols = s[0], rows = s[1];
	u32 full = (cols >> 3) << 3;
	u32 stride = ((2U * cols + (cols >> 3) + ((cols & 7U) != 0U)) << 1) & 0xffffU;
	u32 base = 2U, zbase = 0U, r, a, lane, z, wd, w0, w1, w2, w3, word, nib;

	for (r = 0; r < rows; r++) {
		for (a = 0; a < cols; a++) {
			lane = a & 7U;
			z = (a + zbase) & 0xffffU;
			wd = (base + (a >> 3) * 34U + lane * 4U) & 0xffffU;
			w0 = t32_af_r32(src, wd * 4U);
			w1 = t32_af_r32(src, (wd + 1U) * 4U);
			w2 = t32_af_r32(src, (wd + 2U) * 4U);
			w3 = t32_af_r32(src, (wd + 3U) * 4U);
			t32_af_w32(b, z * 4U, w0 & 0x7fffffU);
			t32_af_w32(b, (225U + z) * 4U, ((w1 & 0x3fffU) << 9) | (w0 >> 23));
			t32_af_w32(b, (450U + z) * 4U, ((w2 & 0x1fU) << 18) | (w1 >> 14));
			t32_af_w32(b, (675U + z) * 4U, (w2 >> 5) & 0x7fffffU);
			t32_af_w32(b, (900U + z) * 4U, ((w3 & 0xfffffU) << 4) | (w2 >> 28));
			if (a < full)
				word = t32_af_r32(src, (wd + t32_af_nib_ofs[lane]) * 4U);
			else
				word = t32_af_r32(src, (wd + ((cols - a) << 2) + (lane >> 2)) * 4U);
			nib = (word >> (8U * (lane & 3U))) & 0xfU;
			t32_af_w32(b, (1125U + z) * 4U, (nib << 12) | (w3 >> 20));
		}
		zbase = (zbase + cols) & 0xffffU;
		base = (base + stride) & 0xffffU;
	}
}

/* tisp_af_interrupt_static (the vendor reads the DMA slot of channel 0 whatever ch is) */
static inline int t32_af_interrupt_static(struct t32_af_state *st, u32 ch)
{
	u32 ev[3] = { 0, 0, T32_AF_EVENT_STAT };
	u32 rd = st->ops->reg_read(ch ? T32_AF_REG_STAT_RD_N : T32_AF_REG_STAT_RD_0);
	u8 *buf = st->dma[0]->buf + (rd << 12);

	st->ops->dma_sync(0, buf, 4096U, 0);
	t32_af_get_statistics(st, ch & 0xffU, buf);
	st->ops->event_push(ch, ev);
	return 1;
}

/* tisp_af_tgain_update */
static inline void t32_af_tgain_update(struct t32_af_state *st, u32 ch, u32 gain)
{
	st->tgain[ch] = gain;
}

/* tisp_af_par_init */
static inline u32 t32_af_zone_len(u32 total, u32 org, u32 n, u32 margin)
{
	int num = (int)(total - org - margin);
	u32 q;

	if (n)
		q = (u32)(num / (int)n);
	else
		q = num < 0 ? 1U : 0xffffffffU;
	q &= 0xffffU;
	if (q & 1U)
		q = (q - 1U) & 0xffffU;
	return q;
}

static inline u32 t32_af_tri(int n, u32 base)
{
	u32 s = 0, i;

	for (i = 0; (int)i < n; i++)
		s = (s + base - i) & 0xffffU;
	return s;
}

static inline void t32_af_row12(u16 *d, const u8 *tbl, u32 row)
{
	u32 k;

	for (k = 0; k < 12U; k++)
		d[k] = (u16)t32_af_r32(tbl, row * 48U + k * 4U);
}

static inline void t32_af_row5(u16 *d, const u8 *tbl, u32 row)
{
	u32 k;

	for (k = 0; k < 5U; k++)
		d[k] = (u16)t32_af_r32(tbl, row * 20U + k * 4U);
}

static inline void t32_af_par_init(struct t32_af_state *st, u32 ch)
{
	struct t32_af_ctx *c = st->ctx[ch];
	u8 *p = c->par;
	u8 *s = c->regs;
	u16 x[12], y[12], z[5], wv[5];
	u32 x0 = t32_af_r16(p, T32_AF_P_X0);
	u32 cols = t32_af_r16(p, T32_AF_P_COLS);
	u32 y0 = t32_af_r16(p, T32_AF_P_Y0);
	u32 rows = t32_af_r16(p, T32_AF_P_ROWS);
	u32 v, i, ra, rb, rc, rd, t2, t3, t4, t5, top, k, src;

	v = t32_af_zone_len(c->width, x0, cols, 14U);
	for (i = 0; i < cols; i++)
		t32_af_w16(p, T32_AF_P_COLW + 2U * i, v);
	v = t32_af_zone_len(c->height, y0, rows, 2U);
	for (i = 0; i < rows; i++)
		t32_af_w16(p, T32_AF_P_ROWH + 2U * i, v);

	if (p[T32_AF_P_MODE] == 0) {
		if (t32_af_r16(p, T32_AF_P_RNG0_LO) >= 6U)
			t32_af_w16(p, T32_AF_P_RNG0_LO, 5U);
		t5 = t32_af_r16(p, T32_AF_P_RNG1_LO);
		if (t5 >= 6U)
			t32_af_w16(p, T32_AF_P_RNG0_LO, 5U);	/* vendor writes RNG0 here */
		if (t32_af_r16(p, T32_AF_P_RNG2_LO) >= 9U)
			t32_af_w16(p, T32_AF_P_RNG2_LO, 8U);
		if (t32_af_r16(p, T32_AF_P_RNG3_LO) >= 9U)
			t32_af_w16(p, T32_AF_P_RNG3_LO, 8U);
		if (t32_af_r16(p, T32_AF_P_RNG0_HI) >= 7U)
			t32_af_w16(p, T32_AF_P_RNG0_HI, 6U);
		top = t32_af_r16(p, T32_AF_P_RNG1_HI);
		if (top >= 7U)
			t32_af_w16(p, T32_AF_P_RNG0_HI, 6U);	/* vendor writes RNG0 here */
		if (t32_af_r16(p, T32_AF_P_RNG2_HI) >= 10U)
			t32_af_w16(p, T32_AF_P_RNG2_HI, 9U);
		if (t32_af_r16(p, T32_AF_P_RNG3_HI) >= 10U)
			t32_af_w16(p, T32_AF_P_RNG3_HI, 9U);
		t2 = t32_af_r16(p, T32_AF_P_RNG0_LO);
		if (t2 >= t32_af_r16(p, T32_AF_P_RNG0_HI))
			t32_af_w16(p, T32_AF_P_RNG0_HI, t2 + 1U);
		if (t5 >= top)
			t32_af_w16(p, T32_AF_P_RNG1_HI, t5 + 1U);
		t4 = t32_af_r16(p, T32_AF_P_RNG2_LO);
		if (t4 >= t32_af_r16(p, T32_AF_P_RNG2_HI))
			t32_af_w16(p, T32_AF_P_RNG2_HI, t4 + 1U);
		t3 = t32_af_r16(p, T32_AF_P_RNG3_LO);
		if (t3 >= t32_af_r16(p, T32_AF_P_RNG3_HI))
			t32_af_w16(p, T32_AF_P_RNG3_HI, t3 + 1U);
		ra = (t32_af_tri((int)((t32_af_r16(p, T32_AF_P_RNG0_HI) - t2) & 0xffffU) - 1, 6U) + t2) & 0xffffU;
		rb = (t32_af_tri((int)((t32_af_r16(p, T32_AF_P_RNG1_HI) - t5) & 0xffffU) - 1, 6U) + t5) & 0xffffU;
		rc = (t32_af_tri((int)((t32_af_r16(p, T32_AF_P_RNG2_HI) - t4) & 0xffffU) - 1, 9U) + t4) & 0xffffU;
		rd = (t32_af_tri((int)((t32_af_r16(p, T32_AF_P_RNG3_HI) - t3) & 0xffffU) - 1, 9U) + t3) & 0xffffU;
		t32_af_row12(x, st->tbl_row12, ra);
		t32_af_row12(y, st->tbl_row12, rb);
		t32_af_row5(z, st->tbl_row5, rc);
		t32_af_row5(wv, st->tbl_row5, rd);
	} else {
		i = (st->tgain[ch] >> 16) & 0xffffU;
		st->tgain[ch] = i;
		ra = p[T32_AF_P_GIDX_A + i];
		rc = p[T32_AF_P_GIDX_C + i];
		t32_af_row12(x, st->tbl_row12, ra);
		t32_af_row12(y, st->tbl_row12, ra + 1U);
		t32_af_row5(z, st->tbl_row5, rc);
		t32_af_row5(wv, st->tbl_row5, rc + 1U);
	}
	t32_af_w16(p, 288U, z[2]);
	t32_af_w16(p, 290U, z[3]);
	t32_af_w16(p, 292U, z[4]);
	t32_af_w16(p, 330U, wv[2]);
	t32_af_w16(p, 332U, wv[3]);
	t32_af_w16(p, 334U, wv[4]);
	for (k = 2; k < 12U; k++) {
		t32_af_w16(p, 368U + 2U * k, x[k]);
		t32_af_w16(p, 424U + 2U * k, y[k]);
	}

	/* register image */
	s[0] = (u8)t32_af_r16(p, T32_AF_P_COLS);
	s[1] = (u8)t32_af_r16(p, T32_AF_P_ROWS);
	t32_af_w16(s, 2U, t32_af_r16(p, T32_AF_P_X0));
	t32_af_w16(s, 34U, t32_af_r16(p, T32_AF_P_Y0));
	for (k = 0; k < 15U; k++) {
		src = k == 7U ? 6U : k;
		t32_af_w16(s, 4U + 2U * k, t32_af_r16(p, T32_AF_P_COLW + 2U * src));
		s[36U + k] = (u8)t32_af_r16(p, T32_AF_P_ROWH + 2U * src);
	}
	for (k = 0; k < 5U; k++) {
		t32_af_w16(s, 52U + 2U * k, z[k]);
		t32_af_w16(s, 62U + 2U * k, wv[k]);
	}
	s[72] = 15;
	t32_af_w16(s, 74U, x[0]);
	t32_af_w16(s, 76U, x[2]);
	t32_af_w16(s, 78U, x[3]);
	t32_af_w16(s, 80U, x[4]);
	t32_af_w16(s, 82U, x[5]);
	t32_af_w16(s, 84U, x[7]);
	t32_af_w16(s, 86U, x[8]);
	t32_af_w16(s, 88U, x[9]);
	t32_af_w16(s, 90U, x[10]);
	t32_af_w16(s, 92U, x[11]);
	t32_af_w16(s, 94U, y[0]);
	t32_af_w16(s, 96U, y[2]);
	t32_af_w16(s, 98U, y[3]);
	t32_af_w16(s, 100U, y[4]);
	t32_af_w16(s, 102U, y[5]);
	t32_af_w16(s, 104U, y[7]);
	t32_af_w16(s, 106U, y[8]);
	t32_af_w16(s, 108U, y[9]);
	t32_af_w16(s, 110U, y[10]);
	t32_af_w16(s, 112U, y[11]);
#define T32_AF_B8(d, o)		(s[d] = p[o])
#define T32_AF_C8(d, o)		(s[d] = (u8)t32_af_r16(p, o))
#define T32_AF_C16(d, o)	t32_af_w16(s, d, t32_af_r16(p, o))
	T32_AF_B8(114, 523); T32_AF_C8(115, 294); T32_AF_C8(116, 296); T32_AF_C16(118, 298);
	T32_AF_C8(120, 300); T32_AF_C8(121, 302); T32_AF_C8(122, 304); T32_AF_C16(124, 306);
	T32_AF_C8(126, 308); T32_AF_B8(127, 524);
	T32_AF_C8(128, 336); T32_AF_C8(129, 338); T32_AF_C16(130, 340); T32_AF_C8(132, 342);
	T32_AF_C8(133, 344); T32_AF_C8(134, 346); T32_AF_C16(136, 348); T32_AF_C8(138, 350);
	T32_AF_B8(139, 525);
	T32_AF_C8(140, 392); T32_AF_C8(141, 394); T32_AF_C16(142, 396); T32_AF_C8(144, 398);
	T32_AF_C8(145, 400); T32_AF_C8(146, 402); T32_AF_C16(148, 404); T32_AF_C8(150, 406);
	T32_AF_B8(151, 526);
	T32_AF_C8(152, 448); T32_AF_C8(153, 450); T32_AF_C16(154, 452); T32_AF_C8(156, 454);
	T32_AF_C8(157, 456); T32_AF_C8(158, 458); T32_AF_C16(160, 460); T32_AF_C8(162, 462);
	T32_AF_B8(163, 527);
	T32_AF_C8(164, 310); T32_AF_C8(165, 312); T32_AF_C8(166, 314); T32_AF_C8(167, 316);
	T32_AF_B8(168, 528);
	T32_AF_C8(169, 352); T32_AF_C8(170, 354); T32_AF_C8(171, 356); T32_AF_C8(172, 358);
	T32_AF_B8(173, 529);
	T32_AF_C8(174, 408); T32_AF_C8(175, 410); T32_AF_C8(176, 412); T32_AF_C8(177, 414);
	T32_AF_B8(178, 530);
	T32_AF_C8(179, 464); T32_AF_C8(180, 466); T32_AF_C8(181, 468); T32_AF_C8(182, 470);
	T32_AF_B8(183, 531);
	T32_AF_B8(184, 532); T32_AF_B8(185, 533); T32_AF_B8(186, 534); T32_AF_B8(187, 535);
	T32_AF_B8(188, 536);
	T32_AF_C8(189, 318); T32_AF_C8(190, 320); T32_AF_C16(192, 322); T32_AF_C16(194, 324);
	T32_AF_B8(196, 537);
	T32_AF_C8(197, 360); T32_AF_C8(198, 362); T32_AF_C16(200, 364); T32_AF_C16(202, 366);
	T32_AF_B8(204, 538);
	T32_AF_C8(205, 416); T32_AF_C8(206, 418); T32_AF_C16(208, 420); T32_AF_C16(210, 422);
	T32_AF_B8(212, 539);
	T32_AF_C8(213, 472); T32_AF_C8(214, 474); T32_AF_C16(216, 476); T32_AF_C16(218, 478);
#undef T32_AF_B8
#undef T32_AF_C8
#undef T32_AF_C16
	s[220] = 1;
	s[221] = 0;
}

/* tisp_af_alogrithm: weighted statistics -> metrics, history, ring */
static inline u32 t32_af_alogrithm(struct t32_af_state *st, u32 ch)
{
	struct t32_af_ctx *c;
	u8 *b, *p;
	u32 rows, cols, ac, bc, cc, gc, sh, dsh, one, state;
	u32 wsum = 0, a1800 = 0, a0 = 0, a2700 = 0, a900 = 0;
	u32 r, i, idx, wgt, x3, x4, q1, q2, v1, v2, n, old, focus, diff, d, val;

	ch &= 0xffU;
	c = st->ctx[ch];
	b = c->blk;
	p = c->par;
	rows = t32_af_r16(p, T32_AF_P_ROWS);
	cols = t32_af_r16(p, T32_AF_P_COLS);
	ac = t32_af_r16(p, T32_AF_P_PARM_A);
	bc = t32_af_r16(p, T32_AF_P_PARM_B);
	cc = t32_af_r16(p, T32_AF_P_PARM_C);
	gc = t32_af_r16(p, T32_AF_P_PARM_G);
	sh = t32_af_r16(p, T32_AF_P_PARM_SH);
	dsh = t32_af_r16(b, 6428U);
	for (r = 0; r < rows; r++) {
		for (i = 0; i < cols; i++) {
			wgt = p[T32_AF_P_WEIGHT + r * 15U + i];
			idx = (r * rows + i) * 4U;
			wsum += wgt;
			a1800 += wgt * t32_af_r32(b, 1800U + idx);
			a2700 += wgt * t32_af_r32(b, 2700U + idx);
			a0 += wgt * t32_af_r32(b, idx);
			a900 += wgt * t32_af_r32(b, 900U + idx);
		}
	}
	one = 1U << (sh & 31U);
	x3 = t32_af_mult2(sh, a1800 << (sh & 31U), ac) +
	     t32_af_mult2(sh, a0 << (sh & 31U), one - ac);
	x4 = t32_af_mult2(sh, a2700 << (sh & 31U), bc) +
	     t32_af_mult2(sh, a900 << (sh & 31U), one - bc);
	q1 = wsum ? x3 / wsum : 0xffffffffU;
	q2 = wsum ? x4 / wsum : 0xffffffffU;
	v1 = (t32_af_mult2(sh, q1 << (sh & 31U), cc) + t32_af_mult2(sh, q2 << (sh & 31U), gc)) >> (sh & 31U);
	v2 = (t32_af_mult2(sh, (x3 >> 3) << (sh & 31U), cc) +
	      t32_af_mult2(sh, (x4 >> 3) << (sh & 31U), gc)) >> (sh & 31U);
	for (i = 1; i < 15U; i++)
		t32_af_w32(p, 4U * i, t32_af_r32(p, 4U * i + 4U));
	t32_af_w32(p, T32_AF_P_V1, v1);
	t32_af_w32(p, T32_AF_P_V2, v2);
	n = cols * rows;
	old = st->last_focus[ch];
	focus = q1 * n;
	t32_af_w32(p, T32_AF_P_FOCUS, focus);
	t32_af_w32(p, T32_AF_P_V1N, v1 * n);
	t32_af_w32(p, T32_AF_P_Q2N, q2 * n);
	diff = old < focus ? focus - old : old - focus;
	if (old == 0)
		st->last_focus[ch] = 1;
	d = t32_af_div16(diff, st->last_focus[ch]);
	state = b[6431];
	if (state <= 3U) {
		for (i = 0; i < 254U; i++)
			t32_af_w32(b, 5404U + 4U * i, t32_af_r32(b, 5408U + 4U * i));
		if (state == 0U)
			val = d << (dsh & 31U);
		else if (state == 1U)
			val = t32_af_r32(p, T32_AF_P_V1N);
		else if (state == 2U)
			val = t32_af_r32(p, T32_AF_P_FOCUS);
		else
			val = t32_af_r32(p, T32_AF_P_Q2N);
		t32_af_w32(b, 6420U, val);
	}
	st->last_focus[ch] = focus;
	return focus;
}

static inline int t32_af_calc_process(struct t32_af_state *st, u32 ch)
{
	t32_af_alogrithm(st, ch & 0xffU);
	return 0;
}

/* tisp_af_init */
static inline int t32_af_init(struct t32_af_state *st, u32 ch, u32 width, u32 height,
			      const u8 *tuning_base, struct t32_front_write *w)
{
	unsigned int n = 0;
	struct t32_af_ctx *c;

	t32_af_malloc_cfg_n(st, ch, w, &n);
	c = (struct t32_af_ctx *)st->ops->vmalloc((u32)sizeof(*c));
	st->ctx[ch] = c;
	memset(c, 0, sizeof(*c));
	c->regs = (u8 *)st->ops->vmalloc(T32_AF_REGS_BYTES);
	memset(c->regs, 0, T32_AF_REGS_BYTES);
	c->blk = (u8 *)st->ops->vmalloc(T32_AF_BLK_BYTES);
	memset(c->blk, 0, T32_AF_BLK_BYTES);
	c->par = (u8 *)st->ops->vmalloc(T32_AF_PAR_BYTES);
	memset(c->par, 0, T32_AF_PAR_BYTES);
	memcpy(c->par, tuning_base + T32_AF_PAR_IQ_OFF, T32_AF_PAR_BYTES);
	c->width = (u16)width;
	c->height = (u16)height;
	t32_af_par_init(st, ch);
	t32_af_all_reg_write_n(c->regs, w, &n);
	st->ops->irq_set(ch & 0xffU, T32_AF_IRQ_STAT, (void *)t32_af_interrupt_static);
	st->ops->event_set_cb(ch, T32_AF_EVENT_STAT, (void *)t32_af_calc_process);
	return (int)n;
}

/* tisp_af_deinit */
static inline int t32_af_deinit(struct t32_af_state *st, u32 ch)
{
	struct t32_af_ctx *c = st->ctx[ch];
	struct t32_af_dma *d;

	if (c) {
		if (c->regs) {
			st->ops->vfree(c->regs);
			c->regs = NULL;
		}
		if (c->blk) {
			st->ops->vfree(c->blk);
			c->blk = NULL;
		}
		if (c->par) {
			st->ops->vfree(c->par);
			c->par = NULL;
		}
		st->ops->vfree(c);
		st->ctx[ch] = NULL;
	}
	d = st->dma[ch];
	if (d) {
		if (d->buf) {
			st->ops->kfree(d->buf);
			d->buf = NULL;
		}
		st->ops->vfree(d);
		st->dma[ch] = NULL;
	}
	return 0;
}

/* tisp_af_param_array_get / _set */
static inline int t32_af_param_array_get(struct t32_af_state *st, u32 ch, u8 *buf, u32 *len)
{
	struct t32_af_ctx *c = st->ctx[ch];

	memcpy(buf, c->par, T32_AF_PAR_BYTES);
	memcpy(buf + T32_AF_PAR_BYTES, c->blk, T32_AF_BLK_BYTES);
	*len = T32_AF_PAR_BYTES + T32_AF_BLK_BYTES;
	return 0;
}

static inline int t32_af_param_array_set(struct t32_af_state *st, u32 ch, const u8 *buf,
					 struct t32_front_write *w)
{
	struct t32_af_ctx *c = st->ctx[ch];
	unsigned int n = 0;

	memcpy(c->par, buf, T32_AF_PAR_BYTES);
	memcpy(c->blk, buf + T32_AF_PAR_BYTES, T32_AF_BLK_BYTES);
	t32_af_par_init(st, ch);
	t32_af_all_reg_write_n(c->regs, w, &n);
	return (int)n;
}

/* tisp_af_api_set_weight / get_weight */
static inline void t32_af_api_set_weight(struct t32_af_state *st, u32 ch, const u8 *buf)
{
	memcpy(st->ctx[ch]->par + T32_AF_P_WEIGHT, buf, T32_AF_WEIGHT_BYTES);
}

static inline void t32_af_api_get_weight(struct t32_af_state *st, u32 ch, u8 *buf)
{
	memcpy(buf, st->ctx[ch]->par + T32_AF_P_WEIGHT, T32_AF_WEIGHT_BYTES);
}

/* tisp_af_api_get_metric */
static inline int t32_af_api_get_metric(struct t32_af_state *st, u32 ch, u32 *m1, u32 *m2, u8 *state)
{
	const u8 *p = st->ctx[ch]->par;
	u32 sh = st->attr[ch][9] & 31U;

	*m1 = t32_af_r32(p, T32_AF_P_V1N) >> sh;
	*m2 = t32_af_r32(p, T32_AF_P_V2) >> sh;
	*state = (u8)t32_af_r32(st->ctx[ch]->blk, 5400U);
	return 0;
}

/* tisp_af_api_get_attr / set_attr */
static inline int t32_af_api_get_attr(struct t32_af_state *st, u32 ch, u8 *out)
{
	const u8 *e = st->attr[ch];
	const u8 *p = st->ctx[ch]->par;
	u32 sh = e[9] & 31U;

	t32_af_w32(out, 0U, t32_af_r32(p, T32_AF_P_V1N) >> sh);
	t32_af_w32(out, 4U, t32_af_r32(p, T32_AF_P_V2) >> sh);
	out[8] = e[8];
	out[9] = e[9];
	t32_af_w16(out, 10U, t32_af_r16(p, T32_AF_P_PARM_C));
	t32_af_w16(out, 12U, t32_af_r16(p, T32_AF_P_PARM_G));
	t32_af_w16(out, 14U, p[531]);
	t32_af_w16(out, 16U, t32_af_r16(p, T32_AF_P_PARM_A));
	out[18] = (u8)t32_af_r16(p, T32_AF_P_X0);
	out[19] = (u8)t32_af_r16(p, T32_AF_P_Y0);
	out[20] = (u8)t32_af_r16(p, T32_AF_P_COLS);
	out[21] = (u8)t32_af_r16(p, T32_AF_P_ROWS);
	t32_af_w16(out, 22U, t32_af_r16(p, T32_AF_P_PARM_B));
	return 0;
}

static inline int t32_af_api_set_attr(struct t32_af_state *st, u32 ch, const u8 *in,
				      struct t32_front_write *w)
{
	struct t32_af_ctx *c = st->ctx[ch];
	u8 *e = st->attr[ch];
	u8 *p = c->par;
	unsigned int n = 0;

	memcpy(e, in, T32_AF_ATTR_BYTES);
	t32_af_w16(p, T32_AF_P_PARM_C, t32_af_r16(e, 10U));
	t32_af_w16(p, T32_AF_P_PARM_G, t32_af_r16(e, 12U));
	p[531] = (u8)t32_af_r16(e, 14U);
	t32_af_w16(p, T32_AF_P_PARM_A, t32_af_r16(e, 16U));
	t32_af_w16(p, T32_AF_P_X0, e[18]);
	t32_af_w16(p, T32_AF_P_Y0, e[19]);
	t32_af_w16(p, T32_AF_P_COLS, e[20]);
	t32_af_w16(p, T32_AF_P_ROWS, e[21]);
	t32_af_w16(p, T32_AF_P_PARM_B, t32_af_r16(e, 22U));
	t32_af_par_init(st, ch);
	t32_af_all_reg_write_n(c->regs, w, &n);
	return (int)n;
}

/* tisp_af_api_get_statis_info */
static inline void t32_af_api_get_statis_info(struct t32_af_state *st, u32 ch, u8 *out)
{
	memcpy(out, st->ctx[ch]->blk, T32_AF_STATIS_BYTES);
}

/* tisp_af_wake_up */
static inline int t32_af_wake_up(struct t32_af_state *st, u32 ch, struct t32_front_write *w)
{
	unsigned int n = 0;
	u32 phys = st->dma[ch]->phys;

	T32_FWR(w, n, T32_AF_REG_DMA0, phys);
	T32_FWR(w, n, T32_AF_REG_DMA1, phys + 4096U);
	T32_FWR(w, n, T32_AF_REG_DMA2, phys + 8192U);
	T32_FWR(w, n, T32_AF_REG_DMA3, phys + 12288U);
	T32_FWR(w, n, T32_AF_REG_DMA_CTRL, T32_AF_DMA_CTRL_VAL);
	t32_af_all_reg_write_n(st->ctx[ch]->regs, w, &n);
	return (int)n;
}

#endif /* TX_ISP_T32_AF_H */
