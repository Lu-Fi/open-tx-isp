/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 block interrupts of the vendor tisp core (openimp #234): the per-frame
 * paths the vendor does not run from the AE process but from interrupt bits
 * of the ISP core (status word 0x30028, lib_tisp_intp_process) with their own
 * DMA buffers.
 *
 *   LCE (tisp_lce_init):  system_irq_func_set (ch, 8, tisp_lce_interrupt_static),
 *                         system_irq_func_set (ch, 30, tisp_lsf_interrupt_static),
 *                         tisp_event_set_cb (ch, 14, tisp_lce_par)
 *   TMO (tisp_tmo_init):  bits 9 (tisp_tmo_interrupt_static) and 31 (tisp_tmo_fbf_interrupt_calc);
 *                         not wired yet (T32_BLK_IRQ_TMO / _FBF only name the bits).
 *
 * The vendor globals of these paths are one object here (struct t32_blk, one
 * instance t32_blk per translation unit, as the vendor's .bss):
 *   lce_st   t32_data_e1da8: the LCE state per channel (32 bit address)
 *   buf      t32_data_e1db8: the buffer set of tisp_lce_malloc_cfg (t32_lce_bufset)
 *   bank     t32_data_e1d7c, last_ch t32_data_e1d78 (tisp_lce_interrupt_static)
 *   band     t32_data_e188c: the 4 x 3 u16 band thresholds tisp_lce_init derives from the
 *            pixel block counts (t32_lce_band); read by Tisp_lce_soft_simd_v4
 * The handlers run the oracle-proven ports unchanged: t32_lce_interrupt_static,
 * t32_lsf_interrupt_static (tx_isp_t32_lsf.h), t32_lce_par with
 * Tisp_lce_soft_simd_v4 = t32_lce_v4 / Tisp_lce_soft_t41_simd = t32_lce_t41 and
 * tisp_lce_write_curve = t32_lce_write_curve, the statistics readers
 * t32_lce_get_sum_data / t32_lce_get_hist_data.  The leaves of the two soft
 * paths are hooked to their ports as in t32_lcev4_oracle / t32_lcet41_oracle,
 * the tables are the vendor's data (data.c lines below; d6c64 .. d6c67 follow the frame size as there).
 * The OS calls (register access, cache sync, the lock of tisp_lce_par, the
 * event queue) are struct t32_blk_ops.  kernel_mxu_begin_partial /
 * kernel_mxu_end save the MXU registers for the vendor's SIMD code; the ports
 * are plain C and need nothing.
 *
 * Checked against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_blkirq_oracle): the buffer set, band table and the
 * registered bits / event after tisp_lce_init, then interrupt bit 8, bit 30
 * and event 14 on the vendor's and our state (register writes, state, curve
 * buffer, event).
 */
#ifndef TX_ISP_T32_BLKIRQ_H
#define TX_ISP_T32_BLKIRQ_H

#include "tx_isp_t32_blkrest.h"
#include "tx_isp_t32_lsf.h"
#include "tx_isp_t32_lcem.h"
#include "tx_isp_t32_lce.h"
#include "tx_isp_t32_lcetg.h"
#include "tx_isp_t32_libintp.h"
#include "tx_isp_t32_event.h"

/* interrupt bits (system_irq_func_set index) and the event of the block paths */
#define T32_BLK_IRQ_LCE		8	/* tisp_lce_interrupt_static */
#define T32_BLK_IRQ_TMO		9	/* tisp_tmo_interrupt_static (not wired) */
#define T32_BLK_IRQ_LSF		30	/* tisp_lsf_interrupt_static */
#define T32_BLK_IRQ_FBF		31	/* tisp_tmo_fbf_interrupt_calc (not wired) */
#define T32_BLK_EV_LCE_PAR	14	/* tisp_lce_par */

