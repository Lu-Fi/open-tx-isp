/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) gamma on the vendor block state gamma_para (openimp #99,
 * wiring group 13d-2; #608 IMP_ISP_Tuning_SetGamma).
 *
 * gamma_para[ch] points at six words (tisp_gamma_init, private_vmalloc each):
 *   0  the section 13 (tparamsP + 0x5b80; 0x42 / 0x43 / 0x45 are written)
 *   1  cp  0x104 B: byte 0 the strength, + 2 the written curve (0x81 u16)
 *   2  ip  8 B: the EV words of the last interp_by_ev
 *   3  sp  0x104 B: byte 0 the API mode, 1 the strength (0xff in API mode), + 2 the curve
 *   4  the curve of update_curve (0x81 u16)
 *   5  ap  0x108 B: word 0 the API mode, + 4 the API curve (gamma_ap)
 * stLutInfo[ch] + 0x20 = the curve table (sec[0x46] records of 0x10a B: 0x81 u16, two s32 EV bounds).
 *
 * The math is the proven tx_isp_t33_gamma.h (block gammaev); this file is the
 * vendor's function split on that state: tisp_gamma_interp_by_ev,
 * _update_curve, _strength_transform, _write_lut_rgb, _ev_update,
 * _switch_para, _api_set / _api_get (tisp_s_Gamma / tisp_g_Gamma) and
 * _irq_frame_done (system irq 46).  The list writes go through wr
 * (tisp_reg_write_t33: cfg != 0 slot numbers, else the 0x32040.. registers).
 * Oracle block gammav (tests/t33_binoracle/gammav.conf) on the vendor module.
 * Only pointer-sized words: built for MIPS32 (kernel, oracle) the block is the
 * vendor's layout.
 */
#ifndef TX_ISP_T33_GAMMAV_H
#define TX_ISP_T33_GAMMAV_H

#include "tx_isp_t33_gamma.h"
#include "tx_isp_t32_tunpre.h"

#define T33_GV_BLK_WORDS	6
#define T33_GV_CP_BYTES		0x104U
#define T33_GV_SP_BYTES		0x104U
#define T33_GV_CURVE_BYTES	0x102U
#define T33_GV_AP_BYTES		0x108U
#define T33_GAMMA_IRQ_BIT	14	/* system irq 46 = interrupt status 0x300a8 bit 14 */

typedef int (*t33_gv_wr_fn)(u32 ch, u32 mod, u32 reg, u32 val);
typedef int (*t33_gv_dma_fn)(u32 mod);
typedef int (*t33_gv_trig_fn)(u32 ch, u32 mod);

#define T33_GV_P(g, i)	((u8 *)(unsigned long)(g)[i])

/* the vendor's .data defaults: gamma_cp { 0xff, 0, curve }, gamma_sp { 0, 0xff, curve }, gamma_ap { 0, curve } */
static inline void t33_gv_defaults(unsigned long *g)
{
	u8 *cp = T33_GV_P(g, 1), *sp = T33_GV_P(g, 3), *ap = T33_GV_P(g, 5);

	memset(cp, 0, T33_GV_CP_BYTES);
	memset(sp, 0, T33_GV_SP_BYTES);
	memset(ap, 0, T33_GV_AP_BYTES);
	cp[0] = 0xff;
	sp[1] = 0xff;
	memcpy(cp + 2, t33_gamma_ap_def, T33_GV_CURVE_BYTES);
	memcpy(sp + 2, t33_gamma_ap_def, T33_GV_CURVE_BYTES);
	memcpy(ap + 4, t33_gamma_ap_def, T33_GV_CURVE_BYTES);
	memset(T33_GV_P(g, 2), 0, 8);
}

static inline u32 t33_gv_ip(const unsigned long *g, int k)
{
	return t33_le32(T33_GV_P(g, 2) + 4 * k);
}

/* tisp_gamma_interp_by_ev(ch, x, lo, hi, force): the strength byte cp[0], ip = the EV words */
static inline int t33_gv_interp(unsigned long *g, u32 lo, u32 hi, int force)
{
	u32 *ip = (u32 *)(void *)T33_GV_P(g, 2);

	ip[0] = lo;
	ip[1] = hi;
	return t33_gamma_interp(T33_GV_P(g, 0), lo, hi, T33_GV_P(g, 1), force);
}

/* tisp_gamma_update_curve(ch, x, lo, hi): the curve at the EV into word 4 (kept on a zero EV span) */
static inline void t33_gv_update_curve(unsigned long *g, const u8 *tab, u32 lo, u32 hi)
{
	u16 c[T33_GAMMA_N];

	memcpy(c, T33_GV_P(g, 4), sizeof(c));
	t33_gamma_curve(T33_GV_P(g, 0), tab, hi << 22 | lo >> 10, c);
	memcpy(T33_GV_P(g, 4), c, sizeof(c));
}

/*
 * tisp_gamma_strength_transform: normal mode (sec[0x42] == 1, or 0 with sec[0x43] != 2) the curve blended
 * with the linear ramp into cp + 2 (and into ap with sec[0x44] == 1), sp = { 0, strength, curve }; API mode
 * the API curve into cp + 2, sp = { mode, 0xff, curve }, with sec[0x45] == 1 also into the table's first curve.
 */
static inline void t33_gv_transform(unsigned long *g, u8 *tab)
{
	const u8 *sec = T33_GV_P(g, 0);
	u8 *cp = T33_GV_P(g, 1), *sp = T33_GV_P(g, 3), *ap = T33_GV_P(g, 5);
	u16 c[T33_GAMMA_N], o[T33_GAMMA_N];

	if (sec[0x42] == 1 || (sec[0x42] == 0 && sec[0x43] != 2)) {
		memcpy(c, T33_GV_P(g, 4), sizeof(c));
		t33_gamma_transform(c, cp[0], o);
		memcpy(cp + 2, o, T33_GV_CURVE_BYTES);
		if (sec[0x44] == 1) {
			memset(ap, 0, 4);
			memcpy(ap + 4, cp + 2, T33_GV_CURVE_BYTES);
		}
		memcpy(sp + 2, cp + 2, T33_GV_CURVE_BYTES);
		sp[0] = 0;
		sp[1] = cp[0];
		return;
	}
	memcpy(cp + 2, ap + 4, T33_GV_CURVE_BYTES);
	memcpy(sp + 2, ap + 4, T33_GV_CURVE_BYTES);
	sp[0] = ap[0];
	sp[1] = 0xff;	/* API mode */
	if (sec[0x45] == 1)
		memcpy(tab, cp + 2, T33_GV_CURVE_BYTES);
}

/* tisp_gamma_write_lut_rgb: cp + 2 as 128 pairs into both ports of list 11, framed by 0x101 / 0x7f0102 */
static inline void t33_gv_write_lut(u32 ch, const unsigned long *g, u8 cfg, t33_gv_wr_fn wr)
{
	const u8 *v = T33_GV_P(g, 1) + 2;
	u32 a = cfg ? 3 : 0x32044, b = cfg ? 0x88 : 0x32084, k, x;

	wr(ch, T33_MOD_GAMMA, cfg ? 1 : 0x32040, 0x101);
	wr(ch, T33_MOD_GAMMA, cfg ? 0x86 : 0x32080, 0x101);
	for (k = 0; k < 0x80; k++) {
		x = (t32_tp_le16(v + 2 * k + 2) & 0xfff) << 12 | (t32_tp_le16(v + 2 * k) & 0xfff);
		wr(ch, T33_MOD_GAMMA, a + k, x);
		wr(ch, T33_MOD_GAMMA, b + k, x);
	}
	wr(ch, T33_MOD_GAMMA, cfg ? 0x84 : 0x32040, 0x7f0102);
	wr(ch, T33_MOD_GAMMA, cfg ? 0x109 : 0x32080, 0x7f0102);
}

/*
 * tisp_gamma_ev_update (tisp_long_ev_update, top bypass bit 8 clear): EV clamped to 0x100:0, interp_by_ev
 * (not forced), update_curve, and on a new strength transform + write_lut_rgb and *refresh = 1 (list idle)
 * or 2.  Returns 0 written, 1 unchanged, -1 two equal EV points.
 */
static inline int t33_gv_ev_update(u32 ch, unsigned long *g, u8 *tab, u32 lo, u32 hi, u8 cfg, t33_gv_wr_fn wr,
				   t33_gv_dma_fn dma, u8 *refresh)
{
	int r;

	if (hi > 0x100 || (hi == 0x100 && lo)) {
		lo = 0;
		hi = 0x100;
	}
	r = t33_gv_interp(g, lo, hi, 0);
	t33_gv_update_curve(g, tab, lo, hi);
	if (r)
		return r < 0 ? -1 : 1;
	t33_gv_transform(g, tab);
	t33_gv_write_lut(ch, g, cfg, wr);
	*refresh = dma(T33_MOD_GAMMA) ? 2 : 1;
	return 0;
}

/* tisp_gamma_switch_para: the new section, interp_by_ev (forced) / update_curve at ip, transform, write_lut */
static inline void t33_gv_switch(u32 ch, unsigned long *g, u8 *sec, u8 *tab, u8 cfg, t33_gv_wr_fn wr)
{
	g[0] = (unsigned long)sec;
	t33_gv_interp(g, t33_gv_ip(g, 0), t33_gv_ip(g, 1), 1);
	t33_gv_update_curve(g, tab, t33_gv_ip(g, 0), t33_gv_ip(g, 1));
	t33_gv_transform(g, tab);
	t33_gv_write_lut(ch, g, cfg, wr);
}

/*
 * tisp_gamma_api_set (tisp_s_Gamma): attr = { u32 mode, 0x81 u16 curve }.  Mode 0: back to the tuned curve
 * (0x42 / 0x43 == 2 cleared, interp_by_ev forced + update_curve at ip); 1 sRGB, 2 Rec.709, 3 HDR preset, 4 the
 * caller's curve into ap (0x43 = 2: API mode); any other mode returns -1 after storing it in ap word 0.  Then
 * transform + write_lut_rgb and *refresh = 1 (the next irq 46 starts list 11).
 */
static inline int t33_gv_api_set(u32 ch, unsigned long *g, u8 *tab, const u8 *attr, u8 cfg, t33_gv_wr_fn wr,
				 u8 *refresh)
{
	u8 *sec = T33_GV_P(g, 0), *ap = T33_GV_P(g, 5);
	const void *src;
	u32 m = t33_le32(attr);

	memcpy(ap, attr, 4);
	if (!m) {
		if (sec[0x42] == 2)
			sec[0x42] = 0;
		if (sec[0x43] == 2)
			sec[0x43] = 0;
		t33_gv_interp(g, t33_gv_ip(g, 0), t33_gv_ip(g, 1), 1);
		t33_gv_update_curve(g, tab, t33_gv_ip(g, 0), t33_gv_ip(g, 1));
	} else {
		if (m == 1)
			src = t32_tun_gamma_bf754;	/* static_srgb_gamma */
		else if (m == 2)
			src = t32_tun_gamma_bf650;	/* static_rec709_gamma */
		else if (m == 3)
			src = t32_tun_gamma_bf54c;	/* static_hdr_gamma */
		else if (m == 4)
			src = attr + 4;
		else
			return -1;
		sec[0x43] = 2;
		memcpy(ap + 4, src, T33_GV_CURVE_BYTES);
	}
	t33_gv_transform(g, tab);
	t33_gv_write_lut(ch, g, cfg, wr);
	*refresh = 1;
	return 0;
}

/* tisp_gamma_api_get (tisp_g_Gamma): the API block ap (0x108 B) */
static inline int t33_gv_api_get(const unsigned long *g, u8 *out)
{
	memcpy(out, T33_GV_P(g, 5), T33_GV_AP_BYTES);
	return 0;
}

/*
 * tisp_gamma_irq_frame_done (system irq 46): count (24 bit); refresh 1 starts list 11, refresh 2 rewrites the
 * slots first once the list is idle; refresh cleared after a start.  Always returns 1.
 */
static inline int t33_gv_frame_done(u32 ch, const unsigned long *g, u32 *count, u8 *refresh, u8 cfg,
				    t33_gv_wr_fn wr, t33_gv_dma_fn dma, t33_gv_trig_fn trig)
{
	count[ch] = (count[ch] + 1) & 0xffffff;
	if (*refresh != 1) {
		if (*refresh != 2 || dma(T33_MOD_GAMMA))
			return 1;
		t33_gv_write_lut(ch, g, cfg, wr);
	}
	trig(ch & 0xff, T33_MOD_GAMMA);
	*refresh = 0;
	return 1;
}

#endif /* TX_ISP_T33_GAMMAV_H */
