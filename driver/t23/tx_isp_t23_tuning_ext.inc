/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T23 tuning controls of the stock libimp that regtrace_t23_tuning_cid did
 * not know (they failed with -EINVAL).  Each follows the stock
 * apical_isp_core_ops_s_ctrl / g_ctrl handler of libt23-firmware-1.3.0
 * (payload size and layout, the tisp_* helper it calls); the data handling
 * is in tx_isp_t23_tuning_ext.h, host-tested.
 *
 * AE: the stock helpers work on AE globals the lifted AE0 keeps private
 * copies of (oem_*, tx_isp_t23_ae_oem.inc), so these controls need the
 * lifted AE (source_ae_oem=1, the default) and take t23_aelift_mutex like
 * the other AE setters.  With the HLIL AE substitute they fail as before.
 *
 * Returns -ENOIOCTLCMD for an id not handled here.
 */

#include "tx_isp_t23_tuning_ext.h"

#define T23X_CID_AE_ROI         0x08000024U
#define T23X_CID_EXPR           0x08000025U
#define T23X_CID_AE_WEIGHT      0x0800002dU
#define T23X_CID_AE_HIST        0x0800002eU
#define T23X_CID_AE_MIN         0x0800002fU
#define T23X_CID_AE_ZONE        0x08000030U
#define T23X_CID_AE_HIST_ORIGIN 0x08000031U
#define T23X_CID_AE_FREEZE      0x08000034U
#define T23X_CID_AE_ATTR        0x08000035U
#define T23X_CID_AE_STATE       0x08000036U
#define T23X_CID_AE_TARGET_LIST 0x08000038U

/*
 * tisp_ae_trig on the lifted copies: reload flags, then the AE hardware
 * parameters (stock does channel 0 and 1; only AE0 is lifted).  Before the
 * lifted AE ran, its init picks the tables up from tparams.
 */
static void t23x_aelift_trig_locked(void)
{
	uint32_t *flags = (uint32_t *)(void *)oem_IspAeFlag;

	flags[4] = 1;
	flags[5] = 1;
	flags[6] = 1;
	flags[7] = 0;
	if (!t23_aelift_ready)
		return;
	t23_aelift_set_busy(true);
	T23_AELIFT_CALL(LA_tiziano_ae_set_hardware_param, 0,
			(uint32_t)(uintptr_t)oem__ae_parameter, 1, 0);
	t23_aelift_set_busy(false);
}

/* tisp_api_ae_*_weight_set/get: reload flags 4, 5 = 1, 7 = 0 */
static void t23x_aelift_weight_flags_locked(void)
{
	uint32_t *flags = (uint32_t *)(void *)oem_IspAeFlag;

	flags[4] = 1;
	flags[5] = 1;
	flags[7] = 0;
}

/* a 900-byte block in the active bank and the day and night banks */
static void t23x_tparams_put(unsigned int off, const void *src, size_t n)
{
	memcpy(tparams + off, src, n);
	if (tparams_day)
		memcpy((void *)(tparams_day + off - T23X_TP_BANK_BASE), src, n);
	if (tparams_night)
		memcpy((void *)(tparams_night + off - T23X_TP_BANK_BASE), src, n);
}

/* AE_SetROI / SetAeWeight (stock apical_isp_ae_s_roi, ..._zone_weight_s_attr) */
static long t23x_ae_weight_set(bool roi, uint32_t uptr)
{
	uint8_t in[T23X_ZONES];
	uint32_t *w;
	long ret;

	ret = regtrace_t23_copy_in(in, uptr, sizeof(in));
	if (ret)
		return ret;
	w = kmalloc(2U * T23X_ZONES * sizeof(uint32_t), GFP_KERNEL);
	if (!w)
		return -ENOMEM;
	ret = t23x_weight_expand(in, w);
	if (!ret) {
		mutex_lock(&t23_aelift_mutex);
		if (roi) {
			uint32_t *roui = w + T23X_ZONES;

			t23x_roui_from_roi(w, roui);
			t23x_tparams_put(T23X_TP_AE_ROI_WEIGHT, w, 900);
			t23x_tparams_put(T23X_TP_AE_ROUI_WEIGHT, roui, 900);
			memcpy(oem__scene_roi_weight, w, 900);
			memcpy(oem__scene_roui_weight, roui, 900);
		} else {
			t23x_tparams_put(T23X_TP_AE_ZONE_WEIGHT, w, 900);
			memcpy(oem__ae_zone_weight, w, 900);
		}
		t23x_aelift_weight_flags_locked();
		t23x_aelift_trig_locked();
		mutex_unlock(&t23_aelift_mutex);
	}
	kfree(w);
	return ret;
}