/* tisp_lce_malloc_cfg: private_kmalloc sizes of the three buffers */
#define T32_LCE_BUF_CURVE	0x5a00U		/* +12, two curve halves of 11520 */
#define T32_LCE_BUF_STAT	0x26400U	/* +16, four statistics banks of 0x9900 */
#define T32_LCE_BUF_HIST	0x9000U		/* +20; tisp_lsf_interrupt_static syncs 0x9900 of it */

struct t32_blk_ops {
	u32 (*reg_read)(u32 reg);			/* system_reg_read */
	void (*reg_write)(u32 ch, u32 reg, u32 val);	/* tisp_reg_write */
	void (*cache_sync)(void *p, u32 len);		/* private_dma_cache_sync (0, p, len, 0) */
	u32 (*lock)(void);				/* the spin lock of tisp_lce_par (state + 0x2284d) */
	void (*unlock)(u32 flags);
	void (*event_push)(u32 ch, const struct t32_event *ev);	/* tisp_event_push */
};

struct t32_blk {
	const struct t32_blk_ops *ops;
	const u8 *shadow;			/* t32_data_d03a0 */
	u32 lce_st[T32_EV_CHANS];		/* t32_data_e1da8 */
	struct t32_lci_buf buf[T32_EV_CHANS];	/* t32_data_e1db8 */
	u32 bank;				/* t32_data_e1d7c */
	u8 last_ch;				/* t32_data_e1d78 */
	u16 band[12];				/* t32_data_e188c */
	struct t32_lce_v4_st v4;		/* Tisp_lce_soft_simd_v4 scratch (vendor .bss e1e48 ..) */
	u32 v4_refused;				/* t32_lce_v4 refused the parameters (vendor overruns) */
};

static struct t32_blk t32_blk;

typedef char t32_blk_bufset_is_36_bytes[sizeof(struct t32_lci_buf) == T32_LCE_BUFSET_BYTES ? 1 : -1];

/*
 * tisp_lce_malloc_cfg: the buffer set of channel ch.  The vendor keeps the
 * kmalloc addresses (+12 curve, +16 statistics, +20 histogram), their
 * physical addresses (+24 / +28 / +32, virtual + 0x80000000 there) and the
 * words +0 = 2, +4 = 4, +8 = 1.  Its five register writes (0xc090 ..
 * 0xc09c, 0xc080) are t32_lce_init's.
 */
static inline void t32_lce_bufset(struct t32_lci_buf *b, u32 curve, u32 curve_pa, u32 stat, u32 stat_pa,
				  u32 hist, u32 hist_pa)
{
	b->rsvd[0] = 2;
	b->rsvd[1] = 4;
	b->rsvd[2] = 1;
	b->curve = curve;
	b->stat = stat;
	b->hist = hist;
	b->curve_phys = curve_pa;
	b->stat_phys = stat_pa;
	b->hist_phys = hist_pa;
}

/*
 * tisp_lce_init: the band table e188c from the four pixel block counts of
 * the zone kinds (t32_lce_grid; 1920 x 1080: 0xe10 each, rodata bf924):
 * per count c, q = (c >> 2) & 0xffff, e = q & 1:
 * { ((c >> 3) & 0xffff) + e, q + e, ((c >> 3) & 0xffff) + q + e }.
 */
static inline void t32_lce_band(u32 width, u32 height, u16 band[12])
{
	u32 cnt[4], inv[4], k, e, q, h;
	u8 grid[4];

	if (width == 1920 && height == 1080)
		cnt[0] = cnt[1] = cnt[2] = cnt[3] = 0xe10;
	else
		t32_lce_grid(width, height, grid, cnt, inv);
	for (k = 0; k < 4; k++) {
		h = (cnt[k] >> 3) & 0xffff;
		q = (cnt[k] >> 2) & 0xffff;
		e = q & 1;
		band[3 * k] = (u16)(h + e);
		band[3 * k + 1] = (u16)(q + e);
		band[3 * k + 2] = (u16)(h + q + e);
	}
}

/* ---- the leaves of the two soft paths: their ports, the vendor rodata ---- */

