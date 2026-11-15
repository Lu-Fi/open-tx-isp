// SPDX-License-Identifier: GPL-2.0
/*
 * T33 (PRJ008) per-frame AE driver glue: tisp_ae_init's state part at
 * stream-on (the T32 t32_ae_ctx_init, then the T33 start parameters and
 * zone sizes), and per AE interrupt the T33 flow of ../t32/tx_isp_t33_aealg.h:
 *   system irq 5 (status bit 5): the zone buffer index (0x1708c bits 4..5);
 *   system irq 4 (status bit 4): the histogram index (bits 0..1), then in a
 *     work tisp_ae_long_ev_alloc (sensor integration time / again, DG into
 *     module list 5) and tisp_normal_ae_calc_process (event 1).
 * The algorithm is the T32 port (tx_isp_t32_aechain.h and the headers it
 * includes, from claude/t32-ds-batch; Matt Davis' recovered C, PR #15).
 * The sensor gets the integration time and again packed (it | again << 16,
 * TX_ISP_EVENT_SENSOR_EXPO) once per change after the frame, as the vendor
 * sensor_set_integration_time / sensor_set_analog_gain mark them for its
 * frame-done sensor update.  Events 4 (long total gain) and 6 (long EV)
 * go to the AWB (tisp_awb_tgain_update / tisp_awb_ev_update); the AE and
 * AWB works share one ordered queue like the vendor's event thread.  The
 * other gain / EV follow-ups (DPC, CCM, BCSH, ...) are not wired yet.
 * T33 only: the T32 build does not compile this file.
 *
 * Vendor-layout AE state (openimp #99 wiring group 10): the AE runs the
 * oracle-proven vendor entry points tisp_normal_ae_init /
 * tisp_normal_ae_interrupt_hist / tisp_normal_ae_interrupt_static /
 * tisp_normal_ae_calc_process (tx_isp_t33_aeint.h), tisp_ae_malloc_info /
 * tisp_ae_set_start_param (tx_isp_t33_aemisc.h), tisp_normal_ae_par_sensor_trig
 * (tx_isp_t33_getB.h) and normal_ae_dn_par_refresh (tx_isp_t33_misc1.h) on
 * the vendor's module statics: ae_contexts[0] = the 19968 B AE block of
 * tisp_ae_init, stAeInfo[0] = ae_cache[0] = block + 8 (the AE state words
 * 0..1743 from ae_cache_default, the tuning section pointer at +1744, the
 * zone / start words of the B part), the parameter / start block / buffer
 * words at block + 19952 .. 19964, fliker_info / fliker_para (block + 8160 /
 * 19928: the T32 chain's D / C blocks), ae_init_flag, stat_num, aeFrameCount,
 * ta_custom_en, and the AE hardware parameter block (g_ae_contexts[0]: zone
 * start / count, histogram threshold, buffer counts) that tisp_ae_switch_para
 * rebuilds list 30 from.  The T32 algorithm port runs on that ae_cache as its
 * state block R (same layout up to +1699; its own words from +1700 are parked
 * after the vendor block, T32_AER_HI).  Still the T32 layout: the B block
 * (sensor limits, statistics, targets; the vendor keeps it in stAeInfo from
 * +1752 at other offsets) and the tuning copy T (the vendor points into
 * tparamsP; the bin switch copies the new section into T, as the vendor's
 * in-place tparamsP update).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include "tx_isp_t32.h"
#include "tx_isp_t33_aeint.h"
#include "tx_isp_t33_aemisc.h"
#include "tx_isp_t33_getB.h"
#include "tx_isp_t32_aesns.h"
#include "tx_isp_t32_libwrap.h"

int isp_printf(unsigned int level, const char *fmt, ...);
u32 t33_hw_rd(u32 reg);
void t33_dg_list_set(u32 v);
void t33_tunblk_hist_bins(u32 ch, u8 *bins);
int tisp_rcfg_module_cfg_trig(u32 ch, u32 mod);		/* tx_isp_t33_bst.h */
extern u32 g_ae_contexts[4];				/* tx_isp_t32_hw.c (aeldc) */		/* ../t32/tx_isp_t32_tunblk.c: statistics config bounds */				/* tx_isp_t32_hw.c: list 5 slots 3 / 4, run */

static unsigned int aec_trace;
module_param(aec_trace, uint, 0644);
MODULE_PARM_DESC(aec_trace, "T33 AE: log the first N AE frames (debug), default 0");