static long t23x_ae_weight_get(bool roi, uint32_t uptr)
{
	uint8_t out[T23X_ZONES];

	mutex_lock(&t23_aelift_mutex);
	t23x_weight_pack((const uint32_t *)(void *)(roi ?
			 oem__scene_roi_weight : oem__ae_zone_weight), out);
	t23x_aelift_weight_flags_locked();  /* the stock getter sets them too */
	mutex_unlock(&t23_aelift_mutex);
	return regtrace_t23_copy_out(uptr, out, sizeof(out));
}

/* tisp_ae_manual_set's tail */
static void t23x_ae_ctrls_done_locked(void)
{
	((uint32_t *)(void *)oem_IspAeFlag)[7] = 0;
}

static long t23x_ae_control(bool get, uint32_t id, uint32_t *value)
{
	uint32_t *ctrls = (uint32_t *)(void *)oem_tisp_ae_ctrls;
	uint32_t v = *value;
	long ret = 0;

	switch (id) {
	case T23X_CID_AE_ROI:
	case T23X_CID_AE_WEIGHT:
	case T23X_CID_AE_HIST:
	case T23X_CID_AE_HIST_ORIGIN:
	case T23X_CID_AE_ZONE:
	case T23X_CID_AE_STATE:
	case T23X_CID_AE_TARGET_LIST:
	case T23X_CID_AE_MIN:
	case T23X_CID_AE_ATTR:
	case T23X_CID_AE_FREEZE:
		break;
	case T23X_CID_EXPR:
		if (get)
			return -ENOIOCTLCMD;	/* the GET_EXPR route serves it */
		break;
	default:
		return -ENOIOCTLCMD;
	}
	if (!regtrace_t23_source_ae_oem)
		return -EINVAL;

	switch (id) {
	case T23X_CID_AE_ROI:
	case T23X_CID_AE_WEIGHT:
		return get ? t23x_ae_weight_get(id == T23X_CID_AE_ROI, v) :
			     t23x_ae_weight_set(id == T23X_CID_AE_ROI, v);
	case T23X_CID_AE_HIST: {
		/* stock: 16 bytes in, the thresholds (bytes 0..3) used */
		uint8_t buf[16];

		if (get) {
			mutex_lock(&t23_aelift_mutex);
			t23x_ae_hist_pack(oem_tisp_ae_hist_last, buf);
			mutex_unlock(&t23_aelift_mutex);
			return regtrace_t23_copy_out(v, buf, sizeof(buf));
		}
		if (!v)
			return -EINVAL;	/* stock: NULL fails */
		ret = regtrace_t23_copy_in(buf, v, sizeof(buf));
		if (ret)
			return ret;
		mutex_lock(&t23_aelift_mutex);
		memcpy(oem_tisp_ae_hist_last + T23X_AE_HIST_THRESH, buf, 4);
		mutex_unlock(&t23_aelift_mutex);
		return 0;
	}
	case T23X_CID_AE_HIST_ORIGIN: {
		uint32_t *bins;

		if (!get)
			return -EINVAL;
		bins = kmalloc(T23X_AE_HIST_BINS, GFP_KERNEL);
		if (!bins)
			return -ENOMEM;
		mutex_lock(&t23_aelift_mutex);
		memcpy(bins, oem_tisp_ae_hist_last, T23X_AE_HIST_BINS);
		mutex_unlock(&t23_aelift_mutex);
		ret = regtrace_t23_copy_out(v, bins, T23X_AE_HIST_BINS);
		kfree(bins);
		return ret;
	}
	case T23X_CID_AE_ZONE: {
		uint32_t *zone;

		if (!get)
			return -EINVAL;
		zone = kmalloc(900, GFP_KERNEL);
		if (!zone)
			return -ENOMEM;
		mutex_lock(&t23_aelift_mutex);
		memcpy(zone, oem_IspAeStatic + T23X_AE_ZONE_OFF, 900);
		mutex_unlock(&t23_aelift_mutex);
		ret = regtrace_t23_copy_out(v, zone, 900);
		kfree(zone);
		return ret;
	}
	case T23X_CID_AE_STATE: {
		uint8_t buf[12];

		if (!get)
			return -EINVAL;
		mutex_lock(&t23_aelift_mutex);
		t23x_ae_state_pack((const uint32_t *)(void *)oem__ae_stat, buf);
		mutex_unlock(&t23_aelift_mutex);
		return regtrace_t23_copy_out(v, buf, sizeof(buf));
	}
	case T23X_CID_AE_TARGET_LIST: {
		uint32_t list[10];

		if (get) {
			mutex_lock(&t23_aelift_mutex);
			memcpy(list, oem__at_list, sizeof(list));
			mutex_unlock(&t23_aelift_mutex);
			return regtrace_t23_copy_out(v, list, sizeof(list));
		}
		ret = regtrace_t23_copy_in(list, v, sizeof(list));
		if (ret)
			return ret;
		mutex_lock(&t23_aelift_mutex);
		memcpy(oem__at_list, list, sizeof(list));
		t23x_aelift_weight_flags_locked();
		mutex_unlock(&t23_aelift_mutex);
		return 0;
	}
	case T23X_CID_AE_MIN: {
		uint32_t m[4];

		if (get) {
			const uint32_t *th = (const uint32_t *)(void *)oem_ae_exp_th;

			memset(m, 0, sizeof(m));
			mutex_lock(&t23_aelift_mutex);
			m[0] = th[4];
			m[1] = th[5];
			mutex_unlock(&t23_aelift_mutex);
			return regtrace_t23_copy_out(v, m, sizeof(m));
		}
		if (!v)
			return -EINVAL;	/* stock: NULL fails */
		ret = regtrace_t23_copy_in(m, v, sizeof(m));
		if (ret)
			return ret;
		mutex_lock(&t23_aelift_mutex);
		if (t23x_ae_min_set((uint32_t *)(void *)oem_ae_exp_th,
				    (uint32_t *)(void *)oem_ae_extra_at_list_wdr,
				    m[0], m[1]) != 3U)
			printk(KERN_WARNING
			       "tx_isp_t23_recovered: AE min it=%u again=%u partly out of range (kept)\n",
			       m[0], m[1]);
		t23x_ae_ctrls_done_locked();
		mutex_unlock(&t23_aelift_mutex);
		return 0;
	}
	case T23X_CID_AE_ATTR: {
		uint32_t *blk;

		blk = kmalloc(T23X_AE_CTRLS_WORDS * sizeof(uint32_t), GFP_KERNEL);
		if (!blk)
			return -ENOMEM;
		if (get) {
			mutex_lock(&t23_aelift_mutex);
			memcpy(blk, ctrls, T23X_AE_CTRLS_WORDS * sizeof(uint32_t));
			mutex_unlock(&t23_aelift_mutex);
			ret = regtrace_t23_copy_out(v, blk,
				T23X_AE_CTRLS_WORDS * sizeof(uint32_t));
		} else {
			ret = regtrace_t23_copy_in(blk, v,
				T23X_AE_CTRLS_WORDS * sizeof(uint32_t));
			if (!ret) {
				mutex_lock(&t23_aelift_mutex);
				t23x_ae_manual_set(ctrls, blk);
				t23x_ae_ctrls_done_locked();
				mutex_unlock(&t23_aelift_mutex);
			}
		}
		kfree(blk);
		return ret;
	}
	case T23X_CID_AE_FREEZE:
		if (get)
			return -EINVAL;
		mutex_lock(&t23_aelift_mutex);
		ret = t23x_ae_freeze(ctrls, v);
		if (!ret)
			t23x_ae_ctrls_done_locked();
		mutex_unlock(&t23_aelift_mutex);
		return ret;
	case T23X_CID_EXPR: {
		struct regtrace_t23_ev_info info;
		uint32_t e[3];

		ret = regtrace_t23_copy_in(e, v, sizeof(e));
		if (ret)
			return ret;
		regtrace_t23_ev_info_get(&info);
		mutex_lock(&t23_aelift_mutex);
		ret = t23x_expr_set(ctrls, e[0], e[1], (uint16_t)e[2],
				    info.line_us);
		if (!ret)
			t23x_ae_ctrls_done_locked();
		mutex_unlock(&t23_aelift_mutex);
		return ret;
	}
	default:
		return -ENOIOCTLCMD;
	}
}