/* data.c:2717..2733 (bf9e8): bf9ec = the 32 halfwords after the index -1 halfword */
static const u16 t32_blk_bf9ec[32] = {
	0x7fff, 0x2aab, 0x1555, 0x0ccd, 0x0889, 0x0618, 0x0492, 0x038e, 0x02d8, 0x0254, 0x01f0, 0x01a4, 0x0168, 0x0138,
	0x0111, 0x00f1, 0x00d6, 0x00c0, 0x00ac, 0x009c, 0x008e, 0x0082, 0x0077, 0x006d, 0x0065, 0x005d, 0x0057, 0x0051,
	0x004b, 0x0046, 0x0042, 0x003e,
};
/* data.c:25729..25733 (d6b50) */
static const u8 t32_blk_d6b50[20] = {
	0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0,
};
/*
 * data.c:25798 (d6c64 = 0x14140c0b): the bytes d6c64 / d6c65 / d6c66 / d6c67 (pdf round, pdf shift, the two
 * hist_method shifts).  Not constant: tisp_lce_size_para_calc rewrites d6c67 from the full zone count (21 .. 24) and
 * d6c65 / d6c64 = d6c67 - 8 / - 9 (t32_lce_size_shifts); 1920 x 1080 skips it and keeps the last values, as here
 * (module lifetime, like the vendor's .data).
 */
static u8 t32_blk_d6c[4] = { 0x0b, 0x0c, 0x14, 0x14 };

static inline void t32_lce_size_shifts(u32 width, u32 height, u8 d6c[4])
{
	u32 cnt[4], inv[4], s;
	u8 grid[4];

	if (width == 1920 && height == 1080)
		return;
	t32_lce_grid(width, height, grid, cnt, inv);
	s = cnt[3] >= 8192 ? 24 : cnt[3] >= 4096 ? 23 : cnt[3] >= 2048 ? 22 : 21;
	d6c[3] = (u8)s;
	d6c[1] = (u8)(s - 8);
	d6c[0] = (u8)(s - 9);
}

static void t32_blk_v4_ts(const u16 h[16][32], const u16 *t, u8 *a, u8 *b, u8 *c) { t32_lce_thres_search_simd(h, t, a, b, c); }
static void t32_blk_v4_t(const u16 *h, const u16 *t, u8 *o) { t32_lce_thres_search(h, t, o); }
static struct t32_lce_tab t32_blk_tab;
static int t32_blk_v4_hm(u16 *h, const u8 *t, const u8 *p) { return t32_lce_hist_method(h, t, p, &t32_blk_tab); }
static void t32_blk_v4_ha(u8 *c, const u8 *u, const u8 *d) { t32_lce_hist_adjust(c, u, d, &t32_blk_tab); }
static void t32_blk_cv(u32 g, u32 lo, u32 hi, const u8 *t, u8 *c) { t32_lce_16bit_data_converge(g, lo, hi, t, c); }
static void t32_blk_cvs(u32 g, u32 lo, u32 hi, const u8 *t, u8 *c) { t32_lce_16bit_data_converge_simd(g, lo, hi, t, c); }

static const struct t32_lce_v4_ops t32_blk_v4_ops = {
	t32_blk_v4_ts, t32_blk_v4_t, t32_blk_v4_hm, t32_blk_v4_ha, t32_blk_cv, t32_blk_cvs, &t32_blk_tab,
};

static void t32_blk_t41_hfj(const u32 *h, u8 *o) { t32_lce_hist_filter_and_judge(h, o); }
static void t32_blk_t41_hm(const u32 *h, u32 *o, const u32 *p)
{
	t32_lce_hist_method32(h, o, (const struct t32_lce_hm16_par *)p, t32_blk_d6c[2], t32_blk_d6c[3]);
}
static void t32_blk_t41_cdf(const u32 *p, u8 *o) { t32_lce_pdf_to_cdf_raw(p, o); }
static void t32_blk_t41_slc(const u8 *i, u8 *o, const u8 *e, const u8 *u, const u8 *d)
{
	t32_lce_self_light_correct_local(i, o, e, u, d);
}
static void t32_blk_t41_hfj16(const u16 *h, u8 *o) { t32_lce_hist_filter_and_judge_16bit(h, o); }
static void t32_blk_t41_slct(const u8 *i, u8 *o, u32 en, const u8 *u, const u8 *d)
{
	t32_lce_self_light_correct_local_simd_test(i, o, en, u, d);
}