static struct t32_aec aec;
static struct t33_ae_front front;
static u8 sblk[T32_SENSOR_ATTR_COPY];
static const u8 *zbufs, *hbufs;
static DEFINE_MUTEX(aec_lock);
static bool ready;
static u8 aec_wsel[2];			/* vendor e1570[ch]: the two AE weight selectors */
static unsigned int aec_api_busy;	/* tuning API calls on the blocks (t32_aec_api_get .. _put) */
static DECLARE_WAIT_QUEUE_HEAD(aec_api_wq);
static u32 aec_it, aec_ag, aec_sent;
static u32 tgain_old;	/* tisp_long_tgain_update's last gain (vendor .bss, not reset per stream) */
static u32 ev_last[2];	/* tisp_long_ev_update's last EV */

/* ---- the vendor module statics of the AE (channel 0 in use) ---- */
#define T33_AEW_CTX		19968U	/* ae_contexts[ch]: the AE block of tisp_ae_init */
#define T33_AEW_INFO		8U	/* stAeInfo = ae_cache = block + 8 */
#define T33_AEW_FLKI		8160U	/* fliker_info = block + 8160: the T32 D block (11768 B) */
#define T33_AEW_FLKP		19928U	/* fliker_para = block + 19928: the T32 C block (16 B) */
#define T33_AEW_TUNE		1744U	/* stAeInfo + 1744: the AE tuning section */
#define T33_AEW_PAR		19952U	/* block + 19952: the parameter pointer of tisp_ae_par_update_trig */
#define T33_AEW_CFG		19956U	/* block + 19956: the start block (tisp_ae_set_start_param) */
#define T33_AEW_STAT		19960U	/* block + 19960 / 19964: statistics / histogram buffers */
#define T33_AEW_HIST		19964U
#define T33_AEW_READY		1732U	/* ae_cache + 0x6c4: the tune ran, tisp_ae_long_ev_alloc at the next irq 4 */
#define T33_AEW_DEF_BYTES	1744U	/* ae_cache_default */
#define T33_AEW_START_BYTES	960U
#define T33_AEW_BYTES		(T33_AEW_INFO + T32_AEI_R_BYTES)	/* the vendor block + the parked T32 words */

u8 ae_init_flag[4];		/* tx_isp_t33_getB.h */
u32 *ae_cache[4];
u32 *lsc_para[4];		/* tx_isp_t33_getB.h declares it; the LSC info is not wired (always NULL) */
u32 t33_ae_hw_par[16];		/* g_ae_contexts[0]: the 64 B AE hardware parameter block */
static u32 fliker_info[4], fliker_para[4], stAeStatBuff[4], stAeHistBuff[4], stAeInfo[4], ae_contexts[4];
static u32 aeFrameCount[4], stat_num[8];
static u8 ae_algo_comp[48];
static u8 ae_cache_default[T33_AEW_DEF_BYTES];
static u8 *aew_blk, *aew_start;
static u32 aew_reg4, aew_reg5;	/* the 0x1708c word read at system irq 4 / 5 */
static bool aew_in_init;	/* tisp_normal_ae_init's tisp_ae_par_update_trig = the T32 init chain */
static int aew_init_rc;
static u8 aew_sinfo[64];	/* t32_ae_ctx_init's sensor info: +0 width, +4 height, +12 fps */

static struct {
	u32 frames, writes, dg_sets, expo_sets, expo_fail, fps_sets, events, irq5, dn_refresh, syncs;
	u32 tgain, ev_lo, ev_hi, bv, dg, mean, target;
} st;

/* the T32 AE register reads on the T33: DG words of list 5 (0x4060 / 0x4064) */
static u32 drv_rd(u32 ch, u32 reg)
{
	(void)ch;
	if (reg == T32_AE_REG_A)
		return t33_hw_rd(0x4060);
	if (reg == T32_AE_REG_C)
		return t33_hw_rd(0x4064);
	return 0;
}
static u32 drv_sys(u32 reg) { return t33_hw_rd(reg); }

static u32 drv_sensor(u32 off, u32 arg, u8 *info)
{
	u32 reg, r;

	switch (off) {
	case T32_SNS_ALLOC_AGAIN:
		r = t32_core_sensor_alloc_again(arg, &reg);
		t32_wr16(info, 4, (u16)reg);
		return r;
	case T32_SNS_SET_AGAIN:
		aec_ag = arg & 0xffff;
		return 0;
	case T32_AEC_SNS_SET_IT:
		aec_it = arg & 0xffff;
		return 0;
	case T32_SNS_SET_FPS:
		st.fps_sets++;
		return (u32)t32_core_sensor_set_fps(arg);
	}
	return 0;
}