/* ---- AWB --------------------------------------------------------------- */

#define T23X_CID_AWB_RGB_COEFFT 0x08000008U
#define T23X_CID_AWB_ZONE       0x0800000bU
#define T23X_CID_AWB_CT         0x0800000dU
#define T23X_CID_AWB_ZONE_WEIGHT 0x08000010U

/* GetAwbZone from the last statistics snapshot of the HLIL AWB */
static long t23x_awb_zone_get(uint32_t uptr)
{
	struct regtrace_t23_awb_hlil_snapshot *snap;
	unsigned long flags;
	uint8_t *out;
	long ret;

	snap = kmalloc(sizeof(*snap) + T23X_AWB_ZONE_BYTES, GFP_KERNEL);
	if (!snap)
		return -ENOMEM;
	out = (uint8_t *)(snap + 1);
	spin_lock_irqsave(&regtrace_t23_awb_hlil_lock, flags);
	memcpy(snap, &regtrace_t23_awb_hlil_snapshot_data, sizeof(*snap));
	spin_unlock_irqrestore(&regtrace_t23_awb_hlil_lock, flags);
	t23x_awb_zone_pack(snap->r, snap->g, snap->b, snap->pixels, out);
	ret = regtrace_t23_copy_out(uptr, out, T23X_AWB_ZONE_BYTES);
	kfree(snap);
	return ret;
}

