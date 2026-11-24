// SPDX-License-Identifier: GPL-2.0
/*
 * T32 per-frame vendor AE in the driver (openimp #234): the AE chain of
 * tx_isp_t32_aechain.h (oracle t32_aechain_oracle) on the AE context that
 * tx_isp_t32_tisp.c builds at stream-on (t32_pf_attach_ae), behind
 * t32_ported_paths bit 10 (T32_PP_AE).
 *
 * Every AE zone statistics interrupt (system irq 4, ISP status bit 4)
 * schedules one AE frame: tisp_ae_interrupt_static (with
 * tisp_ae_long_ev_alloc once the AE tune ran: e1510 = 1) and then the AE
 * process the vendor runs from event 1 (tisp_ae_calc_process).  Their
 * register writes go out in order (direct mode).  The sensor entries of the
 * dd2d4 block go to the sensor driver as the ISP core's ops do:
 * +124 alloc again -> the sensor's alloc_again callback, +168 / +184 set the
 * integration time / gain register value of the packed word
 * it | again << 16, which is handed to the sensor (TX_ISP_EVENT_SENSOR_EXPO)
 * once per change after the frame; +212 the frame rate.  Event 4 (the long
 * total gain, log2 Q16) is kept for the per-frame block refresh
 * (t32_long_tgain_update in tx_isp_t32_tisp.c), which used the fixed
 * tisp_tgain before; on a change it also goes to the AWB (tisp_awb_tgain_update,
 * t32_awbc_tgain).  Event 6 (the long EV) runs tisp_long_ev_update
 * (t32_ctup_ev: AWB, CCM, BCSH; gamma / defog / TMO / WDR not wired); event 5
 * (again) is not wired yet.
 *
 * The vendor runs the interrupt part in the IRQ and the process in its event
 * thread; here both run in one work item (the sensor calls sleep on I2C).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_aechain.h"
#include "tx_isp_t32_libwrap.h"
#include "tx_isp_t32_aesns.h"
#include "tx_isp_t32_abi.h"

int isp_printf(unsigned int level, const char *fmt, ...);

struct t32_aec_drv_stats t32_aec_stats;

static struct t32_aec aec;
static DEFINE_MUTEX(aec_lock);
static bool aec_ready;
static u32 aec_it, aec_ag, aec_sent;
static u32 aec_tgain;
static bool aec_tgain_valid;
static u32 aec_last_tg;		/* vendor tisp_long_tgain_update's last low gain word */
static u8 aec_wsel[2];			/* vendor e1570[ch]: the two AE weight selectors */
static unsigned int aec_api_busy;	/* tuning API calls on the blocks (t32_aec_api_get .. _put) */
static DECLARE_WAIT_QUEUE_HEAD(aec_api_wq);
/*
 * The live sensor block for TX_ISP_EVENT_SYNC_SENSOR_ATTR (t32_aec_sensor_sync).  Its own lock, not aec_lock:
 * the sync comes from the sensor's set_fps, which the AE work and t32_aec_tisp_set_fps call under aec_lock.
 */
#define T32_AEC_R_PAR_TRIG	1285U
/*
 * The AE histogram block (vendor .data tisp_ae_hist, tx-isp-t32.ko .data + 0x37a0) is not zero at load: bytes 1044..1051
 * hold the 5-bin thresholds 13 / 64 / 144 / 192 and 15 / 15 (tisp_ae_get_hist_bin / set_hist_bin, api_ae_long_process).
 * A zeroed block put the whole 5-bin histogram of GetAeStatistics into the last bin until SetStatisConfig (#859).
 */
#define T32_AEC_HIST_BIN_OFF	1044U
static u8 aec_hist_bin[8] = { 13, 64, 144, 192, 15, 15, 0, 0 };
static DEFINE_MUTEX(aec_sync_lock);
static u8 *aec_sync_S, *aec_sync_R;

static u32 drv_rd(u32 ch, u32 reg) { (void)ch; return t32_hw_rd(reg); }
static u32 drv_sys(u32 reg) { return t32_hw_rd(reg); }

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
		t32_aec_stats.fps_sets++;
		return (u32)t32_core_sensor_set_fps(arg);
	}
	return 0;
}

