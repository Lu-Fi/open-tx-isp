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
 * t32_awbc_tgain).  Event 6 (the long EV) goes to the AWB on a change
 * (tisp_awb_ev_update, t32_awbc_ev); its other users (gamma / CCM / defog /
 * BCSH / TMO / WDR) and event 5 (again) are not wired yet.
 *
 * The vendor runs the interrupt part in the IRQ and the process in its event
 * thread; here both run in one work item (the sensor calls sleep on I2C).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_aechain.h"

int isp_printf(unsigned int level, const char *fmt, ...);

struct t32_aec_drv_stats t32_aec_stats;

static struct t32_aec aec;
static DEFINE_MUTEX(aec_lock);
static bool aec_ready;
static u32 aec_it, aec_ag, aec_sent;
static u32 aec_tgain;
static bool aec_tgain_valid;
static u64 aec_last_ev;		/* vendor tisp_long_ev_update's last EV (dd678) */
static u32 aec_last_tg;		/* vendor tisp_long_tgain_update's last low gain word */

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
 * 64-bit value = ev[4] | ev[5] << 32): event 6 -> tisp_long_ev_update, whose
 * AWB part runs on an EV change; event 4 -> tisp_long_tgain_update, whose
 * AWB part runs when the low gain word changed or the high one is set.
 */
static void drv_event(u32 ch, const u32 *ev)
{
	u64 v = ev[4] | (u64)ev[5] << 32;

	(void)ch;
	t32_aec_stats.events++;
	if (ev[2] == 6 && v != aec_last_ev) {
		aec_last_ev = v;
		t32_awbc_ev(ev[4]);	/* tisp_awb_ev_update */
	}
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

	mutex_lock(&aec_lock);
	kfree(aec.hist); kfree(aec.comp); kfree(aec.lum);
	memset(&aec, 0, sizeof(aec));
	aec.b = *b;
	aec.S = S;
	aec.zones = zones;
	aec.dcw = 1;		/* one pipeline, as t32_pf_attach_ae passes to t32_ae_ctx_init */
	aec.ops = &drv_ops;
	aec.hist = kzalloc(T32_AEC_HIST_BYTES, GFP_KERNEL);
	aec.comp = kzalloc(T32_AEC_COMP_BYTES, GFP_KERNEL);
	aec.lum = kzalloc(T32_AEC_HIST_BYTES, GFP_KERNEL);
	aec_it = aec_ag = 0;
	aec_sent = ~0U;
	aec_tgain_valid = false;
	aec_last_ev = 0;
	aec_last_tg = 0;
	memset(&t32_aec_stats, 0, sizeof(t32_aec_stats));
	if (!zones || !aec.hist || !aec.comp || !aec.lum)
		ret = -ENOMEM;
	WRITE_ONCE(aec_ready, !ret);
	mutex_unlock(&aec_lock);
	return ret;
}

void t32_aec_detach(void)
{
	WRITE_ONCE(aec_ready, false);
	cancel_work_sync(&aec_work);
	mutex_lock(&aec_lock);
	kfree(aec.hist); kfree(aec.comp); kfree(aec.lum);
	memset(&aec, 0, sizeof(aec));
	aec_tgain_valid = false;
	mutex_unlock(&aec_lock);
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