/* SetAwbZoneWeight (stock isp_tlib_api_s_awb_zone_weight): 225 words to
 * the active, day and night banks and the AWB's zone weight table
 * (_awb_wght, here the HLIL zone weight mesh) */
static long t23x_awb_zone_weight(bool get, uint32_t uptr)
{
	uint32_t *w;
	long ret;

	w = kmalloc(900, GFP_KERNEL);
	if (!w)
		return -ENOMEM;
	if (get) {
		memcpy(w, regtrace_t23_awb_hlil_zone_weight_mesh, 900);
		ret = regtrace_t23_copy_out(uptr, w, 900);
	} else {
		ret = regtrace_t23_copy_in(w, uptr, 900);
		if (!ret) {
			t23x_tparams_put(T23X_TP_AWB_ZONE_WEIGHT, w, 900);
			memcpy(regtrace_t23_awb_hlil_zone_weight_mesh, w, 900);
		}
	}
	kfree(w);
	return ret;
}

/* Awb_Set/GetRgbCoefft (stock tisp_bcsh_s/g_rgb_coefft): three u16 into
 * the BCSH RGB offsets in use, then the BCSH update */
static long t23x_awb_rgb_coefft(bool get, uint32_t uptr)
{
	uint32_t *now = (uint32_t *)(uintptr_t)tisp_BCSH_au32OffsetRGB_now;
	uint16_t c[3];
	long ret;

	if (!regtrace_t23_valid_ptr((uintptr_t)now))
		now = (uint32_t *)(void *)tisp_BCSH_au32OffsetRGB;
	if (get) {
		c[0] = (uint16_t)now[0];
		c[1] = (uint16_t)now[1];
		c[2] = (uint16_t)now[2];
		return regtrace_t23_copy_out(uptr, c, sizeof(c));
	}
	ret = regtrace_t23_copy_in(c, uptr, sizeof(c));
	if (ret)
		return ret;
	now[0] = c[0];
	now[1] = c[1];
	now[2] = c[2];
	if (regtrace_t23_source_bcsh_runtime)
		tiziano_bcsh_update();
	return 0;
}

static long t23x_awb_control(bool get, uint32_t id, uint32_t *value)
{
	uint32_t v = *value;

	switch (id) {
	case T23X_CID_AWB_ZONE:
		return get ? t23x_awb_zone_get(v) : -EINVAL;
	case T23X_CID_AWB_ZONE_WEIGHT:
		return t23x_awb_zone_weight(get, v);
	case T23X_CID_AWB_RGB_COEFFT:
		return t23x_awb_rgb_coefft(get, v);
	case T23X_CID_AWB_CT: {
		uint32_t ct;
		long ret;

		if (get) {
			ct = ACCESS_ONCE(regtrace_t23_source_awb_hlil_ct);
			return regtrace_t23_copy_out(v, &ct, sizeof(ct));
		}
		ret = regtrace_t23_copy_in(&ct, v, sizeof(ct));
		if (ret)
			return ret;
		ACCESS_ONCE(regtrace_t23_awb_custom_ct) = ct;
		return 0;
	}
	default:
		return -ENOIOCTLCMD;
	}
}

/* ---- Gamma ------------------------------------------------------------- */

#define T23X_CID_GAMMA 0x0800002bU
#define T23X_CID_FIXED_CONTRAST 0x08000102U

