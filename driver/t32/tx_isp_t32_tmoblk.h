/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 TMO / ADR as the vendor runs it (openimp #234): tisp_tmo_init and the
 * two block interrupts it registers, composed from the oracle-proven ports of
 * tx_isp_t32_tmo.h (tables: tx_isp_t32_tmotab.h).
 *
 *   tisp_tmo_init (ch, info):  tisp_tmo_malloc_cfg (statistics DMA buffer 0x10000), the channel record
 *                              (stat, out curve, geometry, big block, tone buffer), size_update,
 *                              fbf_malloc_cfg, params_hard_refresh, the ev interpolations, the default
 *                              register refreshes, modechange_stat_refresh + the ramY / ramV refresh,
 *                              the adr block set, adr_gamma_refresh, register 28960 = 1, then
 *                              system_irq_func_set (ch, 9, tisp_tmo_interrupt_static) and
 *                              (ch, 31, tisp_tmo_fbf_interrupt_calc).
 *   bit 9:   tisp_tmo_interrupt_static: statistics half of the DMA buffer -> tmoadr_get_statistics; stat
 *            byte 385 == 1 (TMO): tmo2fbf_input_prep + tmo2fbf_process; == 0 (ADR): adr_soft_process
 *            (adr_stat_calc, adr_ev_func, adr_tgain_func, adr_algorithm -> adr_fpga, adr_reg_write_every).
 *   bit 31:  tisp_tmo_fbf_interrupt_calc: stat byte 385 == 1: tmo_soft_process of the current channel
 *            (first time tmo_default_reg_refresh, tmo_stat_limit, ramY / ramV refresh).
 *   per frame from the AE: tisp_tmo_tgain_update (tisp_long_tgain_update), tisp_tmo_ev_update
 *            (tisp_long_ev_update).
 * tisp_tmo_init copies ctx byte 10042 (set to 1 there) into stat byte 385: after the init the TMO path
 * runs; the ADR path needs a context with byte 10042 = 0 (tisp_tmo_param_array_set).
 *
 * The vendor's module globals are one object (struct t32_tmob, one instance t32_tmob per translation
 * unit, as the vendor's .bss / .data); its records hold 32 bit addresses exactly as the vendor's
 * (record words, see t32_tmob_rec), so the memory the init leaves is the vendor's byte for byte.
 * The OS calls (allocators, register access, cache sync, the interrupt table) are struct
 * t32_tmob_ops.  Register numbers: the ports choose the vendor's direct or shadow number by
 * t32_data_d03a0 (here m->d03a0); ops->reg_write is tisp_reg_write (the shadow store in mode 1).
 *
 * Checked against the vendor flow (Matt Davis' recovered C, PR #15 @ 2e2e06bc) by tests/t32_oracle
 * (t32_tmoblk_oracle): the whole memory, the slots and globals after tisp_tmo_init, then frames of
 * interrupt bits 9 / 31 with tgain / ev updates on the vendor chain and on ours, d03a0 = 0 and 1.
 */
#ifndef TX_ISP_T32_TMOBLK_H
#define TX_ISP_T32_TMOBLK_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#endif
#include "tx_isp_t32_tmo.h"
#include "tx_isp_t32_tmotab.h"
#include "tx_isp_t32_aemath.h"		/* t32_aem_exp2 = tisp_math_exp2 */
#include "tx_isp_t32_dispatch.h"	/* t32_lcac_no_refresh */
#include "tx_isp_t32_libintp.h"		/* t32_intp_fn */

#define T32_TMOB_IRQ_TMO	9	/* tisp_tmo_interrupt_static */
#define T32_TMOB_IRQ_FBF	31	/* tisp_tmo_fbf_interrupt_calc */
#define T32_TMOB_SEC		4	/* IQ bin section of the context (tuning base + 11976) */
#define T32_TMOB_SEC_BYTES	11000U
#define T32_TMOB_GAMMA_BYTES	258U	/* adr_gamma_refresh: the first gamma record */
#define T32_TMOB_CH		2	/* channels of the vendor's tables */
#define T32_TMOB_KFLAGS		0x24000c0U	/* the vendor's private_kmalloc flags */

struct t32_tmob_ops {
	void *(*vmalloc)(u32 size);			/* private_vmalloc */
	void *(*kmalloc)(u32 size, u32 flags);		/* private_kmalloc */
	void (*vfree)(void *p);				/* private_vfree */
	void (*kfree)(void *p);				/* private_kfree */
	u32 (*sys_read)(u32 reg);			/* system_reg_read */
	void (*sys_write)(u32 reg, u32 val);		/* system_reg_write */
	void (*reg_write)(u32 ch, u32 reg, u32 val);	/* tisp_reg_write */
	void (*lut_write)(u32 ch, u32 reg, u32 val, u32 idx);	/* tisp_reg_lut_write */
	void (*cache_sync)(void *p, u32 len);		/* private_dma_cache_sync (0, p, len, 0) */
	void (*irq_set)(u32 ch, u32 bit, t32_intp_fn fn);	/* system_irq_func_set */
};

/* the vendor's TMO .bss / .data (names = the vendor symbols) */
struct t32_tmob {
	const struct t32_tmob_ops *ops;
	u8 d03a0;				/* t32_data_d03a0 */
	u32 e35e0[T32_TMOB_CH];			/* channel record (64 B) */
	u32 e35d0[T32_TMOB_CH];			/* malloc_cfg record (12 B: 4, buf, phys) */
	u32 e35c0[T32_TMOB_CH];			/* fbf block (20 B) */
	u32 e35b0[T32_TMOB_CH];			/* adr block set (20 B: s0 800, B 1140, C 10056, wdr) */
	u32 e3580[T32_TMOB_CH], e3570[T32_TMOB_CH];	/* info words 0 / 1 (adr_stat_calc) */
	struct t32_tmo_tgain tg;		/* e35a0 / e3590 / flag = e3520 */
	s16 e3520[T32_TMOB_CH];
	s16 e3528[T32_TMOB_CH];			/* ev pending */
	u64 e3530[T32_TMOB_CH], e3550[T32_TMOB_CH];	/* ev seed / current point */
	u32 e3504;				/* current channel */
	u32 e350c;
	u16 e3516;
	u16 e3518;
	u8 e3510[T32_TMOB_CH];			/* tmo_soft_process: first call done */
	u32 d8b34;
	u8 e3500;				/* face ae flag (tisp_adr_faceae_refresh) */
	u8 e3501;
	u8 e34e8[24];
	s16 e3248[336];
	u32 e3244;
	int e31b0[24];				/* adr_gauss_local (tisp_tmo_param_array_set) */
	u32 e3160[11];
	s16 d88d4[16];				/* .data, written by the y filters of adr_fpga */
	const u8 *gamma[T32_TMOB_CH];		/* t32_data_dc274[ch] + 32: the gamma payload */
};

static struct t32_tmob t32_tmob;

/* module load: the .data initialiser of d88d4 (the .bss rest is zero); adr_tgain_func's flag array is e3520 */
static inline void t32_tmob_load(struct t32_tmob *m)
{
	t32_tmo_fpga_d88d4_init(m->d88d4);
	m->tg.flag = m->e3520;
}

#define T32_TMOB_PTR(a) ((u8 *)(uintptr_t)(a))
#define T32_TMOB_ADDR(p) ((u32)(uintptr_t)(p))

/* the channel record (t32_data_e35e0[ch], 64 bytes) */
static inline u32 *t32_tmob_rw(int ch) { return (u32 *)(uintptr_t)t32_tmob.e35e0[ch]; }
static inline u8 *t32_tmob_rb(int ch) { return T32_TMOB_PTR(t32_tmob.e35e0[ch]); }
static inline u32 *t32_tmob_bw(int ch) { return (u32 *)(uintptr_t)t32_tmob.e35b0[ch]; }

/*
 * The record as the ports see it: +0 ctx, +4 stat, +8 out (201 u16), +12 geometry, +16 big block,
 * +20 tone buffer, +24 / +28 / +32 the ev words, u16 +54 drc on, +56 strength; the vendor's constant
 * curves c029c, d89a0, c0544, c0430.
 */
static void t32_tmob_dma1(u8 *p, u32 len) { t32_tmob.ops->cache_sync(p, len); }

static inline void t32_tmob_rec(int ch, struct t32_tmo_rec *r)
{
	const u32 *w = t32_tmob_rw(ch);
	const u8 *b = (const u8 *)w;

	memset(r, 0, sizeof(*r));
	r->ctx = T32_TMOB_PTR(w[0]);
	r->st = T32_TMOB_PTR(w[1]);
	r->out = (u16 *)(uintptr_t)w[2];
	r->geom = (u16 *)(uintptr_t)w[3];
	r->big = T32_TMOB_PTR(w[4]);
	r->tbuf = T32_TMOB_PTR(w[5]);
	r->ev0 = w[6];
	r->ev1 = w[7];
	r->ev1m = w[8];
	r->drc_on = *(const u16 *)(b + 54);
	r->strength = *(const u16 *)(b + 56);
	r->dflt = t32_tmo_tab_c029c;
	r->d89a0 = t32_tmo_tab_d89a0;
	r->c0544 = t32_tmo_tab_c0544;
	r->c0430 = t32_tmo_tab_c0430;
	r->dma_sync = t32_tmob_dma1;
	r->e3501 = &t32_tmob.e3501;
}

/* ---- register access of the ports ---- */

static void t32_tmob_regw(void *cb, int ch, u32 reg, u32 val) { (void)cb; t32_tmob.ops->reg_write((u32)ch, reg, val); }
static void t32_tmob_sysw(void *cb, u32 reg, u32 val) { (void)cb; t32_tmob.ops->sys_write(reg, val); }
static void t32_tmob_lutw(void *cb, int ch, u32 reg, u32 val, u32 idx) { (void)cb; t32_tmob.ops->lut_write((u32)ch, reg, val, idx); }
static u32 t32_tmob_rd(void *cb, u32 reg) { (void)cb; return t32_tmob.ops->sys_read(reg); }
static void t32_tmob_dma(void *cb, u8 *p, u32 len) { (void)cb; t32_tmob.ops->cache_sync(p, len); }
static void t32_tmob_dma_a(void *cb, u32 a, u32 len) { (void)cb; t32_tmob.ops->cache_sync(T32_TMOB_PTR(a), len); }
static void *t32_tmob_vm(void *cb, u32 size) { (void)cb; return t32_tmob.ops->vmalloc(size); }
static void *t32_tmob_km(void *cb, u32 size, u32 flags) { (void)cb; return t32_tmob.ops->kmalloc(size, flags); }

static inline void t32_tmob_io(struct t32_tmo_ramio *io)
{
	io->cb = 0;
	io->sysw = t32_tmob_sysw;
	io->regw = t32_tmob_regw;
	io->lutw = t32_tmob_lutw;
	io->rd = t32_tmob_rd;
	io->dma = t32_tmob_dma;
	io->d03a0 = t32_tmob.d03a0;
}

/*
 * tisp_lcac_no_refresh (ch) on the TMO context (e35e0[ch] -> +0): the dispatch port writes the direct
 * numbers 0x757c .. 0x7590; the vendor's shadow numbers (d03a0 != 0) are 8070 .. 8075.
 */
static void t32_tmob_lcac_w(u32 ch, u32 reg, u32 val)
{
	t32_tmob.ops->reg_write(ch, t32_tmob.d03a0 != 0 ? 8070U + (reg - 0x757cU) / 4U : reg, val);
}
static const struct t32_dispatch_ops t32_tmob_lcac_ops = { .reg_write = t32_tmob_lcac_w };

/* ---- the register refreshes and the soft processes (one function per vendor callee) ---- */

static void t32_tmob_ramy(int ch)		/* tisp_tmo_ramY_vi1 / _vi0_reg_refresh */
{
	struct t32_tmo_rec r;
	struct t32_tmo_ramio io;

	t32_tmob_rec(ch, &r);
	t32_tmob_io(&io);
	if (t32_tmob.d03a0 == 1)
		t32_tmo_ramY_vi1_reg_refresh(&r, ch, &io);
	else
		t32_tmo_ramY_vi0_reg_refresh(&r, ch, &io);
}

static void t32_tmob_ramv(int ch)		/* tisp_tmo_ramV_vi1 / _vi0_reg_refresh */
{
	struct t32_tmo_ramio io;
	u8 *tb = T32_TMOB_PTR(t32_tmob_rw(ch)[5]);

	t32_tmob_io(&io);
	if (t32_tmob.d03a0 == 1)
		t32_tmo_ramV_vi1_reg_refresh(tb, ch, &io);
	else
		t32_tmo_ramV_vi0_reg_refresh(tb, ch, &io);
}

static void t32_tmob_tmoadr_default(int ch)	/* tisp_tmo_tmoadr_default_reg_refresh */
{
	t32_tmo_tmoadr_default_reg_refresh(T32_TMOB_PTR(t32_tmob_rw(ch)[1]), t32_tmob_rw(ch)[10], t32_tmob.d03a0,
					   t32_tmob_regw, 0, ch);
}

static void t32_tmob_tmo_default(int ch)	/* tisp_tmo_tmo_default_reg_refresh */
{
	t32_tmo_tmo_default_reg_refresh(T32_TMOB_PTR(t32_tmob_rw(ch)[1]), t32_tmob.d03a0, t32_tmob_regw, 0, ch);
}

/* tisp_tmo_tmo_soft_process: hook ids 0 default refresh, 1 stat_limit, 2 / 3 ramY / ramV vi1, 4 / 5 vi0 */
static void t32_tmob_tsp_hook(void *cb, int id, int ch)
{
	u32 *w = t32_tmob_rw(ch);

	(void)cb;
	switch (id) {
	case 0:
		t32_tmob_tmo_default(ch);
		break;
	case 1:
		t32_tmo_stat_limit(T32_TMOB_PTR(w[5]), T32_TMOB_PTR(w[0]), (u8 *)w + 53, t32_tmob_dma1);
		break;
	case 2:
	case 4:
		t32_tmob_ramy(ch);
		break;
	default:
		t32_tmob_ramv(ch);
		break;
	}
}

static void t32_tmob_soft(void *cb, u32 ch)	/* tisp_tmo_tmo_soft_process */
{
	u32 *w = t32_tmob_rw((int)ch);

	(void)cb;
	t32_tmo_tmo_soft_process(&t32_tmob.e3510[ch], T32_TMOB_PTR(w[0]), T32_TMOB_PTR(w[1]), t32_tmob.d03a0,
				 t32_tmob_tsp_hook, 0, (int)ch);
}

static void t32_tmob_fpga(void *cb, int ch)	/* tisp_tmo_adr_fpga */
{
	struct t32_tmo_fpga_env e;
	const u32 *q = t32_tmob_bw(ch);

	(void)cb;
	memset(&e, 0, sizeof(e));
	t32_tmo_fpga_env_tables(&e, t32_tmob.d88d4);
	e.e31b0 = t32_tmob.e31b0;
	e.e3160 = t32_tmob.e3160;
	e.e3518 = &t32_tmob.e3518;
	e.e3500 = t32_tmob.e3500;
	e.exp2 = t32_aem_exp2;
	e.e3244 = &t32_tmob.e3244;
	e.e34e8 = t32_tmob.e34e8;
	e.e3248 = t32_tmob.e3248;
	t32_tmo_adr_fpga(T32_TMOB_PTR(t32_tmob_rw(ch)[0]), T32_TMOB_PTR(q[0]), T32_TMOB_PTR(q[1]), T32_TMOB_PTR(q[2]), &e);
}

/* tisp_tmo_adr_soft_process: 0 adr_stat_calc, 1 adr_ev_func, 2 adr_tgain_func, 3 adr_algorithm, 4 adr_reg_write_every */
static void t32_tmob_asp_hook(void *cb, int id, int ch)
{
	u8 *ctx = T32_TMOB_PTR(t32_tmob_rw(ch)[0]);
	const u32 *q = t32_tmob_bw(ch);
	struct t32_tmo_ramio io;

	(void)cb;
	switch (id) {
	case 0:
		t32_tmo_adr_stat_calc(T32_TMOB_PTR(q[2]), t32_tmob.e3580[ch], t32_tmob.e3570[ch]);
		break;
	case 1:
		t32_tmo_adr_ev_func(ch, ctx, T32_TMOB_PTR(q[1]), t32_tmob.e3530, t32_tmob.e3550, t32_tmob.e3528);
		break;
	case 2:
		t32_tmo_adr_tgain_func(&t32_tmob.tg, (u32)ch);
		break;
	case 3:
		t32_tmo_adr_algorithm(ctx, t32_tmo_tab_c04b8, t32_tmo_tab_d87a0, t32_tmob_fpga, 0, ch);
		break;
	default:
		t32_tmob_io(&io);
		t32_tmo_adr_reg_write_every(T32_TMOB_PTR(q[0]), ctx, ch, &io);
		break;
	}
}

/* tisp_tmo_interrupt_static: 0 tmoadr_get_statistics (arg = the synced half), 1 tmo2fbf_input_prep, 2 tmo2fbf_process, 3 adr_soft_process */
static void t32_tmob_int_hook(void *cb, int id, int ch, u32 arg)
{
	u32 *w = t32_tmob_rw(ch);
	u8 *st = T32_TMOB_PTR(w[1]);

	(void)cb;
	switch (id) {
	case 0:
		t32_tmo_adr_get_statistics(st[385], T32_TMOB_PTR(arg), T32_TMOB_PTR(t32_tmob_bw(ch)[2]), T32_TMOB_PTR(w[4]));
		break;
	case 1:
		t32_tmo_tmo2fbf_input_prep(st, T32_TMOB_PTR(w[0]), T32_TMOB_PTR(w[4]), T32_TMOB_PTR(w[3])[18]);
		break;
	case 2:
		t32_tmo_tmo2fbf_process(st, T32_TMOB_PTR(t32_tmob.e35c0[ch]), t32_tmob_rd, t32_tmob_sysw, 0);
		break;
	default:
		t32_tmo_adr_soft_process(t32_tmob_asp_hook, 0, ch);
		break;
	}
}

/* ---- the registered handlers ---- */

/* interrupt bit 9: tisp_tmo_interrupt_static */
static u32 t32_tmob_irq_tmo(u32 ch)
{
	u8 *b = t32_tmob_rb((int)ch);

	return (u32)t32_tmo_interrupt_static(&t32_tmob.e3516, &t32_tmob.e350c, t32_tmob_rd, t32_tmob_dma_a,
					     ((const u32 *)(uintptr_t)t32_tmob.e35d0[ch])[1], T32_TMOB_PTR(((u32 *)b)[1]),
					     (u16 *)(b + 44), (u16 *)(b + 46), (u16 *)(b + 50), &t32_tmob.e3504,
					     t32_tmob_int_hook, 0, (int)ch);
}

/* interrupt bit 31: tisp_tmo_fbf_interrupt_calc */
static u32 t32_tmob_irq_fbf(u32 ch)
{
	return (u32)t32_tmo_fbf_interrupt_calc(T32_TMOB_PTR(t32_tmob_rw((int)ch)[1]), t32_tmob_rb((int)t32_tmob.e3504),
					       &t32_tmob.d8b34, t32_tmob_soft, 0, t32_tmob.e3504);
}

/* ---- tisp_tmo_init ---- */

struct t32_tmob_init_arg {
	const u32 *info;	/* the init argument: word 0 width, word 1 height, byte 36 WDR */
	int ch;
};

/* tisp_tmo_init's callees (ids of t32_tmo_init) */
static void t32_tmob_init_hook(void *cb, int id, int ch)
{
	const struct t32_tmob_init_arg *a = cb;
	struct t32_tmo_rec r;
	u32 *w = id == 0 || id >= 14 ? 0 : t32_tmob_rw(ch);
	u8 *st = w ? T32_TMOB_PTR(w[1]) : 0, *ctx = w ? T32_TMOB_PTR(w[0]) : 0;

	switch (id) {
	case 0:
		t32_tmo_malloc_cfg(&t32_tmob.e35d0[ch], ch, t32_tmob_vm, t32_tmob_km, t32_tmob_regw, t32_tmob.d03a0, 0);
		break;
	case 1:		/* size_update (ch, info): +0 = info word 1, +2 = word 0 */
		t32_tmo_size_update((struct t32_tmo_ctx *)ctx, (struct t32_tmo_stat *)st, (struct t32_tmo_geom *)T32_TMOB_PTR(w[3]),
				    a->info[1], a->info[0]);
		break;
	case 2:
		t32_tmo_fbf_malloc_cfg(&t32_tmob.e35c0[ch], (const u32 *)(uintptr_t)t32_tmob.e35d0[ch], w[5], t32_tmob_vm, 0);
		break;
	case 3:
		t32_tmo_dark_ctr_interp(ctx, (struct t32_tmo_dark_out *)(st + 238), w[6], w[7], w[8]);
		break;
	case 4:
		t32_tmo_detail_ev_interp(ctx, st, w[6], w[7], w[8]);
		break;
	case 5:
		t32_tmo_satcurve_ev_interp(ctx, st, w[6], w[7], w[8]);
		break;
	case 6:
		t32_tmob_tmoadr_default(ch);
		break;
	case 7:
		t32_lcac_no_refresh(&t32_tmob_lcac_ops, (u32)ch, ctx);
		break;
	case 8:
		t32_tmob_tmo_default(ch);
		break;
	case 9:
		t32_tmob_rec(ch, &r);
		t32_tmo_modechange_stat_refresh(&r);
		break;
	case 10:
	case 12:
		t32_tmob_ramy(ch);
		break;
	case 11:
	case 13:
		t32_tmob_ramv(ch);
		break;
	case 14:	/* adr_gamma_refresh (0): channel 0's context, dc274[0] + 32 */
		t32_tmo_adr_gamma_refresh(T32_TMOB_PTR(t32_tmob_rw(0)[0]), t32_tmob.gamma[0]);
		break;
	case 15:
		t32_tmob.ops->irq_set((u32)ch, T32_TMOB_IRQ_TMO, t32_tmob_irq_tmo);
		break;
	default:
		t32_tmob.ops->irq_set((u32)ch, T32_TMOB_IRQ_FBF, t32_tmob_irq_fbf);
		break;
	}
}

/*
 * tisp_tmo_init (ch, info) on the context ctx (the tuning block's TMO section, edited in place) with the
 * gamma payload gamma (258 bytes read by adr_gamma_refresh).  d03a0 = the register mode.
 */
static inline void t32_tmob_init(int ch, const u32 *info, u8 *ctx, const u8 *gamma)
{
	struct t32_tmob_init_arg a = { info, ch };
	struct t32_tmo_init_env e;

	t32_tmob.gamma[ch] = gamma;
	e.vmalloc = t32_tmob_vm;
	e.kmalloc = t32_tmob_km;
	e.hook = t32_tmob_init_hook;
	e.regw = t32_tmob_regw;
	e.cb = &a;
	e.e35e0 = &t32_tmob.e35e0[ch];
	e.e35b0 = &t32_tmob.e35b0[ch];
	e.ctx = T32_TMOB_ADDR(ctx);
	e.e3580 = &t32_tmob.e3580[ch];
	e.e3570 = &t32_tmob.e3570[ch];
	e.e3518 = &t32_tmob.e3518;
	e.d88d6 = (const u8 *)t32_tmob.d88d4 + 2;	/* d88d6: the y filter positions 1 .. 14 as they are now */
	e.param = info;
	e.d03a0 = t32_tmob.d03a0;
	t32_tmo_init(&e, ch);
}

/* ---- per frame from the AE ---- */

/* tisp_tmo_tgain_update (ch, gain): a2 / a3 = the low / high word of the 64 bit total gain */
static inline void t32_tmob_tgain_update(int ch, u32 a2, u32 a3)
{
	t32_tmo_tgain_update(t32_tmob_rw(ch) + 9, (u16 *)&t32_tmob.e3520[ch], &t32_tmob.tg.b[ch], &t32_tmob.tg.a[ch], a2, a3);
}

static void t32_tmob_evu_adr(void *cb, int ch) { (void)cb; t32_tmob_tmoadr_default(ch); }
static void t32_tmob_evu_tmo(void *cb, int ch) { (void)cb; t32_tmob_tmo_default(ch); }

/* tisp_tmo_ev_update (ch, ev, arg4): a2 / a3 = the 64 bit ev point, arg4 = the ev word of mode 1 */
static inline void t32_tmob_ev_update(int ch, u32 a2, u32 a3, u32 arg4)
{
	struct t32_tmo_rec r;
	u32 *w = t32_tmob_rw(ch);

	t32_tmob_rec(ch, &r);
	t32_tmo_ev_update(&r, a2, a3, arg4, &t32_tmob.e3528[ch], (u32 *)&t32_tmob.e3530[ch], t32_tmob_evu_adr,
			  t32_tmob_evu_tmo, 0, ch);
	w[6] = r.ev0;
	w[7] = r.ev1;
	w[8] = r.ev1m;
}

/* ---- tisp_tmo_deinit ---- */

static void t32_tmob_vf(void *cb, u32 a) { (void)cb; t32_tmob.ops->vfree(T32_TMOB_PTR(a)); }
static void t32_tmob_kf(void *cb, u32 a) { (void)cb; t32_tmob.ops->kfree(T32_TMOB_PTR(a)); }

static inline void t32_tmob_deinit(int ch)
{
	t32_tmo_deinit(&t32_tmob.e35d0[ch], &t32_tmob.e35c0[ch], &t32_tmob.e35e0[ch], &t32_tmob.e35b0[ch], t32_tmob_vf,
		       t32_tmob_kf, 0);
}

#endif /* TX_ISP_T32_TMOBLK_H */