static struct t32_lce_t41_ops t32_blk_t41_ops = {	/* shift_byte = d6c67, set by t32_blk_tab_init */
	t32_blk_t41_hfj, t32_blk_t41_hm, t32_blk_t41_cdf, t32_blk_t41_slc, t32_blk_cv, t32_blk_t41_hfj16,
	t32_blk_t41_slct, 0x14,
};

/* the v4 tables (struct t32_lce_tab): d6b50, d6c64 / 65 / 67, bf9ec, d6be4 = 1 .. 32, d6b64 = 32 .. 1 (data.c:25734, 25766) */
static inline void t32_blk_tab_init(struct t32_lce_tab *t)
{
	unsigned int i;

	memcpy(t->adj_base, t32_blk_d6b50, 20);
	t->pdf_rnd = t32_blk_d6c[0];
	t->pdf_sh = t32_blk_d6c[1];
	t->pdf_shift = t32_blk_d6c[3];
	t32_blk_t41_ops.shift_byte = t32_blk_d6c[3];
	memcpy(t->w, t32_blk_bf9ec, sizeof(t->w));
	for (i = 0; i < 32; i++) {
		t->up[i] = i + 1;
		t->down[i] = 32 - i;
	}
}

/* ---- hooks of the ported interrupt / event functions on t32_blk ---- */

static u8 *t32_blk_st(u32 ch) { return (u8 *)(uintptr_t)t32_blk.lce_st[ch]; }

static void t32_blk_get_sum(u32 ch, u8 *stat)
{
	struct t32_lcem l = { .st = { t32_blk_st(0), t32_blk_st(1), t32_blk_st(2), t32_blk_st(3) } };

	t32_lce_get_sum_data(&l, ch, stat);
}

static void t32_blk_get_hist(u32 ch, u32 buf)
{
	struct t32_lcem l = { .st = { t32_blk_st(0), t32_blk_st(1), t32_blk_st(2), t32_blk_st(3) } };

	t32_lce_get_hist_data(&l, ch, (const u8 *)(uintptr_t)buf);
}

static u32 t32_blk_rd(u32 reg) { return t32_blk.ops->reg_read(reg); }
static void t32_blk_wr(u32 ch, u32 reg, u32 val) { t32_blk.ops->reg_write(ch, reg, val); }
static void t32_blk_sync(void *p, u32 len) { t32_blk.ops->cache_sync(p, len); }

static const struct t32_lci_ops t32_blk_lci_ops = { t32_blk_rd, t32_blk_wr, t32_blk_sync, t32_blk_get_sum };

static void t32_blk_lsf_sync(void *dev, u32 addr, u32 size, int dir)
{
	(void)dev; (void)dir;
	t32_blk.ops->cache_sync((void *)(uintptr_t)addr, size);
}

/* tisp_lsf_interrupt_static pushes 8 words (type at +8); the vendor's words +32 .. +47 are stack contents, ours 0 */
static void t32_blk_lsf_push(u32 ch, const u32 *ev)
{
	struct t32_event e;

	memset(&e, 0, sizeof(e));
	memcpy(&e, ev, 8 * sizeof(u32));
	t32_blk.ops->event_push(ch, &e);
}

static const struct t32_lsf_ops t32_blk_lsf_ops = { t32_blk_lsf_sync, t32_blk_get_hist, t32_blk_lsf_push };