static void drv_event(u32 ch, const u32 *ev)
{
	(void)ch;
	st.events++;
	if (ev[2] == 4) {
		st.tgain = ev[4];
		if (ev[4] != tgain_old || ev[5]) {
			t33_tgain_refresh(ev[4]);
			t33_awbc_ae_event(4, ev[4], ev[5]);
			tgain_old = ev[4];
		}
	}
	if (ev[2] == 6) {
		st.ev_lo = ev[4];
		st.ev_hi = ev[5];
		st.bv = ev[6];
		if (ev[4] != ev_last[0] || ev[5] != ev_last[1]) {
			t33_awbc_ae_event(6, ev[4], ev[5]);
			t33_ev_refresh(ev[4], ev[5], ev[6], ev[7]);
			ev_last[0] = ev[4];
			ev_last[1] = ev[5];
		}
	}
}

static void drv_warn(u32 line)
{
	isp_printf(2, "tx-isp-t33: AE sensor ratio word without integer part, vendor default (line %u)\n", line);
}

static void drv_done(void *d) { (void)d; }

static const struct t32_aec_ops drv_ops = { drv_rd, drv_sys, drv_sensor, drv_event, drv_warn, drv_done };

/* init maths hooks of t32_ae_ctx_init */
static u32 i_div32(u32 sh, u32 a, u32 b) { return t32_aem_div32(sh, a, b); }
static u32 i_mult2_32(u32 sh, u32 a, u32 b) { return t32_aem_mult2_32(sh, a, b); }
static u32 i_mult3_32(u32 sh, u32 a, u32 b, u32 c) { return t32_aem_mult3_32(sh, a, b, c); }
static t32_u64 i_mult2(u32 sh, t32_u64 a, t32_u64 b) { return t32_fixp_mult2(sh, a, b); }
static t32_u64 i_mult3_64(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c) { return t32_aem_mult3_64(sh, a, b, c); }
static u32 i_exp2(u32 v, u32 iq, u32 oq) { return t32_aem_exp2(v, iq, oq); }
static const struct t32_ae_init_ops init_ops = {
	i_div32, i_mult2_32, i_mult3_32, i_mult2, i_mult3_64, i_exp2, drv_warn,
};

/* lib_tisp_sync_sensor_attr's AE hook: tisp_ae_par_sensor_trig -> tisp_normal_ae_par_sensor_trig */
static u32 sns_trig(void *ctx, u32 ch)
{
	(void)ctx;
	t33_normal_ae_par_sensor_trig(ch & 0xff);
	return 0;
}

/*
 * A sensor attribute sync after stream-on (the sensor's set_fps etc. notify
 * TX_ISP_EVENT_SYNC_SENSOR_ATTR): the vendor ispcore_sync_sensor_attr ->
 * lib_tisp_sync_sensor_attr copies the run-time fields into the AE's sensor
 * copy and runs the AE hook (ae_cache + 0x505), so the next AE calc runs
 * tisp_ae_par_update_trig (openimp #574).  Unlocked like the vendor: the AE
 * work itself calls the sensor's set_fps under aec_lock.  Before the attach
 * the stream-on sync of t33_aec_attach covers it.
 */
void t33_aec_sensor_sync(void)
{
	struct t32_lib_hooks h = { .ae_sensor_trig = sns_trig };
	const u8 *vin, *si;
	u8 blk[T32_AESNS_BLK];

	if (!ACCESS_ONCE(ready) || t32_core_sensor_ae_src(&vin, &si))
		return;
	t32_ae_sensor_blk(blk, vin, si);
	t32_lib_sync_sensor_attr(&h, sblk, 0, blk);
	st.syncs++;
}

/*
 * The callees of the vendor entry points (struct t33_aeint_ops) on the T32
 * algorithm chain (t32_aec_cur, bound by the caller).
 */
static int aw_long_ev_alloc(u32 ch) { t32_aec_long_alloc(ch); return 0; }
static u32 aw_rd4(u32 reg) { return reg == T33_AE_IDX_REG ? aew_reg4 : t33_hw_rd(reg); }
static u32 aw_rd5(u32 reg) { return reg == T33_AE_IDX_REG ? aew_reg5 : t33_hw_rd(reg); }
/* event 1 (tisp_normal_ae_calc_process): the caller runs it next in the same ordered work */
static int aw_event_push(u32 ch, void *ev) { (void)ch; (void)ev; st.events++; return 0; }

static int aec_init_chain(void);

