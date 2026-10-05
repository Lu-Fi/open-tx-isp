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

static long regtrace_t23_tuning_ext(bool get, uint32_t id, uint32_t *value)
{
	long ret;

	ret = t23x_ae_control(get, id, value);
	if (ret != -ENOIOCTLCMD)
		return ret;
	return -ENOIOCTLCMD;
}