/*
 * The AE events as the vendor's event thread dispatches them (handler words:
 * 64-bit value = ev[4] | ev[5] << 32, then ev[6], ev[7]): event 6 ->
 * tisp_long_ev_update (t32_ctup_ev); event 4 -> tisp_long_tgain_update, whose
 * AWB part runs when the low gain word changed or the high one is set.
 */
static void drv_event(u32 ch, const u32 *ev)
{
	(void)ch;
	t32_aec_stats.events++;
	if (ev[2] == 6)		/* tisp_long_ev_update: AWB / CCM / BCSH (tx_isp_t32_ctup.c) */
		t32_ctup_ev(ev[4], ev[5], ev[6], ev[7]);
	if (ev[2] == 4) {
		aec_tgain = ev[4];
		aec_tgain_valid = true;
		if (ev[4] != aec_last_tg || ev[5]) {
			aec_last_tg = ev[4];
			t32_awbc_tgain(ev[4]);	/* tisp_awb_tgain_update */
		}
	}
}

static void drv_warn(u32 line)
{
	isp_printf(2, "tx-isp-t32: AE sensor ratio word without integer part, vendor default (line %u)\n", line);
}

static void drv_done(void *d) { (void)d; }

static const struct t32_aec_ops drv_ops = { drv_rd, drv_sys, drv_sensor, drv_event, drv_warn, drv_done };

static void aec_flush(void)
{
	unsigned int i;

	for (i = 0; i < aec.n; i++)
		t32_hw_wr(aec.w[i].reg, aec.w[i].val);
	t32_aec_stats.writes += aec.n;
}

static void aec_work_fn(struct work_struct *w)
{
	u32 packed;

	(void)w;
	mutex_lock(&aec_lock);
	if (!aec_ready || !t32_pp_on(T32_PP_AE)) {
		mutex_unlock(&aec_lock);
		return;
	}
	/*
	 * tisp_ae_interrupt_hist (vendor tx-isp-t32.ko 0x2ab70, system irq 5): the histogram buffer
	 * 0x1708c & 3 (2 KiB each behind the four zone buffers) unpacked by tisp_ae_get_hist (0x2a9e4)
	 * into the AE block B + 0 (long) / + 1024 (short), as selected by bits 1 / 2 of top register
	 * 0x60.  Not wired before openimp #859: B stayed zero, GetAeStatistics showed one bin.  Run
	 * here, in the AE work, so it never races the AE frame (the vendor takes its spin lock).
	 */
	t32_ae_interrupt_hist(aec.zones + 0x4000, t32_hw_rd(T32_AE_CUR), aec.b.B, t32_hw_rd(0x60), t32_hw_rd(0x60));
	t32_aec_stats.hists++;
	t32_aec_irq(&aec);
	aec_flush();
	t32_aec_calc(&aec);
	aec_flush();
	t32_aec_stats.frames++;
	t32_aec_stats.mean = t32_st16(aec.b.B, 12366);
	t32_aec_stats.target = t32_st16(aec.b.B, 8570);
	t32_aec_stats.it = t32_ld32(aec.b.R, 576);
	t32_aec_stats.again = t32_ld32(aec.b.R, 580);
	t32_aec_stats.dgain = t32_ld32(aec.b.R, 588);
	t32_aec_stats.tgain = aec_tgain;
	packed = aec_it | aec_ag << 16;
	if (aec.mode == 1 && aec_it && packed != aec_sent) {
		if (!t32_core_sensor_expo(packed)) {
			aec_sent = packed;
			t32_aec_stats.expo_sets++;
		}
	}
	mutex_unlock(&aec_lock);
}
static DECLARE_WORK(aec_work, aec_work_fn);

/* the AE zone statistics interrupt */
void t32_aec_kick(void)
{
	if (READ_ONCE(aec_ready))
		schedule_work(&aec_work);
}