static int aw_trig(u32 ch, u32 par)
{
	(void)par;
	if (aew_in_init) {
		aew_init_rc = aec_init_chain();
		return 0;
	}
	t33_aec_cp_trig(ch);
	return 0;
}
static int aw_alg(u32 ch) { t33_aec_cp_alg(ch); return 0; }
static int aw_long(u32 ch) { t32_aec_cp_long(ch); return 0; }
static void aw_complete(void *c) { (void)c; }
static u32 aw_m2(u32 sh, u32 a, u32 b) { return t32_aem_mult2_32(sh, a, b); }
static u32 aw_m3(u32 sh, u32 a, u32 b, u32 c) { return t32_aem_mult3_32(sh, a, b, c); }
static u32 aw_log2(u32 v, u32 i, u32 o) { return t32_log2_fixed_to_fixed(v, i, o); }
/* the debug-only callees (zeratul_open = 0: not reached) */
static int aw_luma(u32 ch, u8 *l) { (void)ch; *l = 0; return 0; }
static int aw_attr(u32 ch, void *a) { (void)ch; (void)a; return 0; }
/* tisp_ae_init_par_calc: inside the T32 init chain (t32_ae_ctx_init) */
static int aw_none(u32 ch) { (void)ch; return 0; }
/* tisp_flicker_init (vendor 0x2fa84): fliker_info zeroed, fliker_para = { 20, 30, 14, 5 } */
static int aw_flicker(u32 ch)
{
	u8 *p = t33_p(fliker_para[ch & 0xff]);

	memset(t33_p(fliker_info[ch & 0xff]), 0, T32_AEI_D_BYTES);
	t32_wr32(p, 12, 5);
	t32_wr32(p, 8, 14);
	t32_wr32(p, 0, 20);
	t32_wr32(p, 4, 30);
	return 0;
}
static int aw_start(u32 ch, u32 cfg)
{
	u8 *R = (u8 *)ae_cache[ch & 0xff];

	t33_ae_start_param(R, t33_p(t33_le32(R + T33_AEW_TUNE)), t33_p(cfg));	/* tisp_ae_set_start_param */
	return 0;
}
static void *aw_copy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
static void *aw_fill(void *d, int c, size_t n) { return memset(d, c, n); }
static void aw_spin(void *l) { (void)l; }	/* aec_lock serialises the AE */

static const struct t33_aeint_ops aw_ops4 = {
	aw_long_ev_alloc, aw_rd4, aw_event_push, aw_trig, aw_alg, aw_long, aw_complete, aw_m2, aw_m3, aw_log2,
	aw_luma, aw_attr, aw_flicker, aw_none, aw_start, aw_copy, aw_fill, aw_spin,
};
static const struct t33_aeint_ops aw_ops5 = {
	aw_long_ev_alloc, aw_rd5, aw_event_push, aw_trig, aw_alg, aw_long, aw_complete, aw_m2, aw_m3, aw_log2,
	aw_luma, aw_attr, aw_flicker, aw_none, aw_start, aw_copy, aw_fill, aw_spin,
};

/*
 * ae_cache_default (the vendor .data template, 1744 B): the T32 state
 * template up to +1699 without the face / ROI words 1500..1512 and the T32
 * calc flag 1696, the T33 flag bytes 1701 / 1702 (word 1700 = 0x10100).
 * Checked word for word against tx-isp-t33.ko 3028d4ac (.data + 0x5be0).
 * b: the AE block (T33_AEW_BYTES), scratch here.
 */
static void aew_default(u8 *b)
{
	u8 *R = b + T33_AEW_INFO;

	t32_ae_state_tmpl(R);
	memset(R + 1500, 0, 16);
	t32_wr32(R, 1696, 0);
	t32_wr32(R, 1700, 0x10100);
	memcpy(ae_cache_default, R, T33_AEW_DEF_BYTES);
	memset(b, 0, T33_AEW_BYTES);
}

/* tisp_normal_ae_init's tisp_ae_par_update_trig / init_par_calc: the T32 AE context init, the exposure-info copy */
static int aec_init_chain(void)
{
	struct t32_stats_write w[8];
	int n = t32_ae_ctx_init(0, &aec.b, sblk, aew_sinfo, 1, &init_ops, w);	/* its register tail: list 30 / 5 */

	if (n < 0)
		return n;
	aec.S = sblk;
	aec.dcw = 1;
	aec.ops = &drv_ops;
	t32_aec_bind(&aec);
	t33_aec_expr_trig(&aec);	/* the T33 tisp_ae_par_update_trig's exposure-info copy */
	return 0;
}

/*
 * tisp_ae_malloc_info (from tisp_ae_init, list 30 being built in l): the
 * statistics / histogram buffers (allocated by the caller, DMA pages) into
 * the AE hardware parameter block and list 30 slots 7..14.
 */
static void *am_kmalloc(u32 size, u32 flags) { (void)size; (void)flags; return NULL; }
static void am_kfree(void *p) { (void)p; }
static u32 *am_list;
static void am_reg(u32 ch, u32 mod, u32 slot, u32 val)
{
	(void)ch;
	if (mod == 30 && am_list)
		T33_SW(am_list, slot, val);
}
static void am_err(int level, int line) { isp_printf(level, "tx-isp-t33: AE info allocation failed (line %d)\n", line); }
static const struct t33_aemisc_ops am_ops = { am_kmalloc, am_kfree, am_reg, am_err };

