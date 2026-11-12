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
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/seq_file.h>
#include "tx_isp_t32.h"
#include "tx_isp_t33_aealg.h"
#include "tx_isp_t32_aesns.h"
#include "tx_isp_t32_libwrap.h"

int isp_printf(unsigned int level, const char *fmt, ...);
u32 t33_hw_rd(u32 reg);
void t33_dg_list_set(u32 v);				/* tx_isp_t32_hw.c: list 5 slots 3 / 4, run */

static unsigned int aec_trace;
module_param(aec_trace, uint, 0644);
MODULE_PARM_DESC(aec_trace, "T33 AE: log the first N AE frames (debug), default 0");

static struct t32_aec aec;
static struct t33_ae_front front;
static u8 sblk[T32_SENSOR_ATTR_COPY];
static const u8 *zbufs, *hbufs;
static DEFINE_MUTEX(aec_lock);
static bool ready;
static u32 aec_it, aec_ag, aec_sent;
static u32 tgain_old;	/* tisp_long_tgain_update's last gain (vendor .bss, not reset per stream) */
static u32 ev_last[2];	/* tisp_long_ev_update's last EV */
static struct {
	u32 frames, writes, dg_sets, expo_sets, expo_fail, fps_sets, events, irq5;
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
			t33_ev_refresh(ev[4], ev[5]);
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

static u32 sns_trig(void *ctx, u32 ch) { (void)ctx; (void)ch; return 0; }

static void aec_free(void)
{
	kfree(aec.b.ctx); kfree(aec.b.T); kfree(aec.b.B); kfree(aec.b.R); kfree(aec.b.D); kfree(aec.b.C);
	kfree(aec.hist); kfree(aec.comp); kfree(aec.lum);
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
	n = t33_ae_frame(&aec, &front, zbufs, hbufs);
	st.writes += n;
	if (t33_ae_dg_word(&aec, n, &dg)) {
		t33_dg_list_set(dg);
		st.dg_sets++;
		st.dg = dg;
	}
	t33_ae_calc(&aec, &front, zb0, hb0);	/* tisp_normal_ae_calc_process: no register writes on the taken path */
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
			st.frames, front.zidx, front.hidx, aec.mode, n, st.mean, t32_ld32(aec.b.R, 1524), st.target,
			t32_ld32(aec.b.R, 576), t32_ld32(aec.b.R, 580), t32_ld32(aec.b.R, 588), packed, st.dg,
			st.ev_hi, st.ev_lo);
	if (aec.mode == 1 && aec_it && packed != aec_sent) {
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
	front.hidx = t33_ae_hbuf_idx(idx);
	t33_evq_queue(&aec_work);
}

/* system irq 5 (status bit 5): zone buffer index */
void t33_aec_irq5(u32 idx)
{
	if (!ACCESS_ONCE(ready))
		return;
	front.zidx = t33_ae_zbuf_idx(idx);
	st.irq5++;
}

void t33_aec_detach(void)
{
	ACCESS_ONCE(ready) = false;
	cancel_work_sync(&aec_work);
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
	struct t32_stats_write w[8];
	const u8 *vin, *si;
	u8 info[64] = { 0 }, blk[T32_AESNS_BLK];
	int n, ret = 0;

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
	aec.b.R = kzalloc(T32_AEI_R_BYTES, GFP_KERNEL);
	aec.b.D = kzalloc(T32_AEI_D_BYTES, GFP_KERNEL);
	aec.b.C = kzalloc(T32_AEI_C_BYTES, GFP_KERNEL);
	aec.hist = kzalloc(T32_AEC_HIST_BYTES, GFP_KERNEL);
	aec.comp = kzalloc(T32_AEC_COMP_BYTES, GFP_KERNEL);
	aec.lum = kzalloc(T32_AEC_HIST_BYTES, GFP_KERNEL);
	if (!aec.b.T || !aec.b.ctx || !aec.b.B || !aec.b.R || !aec.b.D || !aec.b.C || !aec.hist || !aec.comp ||
	    !aec.lum) {
		ret = -ENOMEM;
		goto out;
	}
	t32_ae_state_tmpl(aec.b.R);
	t32_wr32(info, 0, width);
	t32_wr32(info, 4, height);
	t32_wr32(info, 12, t32_core_sensor_fps());
	n = t32_ae_ctx_init(0, &aec.b, sblk, info, 1, &init_ops, w);	/* its register tail: list 30 / 5 on the T33 */
	if (n < 0) {
		ret = n;
		goto out;
	}
	aec.S = sblk;
	aec.dcw = 1;
	aec.ops = &drv_ops;
	t32_aec_bind(&aec);
	t33_ae_set_start_param(&aec, T33_AE_FST_IT, T33_AE_FST_AG, T33_AE_FST_DG, T33_AE_FST_EV);
	t33_ae_zone_sizes(&aec);
	zbufs = zb;
	hbufs = hb;
	if (aec_trace)
		pr_info("t33 ae S: tw %u th %u ag %u dg %u r %x %x %x it %u..%u b %u %u %u %u limits %u %u %u %u %u %u %u %u\n",
			t32_st16(sblk, 0), t32_st16(sblk, 2), t32_ld32(sblk, 36), t32_ld32(sblk, 40),
			t32_ld32(sblk, 56), t32_ld32(sblk, 60), t32_ld32(sblk, 64), t32_ld32(sblk, 68),
			t32_ld32(sblk, 72), sblk[116], sblk[117], sblk[118], sblk[123],
			t32_ld32(aec.b.B, 7536), t32_ld32(aec.b.B, 7540), t32_ld32(aec.b.B, 7544), t32_ld32(aec.b.B, 7548),
			t32_ld32(aec.b.B, 7552), t32_ld32(aec.b.B, 7556), t32_ld32(aec.b.B, 7560), t32_ld32(aec.b.B, 7564));
	aec_it = aec_ag = 0;
	aec_sent = ~0U;
out:
	if (ret)
		aec_free();
	ACCESS_ONCE(ready) = !ret;
	mutex_unlock(&aec_lock);
	return ret;
}

void t33_aec_show(struct seq_file *m)
{
	mutex_lock(&aec_lock);
	seq_printf(m, "t33 ae: %s frames %u irq5 %u mode %u writes %u dg %u (0x%x) expo %u fail %u fps %u ev %u\n",
		   ready ? "on" : "off", st.frames, st.irq5, aec.mode, st.writes, st.dg_sets, st.dg, st.expo_sets,
		   st.expo_fail, st.fps_sets, st.events);
	seq_printf(m, "t33 ae: it %u again %u sent 0x%x mean %u target %u tgain 0x%x ev 0x%x:%x bv 0x%x tag errors %u/%u\n",
		   aec_it, aec_ag, aec_sent, st.mean, st.target, st.tgain, st.ev_hi, st.ev_lo, st.bv, front.zbad,
		   front.hbad);
	mutex_unlock(&aec_lock);
}
