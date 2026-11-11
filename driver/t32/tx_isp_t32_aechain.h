/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 per-frame AE chain: the vendor flow of one AE frame put together from
 * the ported functions, so that the driver can run it (openimp #234).
 *
 *   AE interrupt (system irq 4)  tisp_ae_interrupt_static    t32_ae_interrupt_static
 *     mode e1510 == 1:           tisp_ae_long_ev_alloc       t32_ae_long_ev_alloc
 *                                  tisp_ae_get_bv            t32_ae_get_bv
 *                                  long_set_sensor_integration_time (dd2d4 +168)
 *                                  tisp_set_ae_long_ag       t32_set_ae_ag (dd2d4 +124 / +184)
 *                                  events 6, 4 (total gain), 5
 *                                tisp_ae_get_statistics      t32_ae_get_statistics
 *                                event 1
 *   event 1                      tisp_ae_calc_process        t32_ae_calc_process
 *                                  tisp_ae_par_update_trig   t32_aec_par_update_trig
 *                                    tisp_ae_sensor_par_ctrls_update, tisp_ae_par_calc,
 *                                    tisp_ae_clac_deflicker_cfg (tx_isp_t32_aeinit.h),
 *                                    tisp_set_ae_sensor_fps (t32_set_ae_sensor_fps, dd2d4 +212)
 *                                  tisp_ae_algorithm         t32_aec_algorithm (below)
 *                                    tisp_ae_weight_mean     t32_ae_weight_mean
 *                                      tisp_ae_fliker_detect t32_aef_fliker_detect
 *                                        tisp_ae_sort_H2L    t32_aec_sort_h2l (below)
 *                                    tisp_ae_tune            t32_ae_tune (sets e1510 = 1)
 *                                  api_ae_long_process       t32_aea_long_process
 *
 * The fixed point maths are the real functions (tx_isp_t32_aemath.h,
 * tx_isp_t32_log2.h, tx_isp_t32_fixp.h; fix_point_div_64 is the flow of
 * t32_awb_fixdiv64).  Hooks (struct t32_aec_ops): ISP register reads
 * (tisp_reg_read / system_reg_read), the sensor entries of the channel's
 * dd2d4 block (sensor_call: +168 set integration time with a 3 word info
 * block, word 2 = channel; +124 alloc analog gain, the sensor's register
 * value comes back as u16 at info +4; +184 set analog gain; +212 set fps),
 * the event push (tisp_event_push, 8 words, type in word 2), the printk of
 * the AE init maths and the AE completion.  Register writes are collected in
 * w[] (direct addresses, vendor t32_data_d03a0 == 0).
 *
 * Three pieces are composed here instead of using the existing port:
 *  - tisp_ae_algorithm: the vendor hands the same stack buffer (sp + 32) to
 *    tisp_ae_weight_mean (6th argument, the weighted sums) and to
 *    tisp_ae_tune (4th argument); t32_ae_algorithm (tx_isp_t32_aetrig.h)
 *    hook shape has no 6th argument and gives tune a zero buffer.
 *    t32_aec_algorithm follows the vendor (proven by t32_aechain_oracle,
 *    the zeroed buffer is one of its detected mutations).
 *  - tisp_ae_par_update_trig: t32_aec_par_update_trig, the flow of
 *    t32_ae_par_update_trig (tx_isp_t32_blkrest.h names a second
 *    struct t32_aeu_ops, so the two headers cannot share a unit).
 *  - tisp_ae_long_set_sensor_integration_time: the one-line vendor call of
 *    dd2d4 +168 with R +576 & 0xffff (t32_ae_long_set_sensor_integration_time
 *    calls through a pointer table; here it goes through sensor_call).
 *
 * Not here (vendor paths the driver does not take): WDR (context word 2 = 1:
 * short frame allocation and tisp_ae_short_sensor_par_ctrls_update; the
 * T32 front end has no WDR), the debug printk block of calc_process
 * (e1500 / e14f0 = 0) and the AE algorithm library mode (e15b8 = 0).  The
 * word dc284 +900 (the pipeline count tisp_ae_init gets, 1 for one sensor)
 * is the dcw field: with T +1740 == 1 it selects tisp_ae_ev_list_alloc_calc.
 *
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (t32_aechain_oracle: N frames of synthetic statistics
 * through the whole vendor chain and ours, sensor calls, events, register
 * writes and every byte of the AE blocks compared per frame).
 */
#ifndef TX_ISP_T32_AECHAIN_H
#define TX_ISP_T32_AECHAIN_H

#include "tx_isp_t32_stats.h"
#include "tx_isp_t32_aeinit.h"
#include "tx_isp_t32_aemath.h"
#include "tx_isp_t32_aefu.h"
#include "tx_isp_t32_aeinfo.h"
#include "tx_isp_t32_api_ag.h"
#include "tx_isp_t32_awblong.h"
#include "tx_isp_t32_fixp.h"
#include "tx_isp_t32_log2.h"
#ifdef __KERNEL__
#include <linux/math64.h>
#endif

#define T32_AEC_SNS_SET_IT	168U
#define T32_AEC_ZONE_BYTES	4096U	/* one of the four zone buffers */
#define T32_AEC_HIST_BYTES	1052U	/* d0950 / d19c0 blocks */
#define T32_AEC_COMP_BYTES	1028U	/* d2a30 block */
#define T32_AEC_D_FLICKER	10856U	/* weight_mean's flicker sample (D + 10856) */
#define T32_AEC_D_VAL		10852U
#define T32_AEC_MAX_WRITES	32U
#define T32_AEC_R_IT		576U	/* R: long integration time */
#define T32_AEC_T_WMODE		1684U	/* T: zone window mode word */
#define T32_AEC_T_WIN_W		2940U
#define T32_AEC_T_WIN_H		2942U
#define T32_AEC_B_VAL		8568U	/* B: u16 copied to D + 10852 */
#define T32_AEC_B_TRIG		12833U	/* B: 9 bytes -> R + 1276 (tisp_ae_par_update_trig) */
#define T32_AEC_R_TRIG		1276U
#define T32_AEC_TRIG_BYTES	9U
#define T32_AEC_CTX_DONE	2008U	/* the AE completion in the context */

struct t32_aec_ops {
	u32 (*reg_read)(u32 ch, u32 reg);			/* tisp_reg_read */
	u32 (*sys_read)(u32 reg);				/* system_reg_read */
	u32 (*sensor_call)(u32 off, u32 arg, u8 *info);	/* dd2d4 + off */
	void (*event)(u32 ch, const u32 *ev);			/* tisp_event_push */
	void (*warn)(u32 line);					/* printk of the AE init maths */
	void (*complete)(void *done);				/* private_complete */
};

struct t32_aec {
	struct t32_ae_blk b;		/* ctx (e15a8), T, B, R (e1588), D (e15d8), C (e15c8) */
	u8 *S;				/* the channel's dd2d4 block (232 B) */
	const u8 *zones;		/* the four zone buffers (vendor e1598[ch] + 4) */
	u8 *hist, *comp, *lum;		/* d0950 / d2a30 / d19c0 blocks of api_ae_long_process */
	u8 mode;			/* e1510[ch]: 1 once tisp_ae_tune ran */
	u32 stop;			/* e15b8[ch] */
	u32 frame_cnt;			/* e1578[ch] */
	u32 trace;			/* d9008 */
	u32 dcw;			/* dc284 + 900: the pipeline (sensor) count, 1 here as at tisp_ae_init */
	u32 running_mode;		/* lib_tisp_api_g_runningMode */
	const struct t32_aec_ops *ops;
	struct t32_stats_write w[T32_AEC_MAX_WRITES];
	unsigned int n;			/* register writes of the last call */
	u32 tab_ctx[4], tab_par[4];	/* 32 bit pointer tables of the table-shaped ports */
};

/* the hooks of the ported functions have no context argument: one chain runs at a time */
static struct t32_aec *t32_aec_cur;

/* ---- maths ---- */
static u32 t32_aec_div32(u32 sh, u32 a, u32 b) { return t32_aem_div32(sh, a, b); }
static u32 t32_aec_mult2_32(u32 sh, u32 a, u32 b) { return t32_aem_mult2_32(sh, a, b); }
static u32 t32_aec_mult3_32(u32 sh, u32 a, u32 b, u32 c) { return t32_aem_mult3_32(sh, a, b, c); }
static t32_u64 t32_aec_mult2(u32 sh, t32_u64 a, t32_u64 b) { return t32_fixp_mult2(sh, a, b); }
static t32_u64 t32_aec_mult3(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c) { return t32_fixp_mult3(sh, a, b, c); }
static t32_u64 t32_aec_mult2_64(u32 sh, t32_u64 a, t32_u64 b) { return t32_aem_mult2_64(sh, a, b); }
static t32_u64 t32_aec_mult3_64(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c) { return t32_aem_mult3_64(sh, a, b, c); }
static t32_u64 t32_aec_div64(u32 sh, t32_u64 a, t32_u64 b) { return t32_awb_fixdiv64(sh, a, b); }
static u32 t32_aec_exp2(u32 v, u32 iq, u32 oq) { return t32_aem_exp2(v, iq, oq); }
static u32 t32_aec_log2(u32 v, u32 iq, u32 oq) { return t32_log2_fixed_to_fixed(v, iq, oq); }
static u32 t32_aec_log2_64(t32_u64 v, u32 iq, u32 oq) { return t32_log2_fixed_to_fixed_64(v, iq, oq); }

/*
 * tisp_ae_lib_bilinear_intp: the flow of t32_ae_lib_bilinear_intp
 * (tx_isp_t32_fixp.h), with the 64 / 32 bit division of tisp_ae_lib_div_64
 * done by div_u64 (no libgcc __udivdi3 in the module).
 */
static t32_u64 t32_aec_bil(t32_u64 x, t32_u64 e0, t32_u64 e1, u32 y0, u32 y1)
{
	u32 dy = y1 < y0 ? y0 - y1 : y1 - y0, d = (u32)t32_fixp_absdiff(e1, e0), q;

	q = d ? (u32)div_u64(dy * t32_fixp_absdiff(x, e0), d) : 0;
	return y1 < y0 ? y0 - q : y0 + q;
}
static void t32_aec_warn(u32 line) { t32_aec_cur->ops->warn(line); }

static const struct t32_ae_init_ops t32_aec_iops = {
	t32_aec_div32, t32_aec_mult2_32, t32_aec_mult3_32, t32_aec_mult2, t32_aec_mult3_64, t32_aec_exp2,
	t32_aec_warn,
};

/* ---- sensor entries ---- */
static u32 t32_aec_sns_call(const u8 *sblk, u32 off, u32 arg, u8 *info)
{
	(void)sblk;
	return t32_aec_cur->ops->sensor_call(off, arg, info);
}

static const struct t32_ae_ag_ops t32_aec_agops = {
	t32_aec_log2, t32_aec_exp2, t32_aec_mult2_32, t32_aec_div32, t32_aec_sns_call,
};

static void t32_aec_ag_ctx(struct t32_ae_ag_ctx *c)
{
	c->tune = t32_aec_cur->b.T;
	c->buf = t32_aec_cur->b.B;
	c->st = t32_aec_cur->b.R;
	c->sns = t32_aec_cur->S;
}

/* tisp_set_ae_sensor_fps */
static void t32_aec_set_fps(u32 ch)
{
	struct t32_ae_ag_ctx c;

	t32_aec_ag_ctx(&c);
	t32_set_ae_sensor_fps(&t32_aec_agops, &c, ch);
}

/* tisp_ae_long_set_sensor_integration_time / tisp_set_ae_long_ag (long_exp == 1 only: no WDR) */
static void t32_aec_set_it(u32 ch, int long_exp)
{
	u8 info[12] = { 0 };

	(void)long_exp;
	t32_wr32(info, 8, ch & 0xff);
	t32_aec_cur->ops->sensor_call(T32_AEC_SNS_SET_IT, t32_ld32(t32_aec_cur->b.R, T32_AEC_R_IT) & 0xffff,
				      info);
}

static void t32_aec_set_ag(u32 ch, int long_exp)
{
	struct t32_ae_ag_ctx c;

	t32_aec_ag_ctx(&c);
	t32_set_ae_ag(&t32_aec_agops, &c, ch, !long_exp);
}

/* ---- tisp_ae_max_exp_calc_fps (exposure list mode only) ---- */
static u32 t32_aec_lock(void) { return 0; }
static void t32_aec_unlock(u32 f) { (void)f; }
static const struct t32_aeinfo_ops t32_aec_infops = { t32_aec_mult2_32, t32_aec_div32, t32_aec_lock, t32_aec_unlock, 0 };

static u32 t32_aec_max_exp_fps(u32 ch, u32 ev)
{
	struct t32_aeinfo_dev d = { &t32_aec_infops, t32_aec_cur->tab_ctx, t32_aec_cur->tab_par, 0 };

	return t32_ae_max_exp_calc_fps(&d, ch, ev);
}

/* ---- tisp_ae_sort_H2L: |v| (two's complement negate), then a bubble sort high to low, idx follows ---- */
static inline void t32_aec_sort_h2l(u32 *v, u32 *idx)
{
	int n, j;
	u32 t;

	for (j = 0; j < 15; j++)
		if ((int)v[j] < 0)
			v[j] = 0U - v[j];
	for (n = 14; n != -1; n--)
		for (j = 0; j < n; j++)
			if ((int)v[j] < (int)v[j + 1]) {
				t = v[j]; v[j] = v[j + 1]; v[j + 1] = t;
				t = idx[j]; idx[j] = idx[j + 1]; idx[j + 1] = t;
			}
}

static void t32_aec_sort(u32 *v, u32 *idx) { t32_aec_sort_h2l(v, idx); }
static const struct t32_aef_ops t32_aec_aefops = { t32_aec_sort };

/* tisp_ae_fliker_detect(ch, smp) */
static void t32_aec_flicker(u32 ch, u8 *smp)
{
	(void)ch;
	t32_aef_fliker_detect(t32_aec_cur->b.D, t32_aec_cur->b.C, t32_aec_cur->b.R, smp, &t32_aec_aefops);
}

static const struct t32_ae_ops t32_aec_aeops = {
	t32_aec_div32, t32_aec_mult2_32, t32_aec_mult3_32, t32_aec_flicker, t32_aec_bil,
};
static const struct t32_aev_ops t32_aec_evops = {
	t32_aec_div32, t32_aec_mult2_32, t32_aec_div64, t32_aec_mult2_64, t32_aec_mult3_64,
	t32_aec_set_fps, t32_aec_max_exp_fps,
};
static const struct t32_aeu_ops t32_aec_uops = { &t32_aec_aeops, &t32_aec_evops };

/* ---- tisp_ae_algorithm ---- */
static inline u32 t32_aec_algorithm(struct t32_aec *a, u32 ch)
{
	struct t32_ae_ctx c = { a->b.T, a->b.B, 0, 0, 0, 0 };	/* ROI bytes e151c.. are .bss (0) */
	u32 mean = 0, ratio[2] = { 0, 0 }, wsum[2] = { 0, 0 }, ww = 1, wh = 1;
	u8 rp[8], wp[8];

	ch &= 0xff;
	t32_wr32(a->b.D, T32_AEC_D_VAL, t32_st16(a->b.B, T32_AEC_B_VAL));
	if (t32_ld32(a->b.T, T32_AEC_T_WMODE) == 1) {
		ww = t32_st16(a->b.T, T32_AEC_T_WIN_W);
		wh = t32_st16(a->b.T, T32_AEC_T_WIN_H);
	}
	t32_ae_weight_mean(&c, a->b.D, a->b.R, ch, &mean, ratio, ww, wh, wsum, &t32_aec_aeops);
	t32_wr32(rp, 0, ratio[0]); t32_wr32(rp, 4, ratio[1]);
	t32_wr32(wp, 0, wsum[0]); t32_wr32(wp, 4, wsum[1]);
	return t32_ae_tune(a->b.T, a->b.B, a->b.R, &a->mode, a->dcw, ch, mean, rp, wp, &t32_aec_uops);
}

/*
 * ---- tisp_ae_par_update_trig (0x30a8c) ----
 * The flow of t32_ae_par_update_trig (tx_isp_t32_blkrest.h, oracle
 * t32_aeupd_oracle; that header cannot share a unit with aefu.h: both name a
 * struct t32_aeu_ops): 9 bytes B +12833 -> R +1276, the sensor limits, the
 * WDR copy (not taken: context word 2 = 0), par_calc, the deflicker table,
 * then the sensor fps (direct register mode, t32_data_d03a0 = 0).
 */
static inline void t32_aec_par_update_trig(struct t32_aec *a, u32 ch)
{
	memcpy(a->b.R + T32_AEC_R_TRIG, a->b.B + T32_AEC_B_TRIG, T32_AEC_TRIG_BYTES);
	t32_ae_sensor_par_ctrls_update(&a->b, a->S, &t32_aec_iops);
	t32_ae_par_calc(&a->b, &t32_aec_iops);
	t32_ae_clac_deflicker_cfg(&a->b, &t32_aec_iops);
	t32_aec_set_fps(ch & 0xff);
}

static void t32_aec_cp_trig(u32 ch) { t32_aec_par_update_trig(t32_aec_cur, ch); }

static void t32_aec_cp_alg(u32 ch) { t32_aec_algorithm(t32_aec_cur, ch); }

static void t32_aec_cp_long(u32 ch)
{
	struct t32_aec *a = t32_aec_cur;
	struct t32_aea_ctx c = { a->b.T, a->b.B, a->b.R, a->S, a->hist, 0, 0, a->comp, a->lum };

	(void)ch;
	t32_aea_long_process(&c);
}

static u32 t32_aec_cp_rd(u32 ch, u32 reg) { return t32_aec_cur->ops->reg_read(ch, reg); }
static void t32_aec_cp_done(void *done) { t32_aec_cur->ops->complete(done); }

static const struct t32_ae_cp_ops t32_aec_cpops = {
	t32_aec_cp_rd, t32_aec_cp_alg, t32_aec_cp_trig, t32_aec_cp_long, t32_aec_cp_done,
};

/* ---- tisp_ae_long_ev_alloc ---- */
static u32 t32_aec_mode(const u8 *st0) { (void)st0; return t32_aec_cur->running_mode; }

static const struct t32_ae_bv_ops t32_aec_bvops = { t32_aec_div32, t32_aec_log2, t32_aec_log2_64, t32_aec_mult3 };

static u32 t32_aec_bv(u32 ch)
{
	(void)ch;
	return t32_ae_get_bv(t32_aec_cur->b.T, t32_aec_cur->b.B, t32_aec_cur->b.R, &t32_aec_bvops);
}

static void t32_aec_event(u32 ch, const u32 *ev) { t32_aec_cur->ops->event(ch, ev); }
static void t32_aec_no_short(u32 ch, t32_u64 v, u32 *a, u32 *b, u32 *c) { (void)ch; (void)v; (void)a; (void)b; (void)c; }

static const struct t32_ae_ev_ops t32_aec_lops = {
	t32_aec_mult3_32, t32_aec_mult2_32, t32_aec_mult3_64, t32_aec_div64, t32_aec_bil, t32_aec_log2,
	t32_aec_cp_rd, t32_aec_mode, t32_aec_bv, t32_aec_no_short, t32_aec_set_it, t32_aec_set_ag, t32_aec_event,
};

static void t32_aec_long_alloc(u32 ch)
{
	struct t32_aec *a = t32_aec_cur;
	unsigned int k, n;
	struct t32_stats_write w[16];

	n = t32_ae_long_ev_alloc(ch, a->b.T, a->b.B, a->b.R, a->b.R, a->S, t32_ld32(a->b.ctx, 8), &t32_aec_lops, w);
	for (k = 0; k < n && a->n < T32_AEC_MAX_WRITES; k++)
		a->w[a->n++] = w[k];
}
static void t32_aec_short_alloc(u32 ch) { (void)ch; }	/* WDR only */
static u32 t32_aec_sysrd(u32 reg) { return t32_aec_cur->ops->sys_read(reg); }
static void t32_aec_sync(const u8 *p, u32 len) { (void)p; (void)len; }	/* the caller maps the zones uncached */

static const struct t32_ae_irq_ops t32_aec_irqops = {
	t32_aec_long_alloc, t32_aec_short_alloc, t32_aec_sysrd, t32_aec_sync, t32_aec_event,
};

static inline void t32_aec_bind(struct t32_aec *a)
{
	a->tab_ctx[0] = (u32)(uintptr_t)a->b.ctx;
	a->tab_par[0] = (u32)(uintptr_t)a->b.R;
	t32_aec_cur = a;
}

/* the AE interrupt (tisp_ae_interrupt_static, channel 0); writes in a->w[0 .. a->n) */
static inline u32 t32_aec_irq(struct t32_aec *a)
{
	struct t32_ae_ctx c = { a->b.T, a->b.B, 0, 0, 0, 0 };

	t32_aec_bind(a);
	a->n = 0;
	return t32_ae_interrupt_static(0, &c, (const u32 *)(a->b.ctx + 8), a->stop, a->mode, a->zones,
				       &t32_aec_irqops);
}

/* event 1 (tisp_ae_calc_process, channel 0); writes in a->w[0 .. a->n) */
static inline unsigned int t32_aec_calc(struct t32_aec *a)
{
	struct t32_ae_ctx c = { a->b.T, a->b.B, 0, 0, 0, 0 };
	struct t32_ae_cp_glob g = { &a->stop, &a->frame_cnt, 0, 0, a->trace };

	t32_aec_bind(a);
	a->n = t32_ae_calc_process(0, &c, a->b.R, t32_ld32(a->b.ctx, 8), &g, a->b.ctx + T32_AEC_CTX_DONE,
				   &t32_aec_cpops, a->w);
	return a->n;
}

#endif /* TX_ISP_T32_AECHAIN_H */