int t33_aec_malloc_info(u32 *list, unsigned long stat, unsigned long hist)
{
	int r;

	t33_ae_hw_par[6] = (u32)stat;	/* allocated: tisp_ae_malloc_info keeps them */
	t33_ae_hw_par[9] = (u32)hist;
	am_list = list;
	r = t33_ae_malloc_info(0, t33_ae_hw_par, &am_ops);
	am_list = NULL;
	g_ae_contexts[0] = (u32)(unsigned long)t33_ae_hw_par;
	return r;
}

static void aec_free(void)
{
	ae_init_flag[0] = 0;	/* the vendor keeps its block: nothing may reach the freed one */
	ae_cache[0] = NULL;
	stAeInfo[0] = ae_contexts[0] = 0;
	kfree(aec.b.ctx); kfree(aec.b.T); kfree(aec.b.B);	/* R, D, C: in aew_blk */
	kfree(aec.hist); kfree(aec.comp); kfree(aec.lum);
	kfree(aew_blk); kfree(aew_start);
	aew_blk = aew_start = NULL;
	memset(&aec, 0, sizeof(aec));
}

static void aec_work_fn(struct work_struct *w)
{
	unsigned int n;
	u32 dg, packed, zb0 = front.zbad, hb0 = front.hbad;

	(void)w;
	mutex_lock(&aec_lock);
	if (!ready)
		goto out;
	/* system irq 4: tisp_ae_interrupt_static -> tisp_normal_ae_interrupt_hist (long EV alloc, histogram index) */
	t32_aec_bind(&aec);
	aec.n = 0;
	t33_ae_int_hist(0, (u8 *)ae_cache[0], stat_num, &aw_ops4);
	n = aec.n;
	front.zidx = stat_num[0];
	front.hidx = stat_num[1];
	t33_ae_load(&aec, &front, zbufs + front.zidx * T33_AE_ZBUF_BYTES, hbufs + front.hidx * T33_AE_HBUF_BYTES);
	st.writes += n;
	if (t33_ae_dg_word(&aec, n, &dg)) {
		t33_dg_list_set(dg);
		st.dg_sets++;
		st.dg = dg;
	}
	/* event 1: tisp_normal_ae_calc_process (zeratul_open = 0: no debug tail) */
	t33_ae_tagerr = front.zbad != zb0 || front.hbad != hb0;
	aec.n = 0;
	t33_ae_calc_process(0, (u8 *)ae_cache[0], aec.stop, t33_le32(aew_blk + T33_AEW_PAR), &aeFrameCount[0], 0, 0, 0,
			    ae_algo_comp, &aw_ops4);
	st.writes += aec.n;
	st.frames++;
	st.mean = t32_st16(aec.b.B, 12366);
	st.target = t32_st16(aec.b.B, 8570);
	packed = aec_it | aec_ag << 16;
	if (st.frames <= aec_trace)
		pr_info("t33 ae %u: zc %u lo %u hi %u ztag %x htag %x hb128 %u\n", st.frames,
			t32_ld32(aec.b.B, 2048 + 60 * 7 + 28), t32_ld32(aec.b.B, 4748 + 60 * 7 + 28),
			t32_ld32(aec.b.B, 5648 + 60 * 7 + 28), t33_st_le16(zbufs + front.zidx * T33_AE_ZBUF_BYTES),
			t33_st_le16(hbufs + front.hidx * T33_AE_HBUF_BYTES), t32_ld32(aec.b.B, 4 * 128));
	if (st.frames <= aec_trace)
		pr_info("t33 ae %u: z%u h%u mode %u n %u mean %u (w %u) target %u it %u ag %u dg %u -> sns 0x%x dg 0x%x ev 0x%x:%x\n",
			st.frames, front.zidx, front.hidx, aec.b.R[T33_AEW_READY], n, st.mean, t32_ld32(aec.b.R, 1524), st.target,
			t32_ld32(aec.b.R, 576), t32_ld32(aec.b.R, 580), t32_ld32(aec.b.R, 588), packed, st.dg,
			st.ev_hi, st.ev_lo);
	if (t32_ld32(aec.b.R, T33_AEW_READY) == 1 && aec_it && packed != aec_sent) {
		if (!t32_core_sensor_expo(packed)) {
			aec_sent = packed;
			st.expo_sets++;
		} else {
			st.expo_fail++;
		}
	}
out:
	mutex_unlock(&aec_lock);
}
static DECLARE_WORK(aec_work, aec_work_fn);