/* stream-on, after t32_ae_ctx_init: the blocks stay owned by the caller until t32_aec_detach */
int t32_aec_attach(const struct t32_ae_blk *b, u8 *S, const u8 *zones)
{
	int ret = 0;

	u8 roi[4];

	mutex_lock(&aec_lock);
	if (aec.hist)	/* vendor .data tisp_ae_hist: the histogram thresholds survive tisp_ae_init (#859) */
		memcpy(aec_hist_bin, aec.hist + T32_AEC_HIST_BIN_OFF, sizeof(aec_hist_bin));
	kfree(aec.hist); kfree(aec.comp); kfree(aec.lum);
	memcpy(roi, aec.roi, sizeof(roi));	/* vendor .bss: the face ROI bounds survive tisp_ae_init */
	memset(&aec, 0, sizeof(aec));
	memcpy(aec.roi, roi, sizeof(roi));
	aec.b = *b;
	aec.S = S;
	aec.zones = zones;
	aec.dcw = 1;		/* one pipeline, as t32_pf_attach_ae passes to t32_ae_ctx_init */
	aec.ops = &drv_ops;
	aec.hist = kzalloc(T32_AEC_HIST_BYTES, GFP_KERNEL);
	aec.comp = kzalloc(T32_AEC_COMP_BYTES, GFP_KERNEL);
	aec.lum = kzalloc(T32_AEC_HIST_BYTES, GFP_KERNEL);
	if (aec.hist)
		memcpy(aec.hist + T32_AEC_HIST_BIN_OFF, aec_hist_bin, sizeof(aec_hist_bin));
	aec_it = aec_ag = 0;
	aec_wsel[0] = aec_wsel[1] = 0;
	aec_sent = ~0U;
	aec_tgain_valid = false;
	aec_last_tg = 0;
	memset(&t32_aec_stats, 0, sizeof(t32_aec_stats));
	if (!zones || !aec.hist || !aec.comp || !aec.lum)
		ret = -ENOMEM;
	WRITE_ONCE(aec_ready, !ret);
	mutex_lock(&aec_sync_lock);
	aec_sync_S = ret ? NULL : S;
	aec_sync_R = ret ? NULL : b->R;
	mutex_unlock(&aec_sync_lock);
	mutex_unlock(&aec_lock);
	return ret;
}

void t32_aec_detach(void)
{
	u8 roi[4];

	WRITE_ONCE(aec_ready, false);
	mutex_lock(&aec_sync_lock);
	aec_sync_S = aec_sync_R = NULL;
	mutex_unlock(&aec_sync_lock);
	cancel_work_sync(&aec_work);
	wait_event(aec_api_wq, !READ_ONCE(aec_api_busy));	/* a tuning call may be in t32_aec_api_sleep */
	mutex_lock(&aec_lock);
	kfree(aec.hist); kfree(aec.comp); kfree(aec.lum);
	memcpy(roi, aec.roi, sizeof(roi));
	memset(&aec, 0, sizeof(aec));
	memcpy(aec.roi, roi, sizeof(roi));
	aec_tgain_valid = false;
	mutex_unlock(&aec_lock);
}

/*
 * tisp_set_fps (vendor 0xc0b58, openimp #716), the tisp part of tuning id 0x08000070 after the sensor took the
 * rate: tisp_ae_api_set_fps (0x46ccc: AE tune word +0x6cc == 1, the exposure list mode, refuses with the vendor's
 * print; else, when the sensor block's fps word S +0x38 differs, its entry S +0xd4 = sensor_set_fps (0x2515c),
 * which hands the rate on only when the input's live fps word differs), then the entry S +0x90 =
 * sensor_fps_control (0x25108): its block refresh goes to the caller's stack, its result is the input word after
 * the fps word (ispcore input +0x18c, which sensor_init / lib_tisp_sync_sensor_attr copy to S +0x3c; here
 * struct tx_isp_video_in +72 next to the fps word +68, the mapping of t32_ae_sensor_blk), < 0 -> "fps get wrong!"
 * and -1; then tisp_ae_clac_deflicker_cfg (0x40400) on the AE blocks (inputs T +0x67c / +0x6c8, R +0x4ec / +0x4f0,
 * B +0x216a, output B +0x322a and R +0x6a4: the port t32_ae_clac_deflicker_cfg).  fps = num << 16 | den.
 * Without the live AE (before stream-on) nothing runs here; returns 0.
 */
