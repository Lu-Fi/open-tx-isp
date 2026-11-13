/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LSC: the whole vendor tisp_lsc_write_all_reg (all modes, the mesh LUT
 * port with its completion wait), the colour temperature entry point
 * tisp_lsc_ct_update (tisp_lsc_interp_by_ct + write_all_reg(ch, 2, 0, 1)),
 * tisp_lsc_tgain_update on top of it (tisp_lsc_interp_by_tgain +
 * write_all_reg(ch, 1, 1, 0)) and the frame done interrupt
 * tisp_lsc_irq_frame_done (Matt Davis' recovered C, PR #15).
 *
 * tisp_lsc_write_all_reg(ch, mode, sonly, mesh):
 *  1. mutex (vendor t32_data_e1800); under the spin lock t32_small_bss the
 *     pending list trigger (t32_data_e180c) is dropped and 0x22014 (register
 *     list DMA) read: busy -> flag the waiter (t32_data_e17f0), init the
 *     completion (e17f4), unlock, isp_printf, wait up to 100 jiffies for the
 *     next frame done interrupt; else unlock.
 *  2. mesh == 1: the mesh words (vendor info + 13852, here t32_lsc_cell)
 *     through the LUT port, one sensor: the LSC list image
 *     (tisp_single_lsc_reg_write / _reg_lut_write: 18 = 0x101, 20 + k = word k,
 *     3608 = (cells - 1) << 16 | 0x102), else under the channel lock (ctx + 16)
 *     tisp_reg_write / tisp_reg_lut_write to the port 0x32020 / 0x32024
 *     (shadow mode d03a0 = 1: indices 2767 / 2769) and the close write to
 *     port + 3590.  tisp_get_system_sensor_num is asked again for every cell
 *     and for the close write.  Cells: byte 65 set: 1152 (cur word 4 set) or
 *     576, else the grid (info + 4); at most 1152.
 *  3. registers by mode (r = t32_lsc_registers, wdr from cur word 7):
 *     d03a0 == 1 (tisp_reg_write, shadow indices): mode 0 / 1: 2757 = r4;
 *       mode 0 and 2: 2754 = r1, 2760 = r7; mode 0 and 3: 2753 r0, 2755 r2,
 *       2765 r12, 2758 r5, 2759 r6, 2761 r8, 2762 r9; always 2751 = 1.
 *     else, sonly == 1: mode 0 / 1 only: tisp_reg_write(d03a0 ? 2757 : 0x5050,
 *       r4) and image 8 = r4 (mode >= 2: nothing).
 *     else (LSC list image): mode 0 / 1: 8 = r4; mode 0 and 2: 5 = r1,
 *       11 = r7; mode 0 and 3: 4 r0, 6 r2, 16 r12, 9 r5, 10 r6, 12 r8,
 *       13 r9; always 2 = 1, then the list trigger is armed (e180c = 1).
 * tisp_lsc_irq_frame_done (interrupt bit 36): frame counter (u16, e17d0[ch]);
 * under the spin lock: d03a0 != 1 and armed -> tisp_single_lsc_rcfg_cfg_trig
 * (the LSC list runs), disarm; a waiter -> clear, complete.
 *
 * Vendor behaviour kept: the close write of the one-sensor path goes to index
 * 3608 of the LSC list image, 4 bytes behind the vendor's 14428-byte list
 * (the list's own close command at word 3477 keeps its value 0); the
 * many-sensor direct mode closes at 0x32e26 (0x32020 + 3590, unaligned); a
 * grid of 0 cells writes all 1152 cells and closes with 0xffff0102.
 * Vendor UB not taken over: e17d0[ch] for ch >= 4 (ours: 4 channels).
 * The OS calls are hooks (struct t32_lscct_ops).  Checked against the vendor
 * flow by tests/t32_oracle (t32_lscct_oracle).
 */
#ifndef TX_ISP_T32_LSCCT_H
#define TX_ISP_T32_LSCCT_H

#include "tx_isp_t32_lsc.h"

#define T32_LSCCT_CHANS		4
#define T32_LSCCT_BUSY_REG	0x22014U	/* register list DMA running */
#define T32_LSCCT_WAIT		100U		/* jiffies */
#define T32_LSCCT_MAX_WORDS	3456U
#define T32_LSCCT_CLOSE_OFF	3590U		/* close write: port select + 3590 */
#define T32_LSCCT_IMG_WORDS	3609U		/* the LSC list image incl. the close slot 3608 */

struct t32_lscct_ops {
	u32 (*reg_read)(void *priv, u32 reg);					/* system_reg_read */
	void (*reg_write)(void *priv, u8 ch, u32 reg, u32 val);			/* tisp_reg_write */
	void (*reg_lut_write)(void *priv, u8 ch, u32 reg, u32 val, u32 off);	/* tisp_reg_lut_write */
	u32 (*sensor_num)(void *priv);						/* tisp_get_system_sensor_num */
	void (*mutex_lock)(void *priv);						/* t32_data_e1800 */
	void (*mutex_unlock)(void *priv);
	unsigned long (*lock)(void *priv);					/* t32_small_bss */
	void (*unlock)(void *priv, unsigned long f);
	unsigned long (*ch_lock)(void *priv, u8 ch);				/* ctx + 16 */
	void (*ch_unlock)(void *priv, u8 ch, unsigned long f);
	void (*comp_init)(void *priv);						/* t32_data_e17f4 */
	long (*comp_wait)(void *priv, u32 timeout);
	void (*complete)(void *priv);
	void (*print_wait)(void *priv);						/* isp_printf(0, ...) */
	void (*cfg_trig)(void *priv, u8 ch);					/* tisp_single_lsc_rcfg_cfg_trig */
};

struct t32_lscct {
	struct t32_lsc_ctx *lsc;	/* sec, s, wk, cur, img, bayer */
	const struct t32_lscct_ops *ops;
	void *priv;
	const u8 *shadow;		/* t32_data_d03a0 */
	u8 armed;			/* t32_data_e180c: run the LSC list at the next frame done */
	u8 waiting;			/* t32_data_e17f0: write_all_reg waits for the frame done */
	u32 frames[T32_LSCCT_CHANS];	/* t32_data_e17d0 */
};

/* step 1: drop the pending trigger, wait for a running register list */
static inline void t32_lscct_wait_idle(struct t32_lscct *w)
{
	const struct t32_lscct_ops *o = w->ops;
	unsigned long f = o->lock(w->priv);

	w->armed = 0;
	if (o->reg_read(w->priv, T32_LSCCT_BUSY_REG)) {
		w->waiting = 1;
		o->comp_init(w->priv);
		o->unlock(w->priv, f);
		o->print_wait(w->priv);
		o->comp_wait(w->priv, T32_LSCCT_WAIT);
	} else {
		o->unlock(w->priv, f);
	}
}

/* step 2: the mesh through the LUT port */
static inline void t32_lscct_mesh(struct t32_lscct *w, u8 ch)
{
	const struct t32_lscct_ops *o = w->ops;
	struct t32_lsc_ctx *c = w->lsc;
	u32 cells = c->sec[65] ? (c->cur->rsvd1[1] ? 1152U : 576U) : c->s.cells;
	u32 sel, data, k, wd[3];
	unsigned long f = 0;

	if (o->sensor_num(w->priv) == 1) {
		t32_lsc_img_write(c, ch, T32_LSC_IMG_SEL, 0x101);
		sel = T32_LSC_IMG_SEL;
		data = T32_LSC_IMG_MESH;
	} else {
		sel = *w->shadow ? 2767U : T32_LSC_PORT_SEL;
		data = *w->shadow ? 2769U : T32_LSC_PORT_DATA;
		f = o->ch_lock(w->priv, ch);
		o->reg_write(w->priv, ch, sel, 0x101);
	}
	for (k = 0;;) {
		int one = o->sensor_num(w->priv) == 1;
		unsigned int j;

		t32_lsc_cell(c->sec, &c->wk, k / 3, wd);
		wd[2] &= 0xff;		/* the vendor stores the third word as a byte */
		for (j = 0; j < 3; j++) {
			if (one)
				t32_lsc_img_write(c, ch, data + k + j, wd[j]);
			else
				o->reg_lut_write(w->priv, ch, data, wd[j], k + j);
		}
		if (k == cells * 3 - 3)
			break;
		k += 3;
		if (k == T32_LSCCT_MAX_WORDS)
			break;
	}
	if (o->sensor_num(w->priv) == 1) {
		t32_lsc_img_write(c, ch, sel + T32_LSCCT_CLOSE_OFF, (cells - 1) << 16 | 0x102);
	} else {
		o->reg_write(w->priv, ch, sel + T32_LSCCT_CLOSE_OFF, (cells - 1) << 16 | 0x102);
		o->ch_unlock(w->priv, ch, f);
	}
}

/* step 3, shadow mode d03a0 == 1: tisp_reg_write with the shadow indices */
static inline void t32_lscct_regs_shadow(struct t32_lscct *w, u8 ch, u8 mode)
{
	const struct t32_lscct_ops *o = w->ops;
	const u32 *r = w->lsc->s.regs;

	if (mode < 2)
		o->reg_write(w->priv, ch, 2757, r[4]);
	if (mode == 0 || mode == 2) {
		o->reg_write(w->priv, ch, 2754, r[1]);
		o->reg_write(w->priv, ch, 2760, r[7]);
	}
	if (mode == 0 || mode == 3) {
		o->reg_write(w->priv, ch, 2753, r[0]);
		o->reg_write(w->priv, ch, 2755, r[2]);
		o->reg_write(w->priv, ch, 2765, r[12]);
		o->reg_write(w->priv, ch, 2758, r[5]);
		o->reg_write(w->priv, ch, 2759, r[6]);
		o->reg_write(w->priv, ch, 2761, r[8]);
		o->reg_write(w->priv, ch, 2762, r[9]);
	}
	o->reg_write(w->priv, ch, 2751, 1);
}

/* step 3, else: the LSC list image (sonly: only the strength, register and image) */
static inline void t32_lscct_regs_list(struct t32_lscct *w, u8 ch, u8 mode, u8 sonly)
{
	struct t32_lsc_ctx *c = w->lsc;
	const u32 *r = c->s.regs;

	if (sonly == 1) {
		if (mode >= 2)
			return;
		w->ops->reg_write(w->priv, ch, *w->shadow ? 2757U : T32_LSC_CTRL0 + 0x10, r[4]);
		t32_lsc_img_write(c, ch, 8, r[4]);
		return;
	}
	if (mode < 2)
		t32_lsc_img_write(c, ch, 8, r[4]);
	if (mode == 0 || mode == 2) {
		t32_lsc_img_write(c, ch, 5, r[1]);
		t32_lsc_img_write(c, ch, 11, r[7]);
	}
	if (mode == 0 || mode == 3) {
		t32_lsc_img_write(c, ch, 4, r[0]);
		t32_lsc_img_write(c, ch, 6, r[2]);
		t32_lsc_img_write(c, ch, 16, r[12]);
		t32_lsc_img_write(c, ch, 9, r[5]);
		t32_lsc_img_write(c, ch, 10, r[6]);
		t32_lsc_img_write(c, ch, 12, r[8]);
		t32_lsc_img_write(c, ch, 13, r[9]);
	}
	t32_lsc_img_write(c, ch, T32_LSC_IMG_EN, 1);
	w->armed = 1;
}

/* tisp_lsc_write_all_reg(ch, mode, sonly, mesh), always 0 */
static inline int t32_lscct_write_all_reg(struct t32_lscct *w, u8 ch, u8 mode, u8 sonly, u8 mesh)
{
	struct t32_lsc_ctx *c = w->lsc;

	w->ops->mutex_lock(w->priv);
	t32_lscct_wait_idle(w);
	if (mesh == 1)
		t32_lscct_mesh(w, ch);
	t32_lsc_registers(c->sec, &c->s, c->cur->wdr == 1);
	if (*w->shadow == 1)
		t32_lscct_regs_shadow(w, ch, mode);
	else
		t32_lscct_regs_list(w, ch, mode, sonly);
	w->ops->mutex_unlock(w->priv);
	return 0;
}

/*
 * tisp_lsc_interp_by_ct(ch, ct, force): t32_lsc_update (tx_isp_t32_lsc.h) with the vendor's
 * last colour temperature (cur word 2) as its reference and the vendor's channel lock
 * (ctx + 16): one around the table choice and a single-table copy, a second one around
 * a two-table blend (tisp_lsc_mesh_itp / _ring_itp; mesh_itp takes the same lock once
 * more inside: nested, vendor behaviour kept, so the driver's hook must allow it), a third
 * around the packing of the mesh words (vendor info + 13852; t32_lsc_cell packs them
 * where they are written).  0 = unchanged (not forced and |ct - last| <= u32 p[12]), else 1.
 */
static inline int t32_lscct_interp_by_ct(struct t32_lscct *w, u8 ch, u32 ct, u8 force)
{
	const struct t32_lscct_ops *o = w->ops;
	struct t32_lsc_ctx *c = w->lsc;
	const u8 *p = c->sec;
	struct t32_lsc *s = &c->s;
	u32 last = c->cur->ct, diff = ct > last ? ct - last : last - ct;
	unsigned int plane, e, n;
	unsigned long f;
	int blend;

	if (!force && !(t32_le32(p + 12) < diff))
		return 0;
	c->cur->ct = ct;
	c->wk.old_ct = ct;
	f = o->ch_lock(w->priv, ch);
	t32_lsc_ct(p, s, ct);
	blend = !s->none && s->table_lo != s->table_hi;
	if (blend) {	/* two tables: interpolated under a second lock */
		o->ch_unlock(w->priv, ch, f);
		f = o->ch_lock(w->priv, ch);
		if (!p[65]) {	/* tisp_lsc_mesh_itp takes the same lock again (nested) */
			unsigned long f2 = o->ch_lock(w->priv, ch);

			o->ch_unlock(w->priv, ch, f2);
		}
	}
	if (!s->none) {
		n = p[65] ? T32_LSC_RING_ELEMS : 2304U;
		for (plane = 0; plane < 3; plane++)
			for (e = 0; e < n; e++)
				c->wk.pl[plane][e] = (u16)(p[65] ? t32_lsc_ring_elem(p, s, plane, e) :
							   t32_lsc_raw_elem(p, s, plane, e));
	}
	o->ch_unlock(w->priv, ch, f);
	if (!p[65])
		for (plane = 0; plane < 3; plane++)
			for (e = 0; e < 4 * (unsigned int)s->rows * s->cols && e < 2304; e++)
				c->wk.pl[plane][e] = (u16)t32_lsc_radial(p, s, plane, e, c->wk.pl[plane][e]);
	f = o->ch_lock(w->priv, ch);
	o->ch_unlock(w->priv, ch, f);
	return 1;
}

/* tisp_lsc_ct_update(ch, ct): a changed mesh goes out with write_all_reg(ch, 2, 0, 1); always 0 */
static inline int t32_lscct_ct_update(struct t32_lscct *w, u8 ch, u32 ct)
{
	if (t32_lscct_interp_by_ct(w, ch, ct, 0) == 1)
		t32_lscct_write_all_reg(w, ch, 2, 0, 1);
	return 0;
}

/* tisp_lsc_tgain_update(ch, gain): interp_by_tgain (cur word 5, strength), write_all_reg(ch, 1, 1, 0) */
static inline int t32_lscct_tgain_update(struct t32_lscct *w, u8 ch, u32 gain)
{
	struct t32_lsc_ctx *c = w->lsc;
	struct t32_lsc t = c->s;

	c->cur->tgain = gain;
	t32_lsc_gain(c->sec, &t, gain);
	c->s.strength = t.strength;
	t32_lscct_write_all_reg(w, ch, 1, 1, 0);
	return 0;
}

/* tisp_lsc_irq_frame_done (interrupt bit 36), always 1; ch < T32_LSCCT_CHANS */
static inline int t32_lscct_frame_done(struct t32_lscct *w, u8 ch)
{
	const struct t32_lscct_ops *o = w->ops;
	unsigned long f;

	w->frames[ch] = (w->frames[ch] + 1) & 0xffff;
	f = o->lock(w->priv);
	if (*w->shadow != 1 && w->armed == 1) {
		o->cfg_trig(w->priv, ch);
		w->armed = 0;
	}
	if (w->waiting == 1) {
		w->waiting = 0;
		o->complete(w->priv);
	}
	o->unlock(w->priv, f);
	return 1;
}

/*
 * The LSC list image as the vendor's tisp_lsc_init leaves it, for a driver that runs
 * the list itself: the headers and the length of tisp_single_lsc_rcfg_reg_init
 * (t32_rcfgv_lsc_reg_init, rcfgv.h, to be called first), then the values of
 * write_all_reg(ch, 0, 0, 1) and the constants of tisp_lsc_wake_up (7 = 1,
 * 14 = 0, 15 = 0x04000400).  Writes only the list image (no hooks).
 */
static inline void t32_lscct_img_fill(struct t32_lsc_ctx *c, u8 ch)
{
	u32 cells = c->sec[65] ? (c->cur->rsvd1[1] ? 1152U : 576U) : c->s.cells;
	u32 k, wd[3];

	if (!cells || cells > T32_LSCCT_MAX_WORDS / 3)
		cells = T32_LSCCT_MAX_WORDS / 3;
	t32_lsc_registers(c->sec, &c->s, c->cur->wdr == 1);
	for (k = 0; k < 13; k++)
		t32_lsc_img_write(c, ch, T32_LSC_IMG_REG0 + k, c->s.regs[k]);
	t32_lsc_img_write(c, ch, T32_LSC_IMG_EN, 1);
	t32_lsc_img_write(c, ch, T32_LSC_IMG_SEL, 0x101);
	for (k = 0; k < cells; k++) {
		unsigned int j;

		t32_lsc_cell(c->sec, &c->wk, k, wd);
		wd[2] &= 0xff;
		for (j = 0; j < 3; j++)
			t32_lsc_img_write(c, ch, T32_LSC_IMG_MESH + 3 * k + j, wd[j]);
	}
}

#endif /* TX_ISP_T32_LSCCT_H */