/*
 * The vendor runs AE calc, AWB and the gain / EV refreshes in one event
 * thread in push order (AWB irq 3 before AE irq 4).  One ordered queue for
 * the AWB and AE works keeps that order; schedule_work when it is missing.
 */
static struct workqueue_struct *evq;
static DEFINE_SPINLOCK(evq_lock);

bool t33_evq_queue(struct work_struct *w)
{
	struct workqueue_struct *q = ACCESS_ONCE(evq);

	return q ? queue_work(q, w) : schedule_work(w);
}

void t33_evq_get(void)
{
	struct workqueue_struct *q;

	if (ACCESS_ONCE(evq))
		return;
	q = alloc_ordered_workqueue("t33_isp_ev", 0);
	if (!q)
		return;
	spin_lock(&evq_lock);
	if (!evq) {
		evq = q;
		q = NULL;
	}
	spin_unlock(&evq_lock);
	if (q)
		destroy_workqueue(q);
}

/* module teardown, after both detaches */
void t33_evq_free(void)
{
	struct workqueue_struct *q;

	spin_lock(&evq_lock);
	q = evq;
	evq = NULL;
	spin_unlock(&evq_lock);
	if (q)
		destroy_workqueue(q);
}

/* system irq 4 (status bit 4): histogram index, then the AE frame */
void t33_aec_irq4(u32 idx)
{
	if (!ACCESS_ONCE(ready))
		return;
	ACCESS_ONCE(aew_reg4) = idx;	/* tisp_normal_ae_interrupt_hist reads it in the work */
	t33_evq_queue(&aec_work);
}

/* system irq 5 (status bit 5): zone buffer index */
void t33_aec_irq5(u32 idx)
{
	if (!ACCESS_ONCE(ready))
		return;
	aew_reg5 = idx;
	t33_ae_int_static(0, stat_num, &aw_ops5);	/* tisp_normal_ae_interrupt_static: zone buffer index */
	st.irq5++;
}

void t33_aec_detach(void)
{
	ACCESS_ONCE(ready) = false;
	cancel_work_sync(&aec_work);
	wait_event(aec_api_wq, !ACCESS_ONCE(aec_api_busy));	/* a tuning call may be in t32_aec_api_sleep */
	mutex_lock(&aec_lock);
	aec_free();
	mutex_unlock(&aec_lock);
}

/*
 * stream-on, after the AE lists: sec = the IQ bin AE section (T32 layout,
 * 3288 B), zb / hb = the four zone and four histogram buffers (uncached).
 */