int t32_aec_tisp_set_fps(u32 ch, u32 fps)
{
	const u8 *vin, *attr;
	int r = 0;

	if (ch)
		return 0;
	mutex_lock(&aec_lock);
	if (!aec_ready) {
		mutex_unlock(&aec_lock);
		return 0;
	}
	if (t32_ld32(aec.b.T, 0x6cc) == 1) {
		isp_printf(1, "[%s, %d]:EXP LIST MODE, SET FPS FAILED \n", "tisp_ae_api_set_fps", 0x16f1);
	} else if (t32_ld32(aec.S, 0x38) != fps && t32_core_sensor_fps() != fps) {
		t32_aec_stats.fps_sets++;
		t32_core_sensor_set_fps(fps);
	}
	if (!t32_core_sensor_ae_src(&vin, &attr) && (int)t32_ld32(vin, 72) < 0) {
		isp_printf(2, "fps get wrong!\n");
		r = -1;
	} else {
		t32_ae_clac_deflicker_cfg(&aec.b, &t32_aec_iops);
	}
	mutex_unlock(&aec_lock);
	return r;
}

/*
 * TX_ISP_EVENT_SYNC_SENSOR_ATTR after stream-on (openimp #724): the vendor ispcore_sync_sensor_attr (0xb6ac4)
 * builds the 124 B block from the sensor's struct tx_isp_video_in (the notify data) and its attribute block
 * (t32_ae_sensor_blk, the mapping of t32_ispcore_sync_sensor_attr), then lib_tisp_sync_sensor_attr (0x27f70)
 * copies its run-time fields into stSensorAttr[ch] (S: +0x24 / +0x28, +0x38 .. +0x48, +0x58 .. +0x64,
 * +0x74 .. +0x7b, t32_lib_sync_sensor_attr) and tail-calls tisp_ae_par_sensor_trig (0x305e0): ae_init_flag[ch]
 * == 1 -> ae_cache[ch] + 0x505 (R + 1285) = 1, which the next tisp_ae_calc_process turns into
 * tisp_ae_par_update_trig (t32_ae_calc_process).  Unlocked against the AE like the vendor (the sensor's set_fps
 * runs under aec_lock); aec_sync_lock only keeps the blocks from going away.  Before the attach nothing runs:
 * the stream-on sync (ae_sensor_block) covers it.
 */
static u32 aec_sns_trig(void *ctx, u32 ch)
{
	u8 *R = ctx;

	(void)ch;
	if (R)			/* ae_init_flag: the AE is attached */
		R[T32_AEC_R_PAR_TRIG] = 1;	/* ae_cache[ch] + 0x505 */
	return 0;
}

void t32_aec_sensor_sync(const u8 *vin)
{
	struct t32_lib_hooks h = { .ae_sensor_trig = aec_sns_trig };
	const u8 *si;
	u8 blk[T32_AESNS_BLK];

	mutex_lock(&aec_sync_lock);
	si = vin ? T32_AT(vin, T32_VIDEO_ATTR, const u8 *) : NULL;
	if (aec_sync_S && si) {
		h.ctx = aec_sync_R;
		t32_ae_sensor_blk(blk, vin, si);
		t32_lib_sync_sensor_attr(&h, aec_sync_S, 0, blk);
		t32_aec_stats.syncs++;
		t32_aec_stats.sync_fps = t32_ld32(aec_sync_S, 0x38);
	}
	mutex_unlock(&aec_sync_lock);
}

/* the last long total gain of the vendor AE (event 4, log2 Q16); false before the first one */
bool t32_aec_tgain(u32 *g)
{
	bool v;

	mutex_lock(&aec_lock);
	v = aec_ready && aec_tgain_valid;
	if (v)
		*g = aec_tgain;
	mutex_unlock(&aec_lock);
	return v;
}