static u32 t32_blk_lock(void *lock) { (void)lock; return t32_blk.ops->lock(); }
static void t32_blk_unlock(void *lock, u32 flags) { (void)lock; t32_blk.ops->unlock(flags); }
static void t32_blk_mxu_begin(u32 zero) { (void)zero; }
static void t32_blk_mxu_end(void) { }

static void t32_blk_soft_v4(u32 ch, u8 *par, u8 *w, u32 update)
{
	(void)ch;
	if (t32_lce_v4(par, w, update, t32_blk.band, &t32_blk_v4_ops, &t32_blk.v4) < 0)
		t32_blk.v4_refused++;
}

static void t32_blk_soft_t41(u32 ch, u8 *par, u8 *w, u32 update)
{
	(void)ch;
	t32_lce_t41(par, w, update, &t32_blk_t41_ops);
}

static void t32_blk_write_curve(u32 ch)
{
	t32_lce_write_curve(t32_blk_st(ch), (u8 *)(uintptr_t)t32_blk.buf[ch].curve);
}

static const struct t32_lcep_ops t32_blk_lcep_ops = {
	t32_blk_lock, t32_blk_unlock, t32_blk_mxu_begin, t32_blk_mxu_end, t32_blk_soft_v4, t32_blk_soft_t41,
	t32_blk_write_curve,
};

/* ---- the registered handlers ---- */

/* interrupt bit 8: tisp_lce_interrupt_static */
static u32 t32_blk_irq_lce(u32 ch)
{
	struct t32_lci l = { &t32_blk_lci_ops, t32_blk.buf, t32_blk.shadow, &t32_blk.bank, &t32_blk.last_ch };

	return (u32)t32_lce_interrupt_static(&l, ch);
}

/* interrupt bit 30: tisp_lsf_interrupt_static (histogram, event 14) */
static u32 t32_blk_irq_lsf(u32 ch)
{
	return (u32)t32_lsf_interrupt_static(&t32_blk_lsf_ops, (u8)ch, (const u8 *)t32_blk.buf);
}

/* event 14: tisp_lce_par */
static int t32_blk_ev_lce_par(u32 ch, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7)
{
	(void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7;
	return t32_lce_par(&t32_blk_lcep_ops, t32_blk.lce_st, ch);
}

/*
 * The LCE part of tisp_lce_init for channel ch: state st (t32_lce_state_init),
 * the buffer set, the band table, then the two interrupt bits and the event
 * (system_irq_func_set / tisp_event_set_cb: irq[bit], ev[ch * 18 + type]).
 */
static inline void t32_blk_lce_init(u32 ch, u8 *st, u32 width, u32 height, const u32 va[3], const u32 pa[3],
				    t32_intp_fn *irq, t32_ev_handler *ev)
{
	t32_lce_size_shifts(width, height, t32_blk_d6c);
	t32_blk_tab_init(&t32_blk_tab);
	t32_blk.lce_st[ch] = (u32)(uintptr_t)st;
	t32_lce_bufset(&t32_blk.buf[ch], va[0], pa[0], va[1], pa[1], va[2], pa[2]);
	t32_lce_band(width, height, t32_blk.band);
	irq[T32_BLK_IRQ_LCE] = t32_blk_irq_lce;
	irq[T32_BLK_IRQ_LSF] = t32_blk_irq_lsf;
	ev[ch * T32_EV_TYPES + T32_BLK_EV_LCE_PAR] = t32_blk_ev_lce_par;
}

/*
 * lib_tisp_intp_process on the status word 0x30028 (st[0]; the block bits
 * live there), handlers irq[]: the registered bits in ascending order.
 */
static inline u32 t32_blk_dispatch(struct t32_intp_glob *g, t32_intp_fn const *irq, u32 st0, u32 ch)
{
	g->st[0] = st0;
	g->st[3] = 0;
	g->st[4] = 0;
	return t32_lib_intp_process(g, irq, ch);
}

#endif /* TX_ISP_T32_BLKIRQ_H */