int t33_aec_attach(const u8 *sec, u32 len, u32 width, u32 height, const u8 *zb, const u8 *hb)
{
	struct t32_lib_hooks h = { .ae_sensor_trig = sns_trig };
	const u8 *vin, *si;
	u8 out_a[64] = { 0 }, blk[T32_AESNS_BLK];
	struct t33_aeinit_io io = {
		&fliker_info[0], &fliker_para[0], &stAeStatBuff[0], &stAeHistBuff[0], &stAeInfo[0], (u32 *)&ae_cache[0],
		&ae_contexts[0], &ae_init_flag[0], ae_cache_default,
	};
	int ret = 0;

	t33_aec_detach();
	t33_evq_get();
	mutex_lock(&aec_lock);
	memset(&st, 0, sizeof(st));
	memset(&front, 0, sizeof(front));
	if (len < T32_AEI_T_BYTES || !zb || !hb) {
		ret = -EINVAL;
		goto out;
	}
	if (t32_core_sensor_ae_src(&vin, &si)) {
		ret = -ENODEV;
		goto out;
	}
	t32_ae_sensor_blk(blk, vin, si);
	memset(sblk, 0, sizeof(sblk));
	t32_lib_sync_sensor_attr(&h, sblk, 0, blk);
	aec.b.T = kmemdup(sec, T32_AEI_T_BYTES, GFP_KERNEL);
	aec.b.ctx = kzalloc(T32_AEI_CTX_BYTES, GFP_KERNEL);
	aec.b.B = kzalloc(T32_AEI_B_BYTES, GFP_KERNEL);
	aew_blk = kzalloc(T33_AEW_BYTES, GFP_KERNEL);
	aew_start = kzalloc(T33_AEW_START_BYTES, GFP_KERNEL);
	aec.hist = kzalloc(T32_AEC_HIST_BYTES, GFP_KERNEL);
	aec.comp = kzalloc(T32_AEC_COMP_BYTES, GFP_KERNEL);
	aec.lum = kzalloc(T32_AEC_HIST_BYTES, GFP_KERNEL);
	if (!aec.b.T || !aec.b.ctx || !aec.b.B || !aew_blk || !aew_start || !aec.hist || !aec.comp || !aec.lum) {
		ret = -ENOMEM;
		goto out;
	}
	aew_default(aew_blk);
	aec.b.R = aew_blk + T33_AEW_INFO;	/* the T32 chain's state block = the vendor ae_cache */
	aec.b.D = aew_blk + T33_AEW_FLKI;	/* its D / C blocks = the vendor fliker_info / fliker_para */
	aec.b.C = aew_blk + T33_AEW_FLKP;
	t33_tunblk_hist_bins(0, aec.hist + T32_AEA_HIST_BIN);
	memset(aew_sinfo, 0, sizeof(aew_sinfo));
	t32_wr32(aew_sinfo, 0, width);
	t32_wr32(aew_sinfo, 4, height);
	t32_wr32(aew_sinfo, 12, t32_core_sensor_fps());
	/* tisp_ae_init's AE block: tuning pointer, parameter / start block, buffers */
	t32_wr32(aec.b.R, T33_AEW_TUNE, (u32)(unsigned long)aec.b.T);
	t32_wr32(aew_blk, T33_AEW_PAR, (u32)(unsigned long)aec.b.T);
	t32_wr32(aew_blk, T33_AEW_CFG, (u32)(unsigned long)aew_start);
	t32_wr32(aew_blk, T33_AEW_STAT, (u32)(unsigned long)zb);
	t32_wr32(aew_blk, T33_AEW_HIST, (u32)(unsigned long)hb);
	/* the start block: picture size (+12 / +14), first-frame EV / again / dgain / integration time (+920 ..) */
	t32_wr16(aew_start, 12, width);
	t32_wr16(aew_start, 14, height);
	t32_wr32(aew_start, 920, (u32)T33_AE_FST_EV);
	t32_wr32(aew_start, 924, 0);
	t32_wr32(aew_start, 932, T33_AE_FST_AG);
	t32_wr32(aew_start, 936, T33_AE_FST_DG);
	t32_wr32(aew_start, 940, T33_AE_FST_IT);
	aew_in_init = true;
	aew_init_rc = -ENODEV;
	t33_normal_ae_init(0, aew_blk, out_a, (u8 *)t33_ae_hw_par, &io, &aw_ops4);	/* tisp_normal_ae_init */
	aew_in_init = false;
	if (aew_init_rc) {
		ret = aew_init_rc;
		goto out;
	}
	/*
	 * The vendor's STREAMON runs tisp_init (tisp_ae_init) before the
	 * sensor's stream-on attribute sync (ispcore_sync_sensor_attr ->
	 * lib_tisp_sync_sensor_attr -> tisp_normal_ae_par_sensor_trig), so
	 * ae_cache + 0x505 is 1 at the first calc (QEMU peek of the vendor
	 * ae_cache, openimp #570): the first tisp_ae_tune skips its stable-band
	 * exit and runs, tisp_ae_long_ev_alloc follows at the next irq 4.  Our
	 * attach comes after that sync (whose AE hook found no AE yet): the
	 * hook of that sync here, after the init.
	 */
	sns_trig(NULL, 0);
	t33_ae_zone_sizes(&aec);	/* the T32 B block's copy of the zone sizes (vendor: stAeInfo + 7100) */
	zbufs = t33_p(stAeStatBuff[0]);
	hbufs = t33_p(stAeHistBuff[0]);
	if (aec_trace)
		pr_info("t33 ae S: tw %u th %u ag %u dg %u r %x %x %x it %u..%u b %u %u %u %u limits %u %u %u %u %u %u %u %u\n",
			t32_st16(sblk, 0), t32_st16(sblk, 2), t32_ld32(sblk, 36), t32_ld32(sblk, 40),
			t32_ld32(sblk, 56), t32_ld32(sblk, 60), t32_ld32(sblk, 64), t32_ld32(sblk, 68),
			t32_ld32(sblk, 72), sblk[116], sblk[117], sblk[118], sblk[123],
			t32_ld32(aec.b.B, 7536), t32_ld32(aec.b.B, 7540), t32_ld32(aec.b.B, 7544), t32_ld32(aec.b.B, 7548),
			t32_ld32(aec.b.B, 7552), t32_ld32(aec.b.B, 7556), t32_ld32(aec.b.B, 7560), t32_ld32(aec.b.B, 7564));
	aec_it = aec_ag = 0;
	aec_sent = ~0U;
	aec_wsel[0] = aec_wsel[1] = 0;
out:
	if (ret)
		aec_free();
	ACCESS_ONCE(ready) = !ret;
	mutex_unlock(&aec_lock);
	return ret;
}