/*
 * The tuning controls (tx_isp_t32_tunblk.c) on the live AE blocks, as the
 * vendor AE API sees them: e15a8[ch] = { T, B }, e1588[ch] = R, dd2d4 = S,
 * d0950 / d2a30 / d19c0 = the hist / comp / lum blocks, e1570 = the weight
 * selectors.  t32_aec_api_get takes the AE lock (false and unlocked when the
 * per-frame AE is not attached); the AE frames wait until t32_aec_api_put.
 */
bool t32_aec_api_get(struct t32_aea_ctx *c)
{
	mutex_lock(&aec_lock);
	if (!aec_ready) {
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

/* the msleep of api_ae_get_exprinfo_attr: the AE frames run meanwhile (they clear R + 1285) */
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

/*
 * Face AE (openimp #686): tisp_ae_face_set -> tisp_ae_sepc_area and
 * tisp_ae_face_get -> tisp_ae_api_get_faceae of the vendor tx-isp-t32.ko on
 * the live AE blocks.  sepc_area (t32_aea_sepc_area, oracle t32_aeae) keeps
 * the enable word R + 1520, the face zone bounds aec.roi (vendor e151c /
 * e1518 / e1524 / e1520) that tisp_ae_weight_mean reads in the next AE frame
 * (t32_aec_algorithm, oracle t32_aechain), the target R + 1296 (10..200,
 * also written back to the caller's block) and the six words R + 1500 ..;
 * its tisp_ae_roi_point (t32_ae_roi_point, oracle t32_aeroi) writes the AE
 * ROI window 0x17090 / 0x17094, the trigger 0x17010 (direct mode, cfgMode 0
 * as in this driver) and the face flag R + 1297.  Channel 0 only (the
 * per-frame AE runs on channel 0); -1 while the AE is not attached.
 */
static void aec_roi_wr(void *priv, u32 reg, u32 val)
{
	(void)priv;
	t32_hw_wr(reg, val);
}

static void aec_roi_msg(void *priv, u32 which, u32 x0, u32 x1, u32 y0, u32 y1)
{
	(void)priv;
	if (which == 0)
		isp_printf(1, "Abnormal input coordinates !!!, x0 is %d, x1 is %d, y0 is %d, y1 is %d \n",
			   (int)x0, (int)x1, (int)y0, (int)y1);
	else if (which == 1)
		isp_printf(1, "The size entered is out of range !!!\n");
	else
		isp_printf(1, "x0 is %d, x1 is %d, y0 is %d, y2 is %d\n", (int)x0, (int)x1, (int)y0, (int)y1);
}

static const struct t32_aea_roi_ops aec_roi_ops = { aec_roi_wr, aec_roi_msg };

static void aec_roi_point(void *priv, u32 ch, u32 x0, u32 y0, u32 x1, u32 y1)
{
	(void)ch;
	t32_ae_roi_point(priv, 0, x0, y0, x1, y1, &aec_roi_ops, NULL);
}

int t32_aec_face_set(u32 ch, void *buf)
{
	struct t32_aea_ctx c;

	if (ch || !t32_aec_api_get(&c))
		return -1;
	t32_aea_sepc_area(&c, 0, buf, &aec.roi[0], &aec.roi[2], &aec.roi[1], &aec.roi[3], aec_roi_point, c.st);
	t32_aec_stats.face_sets++;
	t32_aec_stats.face_en = t32_ld32(c.st, T32_AEA_ST_SEPC);
	t32_aec_stats.face_zone = aec.roi[0] | aec.roi[1] << 8 | aec.roi[2] << 16 | aec.roi[3] << 24;
	t32_aec_stats.face_target = c.st[T32_AEA_ST_SEPC_STR] | c.st[T32_AEA_ST_FACE_ON] << 8;
	t32_aec_api_put();
	return 0;
}

int t32_aec_face_get(u32 ch, void *buf)
{
	struct t32_aea_ctx c;

	if (ch || !t32_aec_api_get(&c))
		return -1;
	t32_aea_get_faceae(&c, buf);
	t32_aec_api_put();
	return 0;
}