/*
 * SetGamma (stock tisp_s_Gamma): the 129-point curve goes to the active,
 * day and night banks and tiziano_gamma_lut.  The stock module does not
 * rewrite the LUT registers here; the curve is programmed by the next
 * gamma refresh (day/night or custom-mode switch, stream start).
 */
static long t23x_gamma(bool get, uint32_t uptr)
{
	uint16_t lut[T23X_GAMMA_BYTES / 2U];
	long ret;

	if (get) {
		memcpy(lut, tiziano_gamma_lut, sizeof(lut));
		return regtrace_t23_copy_out(uptr, lut, sizeof(lut));
	}
	ret = regtrace_t23_copy_in(lut, uptr, sizeof(lut));
	if (ret)
		return ret;
	t23x_tparams_put(T23X_TP_GAMMA, lut, sizeof(lut));
	memcpy(tiziano_gamma_lut, lut, sizeof(lut));
	return 0;
}

/* ---- WaitFrame --------------------------------------------------------- */

/*
 * Stock isp_frame_done_wait / isp_frame_done_wakeup: the core frame-done
 * interrupt (status bit 0) counts frames and wakes the waiters; a waiter
 * clears the condition and waits for the next frame-done, the timeout is
 * passed to the scheduler as is (jiffies).
 */
static DECLARE_WAIT_QUEUE_HEAD(t23x_frame_wq);
static DEFINE_SPINLOCK(t23x_frame_lock);
static uint64_t t23x_frame_cnt;
static int t23x_frame_cond;

static void t23x_frame_done_wakeup(void)
{
	unsigned long flags;

	spin_lock_irqsave(&t23x_frame_lock, flags);
	t23x_frame_cnt++;
	t23x_frame_cond = 1;
	spin_unlock_irqrestore(&t23x_frame_lock, flags);
	wake_up_interruptible(&t23x_frame_wq);
}

static long t23x_wait_frame(uint32_t uptr)
{
	uint32_t blk[T23X_WAIT_FRAME_BYTES / 4U];
	unsigned long flags;
	uint64_t cnt;
	long wret;
	long ret;
	int cond;

	ret = regtrace_t23_copy_in(blk, uptr, sizeof(blk));
	if (ret)
		return ret;
	spin_lock_irqsave(&t23x_frame_lock, flags);
	t23x_frame_cond = 0;
	spin_unlock_irqrestore(&t23x_frame_lock, flags);
	/* a value beyond LONG_MAX (negative as long) would make the
	 * scheduler complain; it waits without a limit instead */
	wret = wait_event_interruptible_timeout(t23x_frame_wq,
						ACCESS_ONCE(t23x_frame_cond),
						blk[0] > (uint32_t)LONG_MAX ?
						MAX_SCHEDULE_TIMEOUT :
						(long)blk[0]);
	spin_lock_irqsave(&t23x_frame_lock, flags);
	cnt = t23x_frame_cnt;
	cond = t23x_frame_cond;
	spin_unlock_irqrestore(&t23x_frame_lock, flags);
	ret = t23x_wait_frame_result(wret, cond);
	memcpy(&blk[2], &cnt, sizeof(cnt));
	if (regtrace_t23_copy_out(uptr, blk, sizeof(blk)))
		return -EFAULT;
	return ret;
}

static long regtrace_t23_tuning_ext(bool get, uint32_t id, uint32_t *value)
{
	long ret;

	ret = t23x_ae_control(get, id, value);
	if (ret != -ENOIOCTLCMD)
		return ret;
	ret = t23x_awb_control(get, id, value);
	if (ret != -ENOIOCTLCMD)
		return ret;
	switch (id) {
	case T23X_CID_GAMMA:
		return t23x_gamma(get, *value);
	case T23X_CID_FIXED_CONTRAST: {
		/* stock tisp_set_bcsh_fixed_contrast: 3 bytes {mode, y low,
		 * y high} -> tisp_bcsh_set_mjpeg_contrast + BCSH update */
		uint8_t fc[3];
		long ret2;

		if (get)
			return -EINVAL;
		ret2 = regtrace_t23_copy_in(fc, *value, sizeof(fc));
		if (ret2)
			return ret2;
		tisp_bcsh_set_mjpeg_contrast(0, fc[0], fc[1], fc[2]);
		return 0;
	}
	case T23X_CID_WAIT_FRAME:
		return get ? t23x_wait_frame(*value) : -EINVAL;
	default:
		return -ENOIOCTLCMD;
	}
}