/* normal_ae_dn_par_refresh (tx_isp_t33_misc1.h): its callees on the AE chain */
u32 system_reg_read(u32 reg);
void system_reg_write(u32 reg, u32 val);
static void aew_deflicker(u32 ch)
{
	(void)ch;
	t32_ae_clac_deflicker_cfg(&aec.b, &t32_aec_iops);	/* tisp_ae_clac_deflicker_cfg */
}
#define tisp_ae_clac_deflicker_cfg aew_deflicker
#include "tx_isp_t33_misc1.h"
#undef tisp_ae_clac_deflicker_cfg

/*
 * The AE part of the bin switch (lib_tisp_refresh_para, vendor order top,
 * dpc, blc, ae): the vendor's stAeInfo + 1744 points into tparamsP, which
 * SET_TIZIANO_PAR overwrote with the new bin; here the new AE section is
 * copied into the tuning block T.  Then tx_isp_t32_hw.c runs
 * tisp_ae_switch_para (list 30 from the AE hardware parameter block) and its
 * normal_ae_dn_par_refresh (below).
 */
void t33_aec_switch_sec(const u8 *sec, u32 len)
{
	mutex_lock(&aec_lock);
	if (ready && len >= T32_AEI_T_BYTES)
		memcpy(aec.b.T, sec, T32_AEI_T_BYTES);
	mutex_unlock(&aec_lock);
}

/* normal_ae_dn_par_refresh: deflicker table, ae_cache + 1285 (parameter update at the next calc), list 30 trigger */
int t33_aec_dn_par_refresh(u32 ch)
{
	mutex_lock(&aec_lock);
	if (ready && ae_cache[ch & 3]) {
		t32_aec_bind(&aec);
		t33_ae_dn_par_refresh((u8 *const *)ae_cache, ch & 3);
		st.dn_refresh++;
	}
	mutex_unlock(&aec_lock);
	return 0;
}

void t33_aec_show(struct seq_file *m)
{
	mutex_lock(&aec_lock);
	seq_printf(m, "t33 ae: %s frames %u irq5 %u ready %u writes %u dg %u (0x%x) expo %u fail %u fps %u ev %u dn %u syncs %u\n",
		   ready ? "on" : "off", st.frames, st.irq5, aec.b.R ? t32_ld32(aec.b.R, T33_AEW_READY) : 0, st.writes,
		   st.dg_sets, st.dg, st.expo_sets,
		   st.expo_fail, st.fps_sets, st.events, st.dn_refresh, st.syncs);
	seq_printf(m, "t33 ae: it %u again %u sent 0x%x mean %u target %u tgain 0x%x ev 0x%x:%x bv 0x%x tag errors %u/%u\n",
		   aec_it, aec_ag, aec_sent, st.mean, st.target, st.tgain, st.ev_hi, st.ev_lo, st.bv, front.zbad,
		   front.hbad);
	mutex_unlock(&aec_lock);
}

/*
 * The tuning controls (../t32/tx_isp_t32_tunblk.c) on the live T33 AE
 * blocks, as in ../t32/tx_isp_t32_ae.c: the vendor T33 AE API keeps the same
 * { T, B }, R, S, hist / comp / lum blocks and weight selectors (the T33 AE
 * is the T32 algorithm with the T33 statistics front).  False and unlocked
 * while the per-frame AE is not attached.
 */
bool t32_aec_api_get(struct t32_aea_ctx *c)
{
	mutex_lock(&aec_lock);
	if (!ready) {
		mutex_unlock(&aec_lock);
		return false;
	}
	aec_api_busy++;
	memset(c, 0, sizeof(*c));
	c->tune = aec.b.T;
	c->buf = aec.b.B;
	c->st = aec.b.R;
	c->fps = aec.S;
	c->hist = aec.hist;
	c->wsel = aec_wsel;
	c->comp = aec.comp;
	c->lum = aec.lum;
	return true;
}

void t32_aec_api_put(void)
{
	aec_api_busy--;
	mutex_unlock(&aec_lock);
	wake_up(&aec_api_wq);
}

/* the msleep of api_ae_get_exprinfo_attr: the AE frames run meanwhile */
void t32_aec_api_sleep(u32 ms)
{
	mutex_unlock(&aec_lock);
	msleep(ms);
	mutex_lock(&aec_lock);
}

/* tisp_g_bv -> tisp_ae_get_bv on the live blocks (lock held) */
u32 t32_aec_api_bv(void)
{
	t32_aec_bind(&aec);
	return t32_aec_bv(0);
}

/* tisp_ae_api_set_exp_list: the pipeline word dc284 + 900 */
u32 t32_aec_api_dcw(void)
{
	return aec.dcw;
}
