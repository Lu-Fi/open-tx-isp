/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T21 tuning controls the stock (lifted) dispatcher accepts but ignores
 * (beyond vendor).  Included once by tx_isp_t21_recovered.c, after the
 * tuning-array declarations; the hooks run from the hand-written glue,
 * the lifted code (tx_isp_t21_adr_oem.inc) is unchanged.
 *
 * Stock oem-t21.ko apical_isp_core_ops_s_ctrl returns 0 without any
 * effect for these CIDs and its g_ctrl leaves the value untouched:
 *
 *   0x009a091a V4L2_CID_SCENE_MODE  SetSceneMode (S_CTRL)
 *   0x0098091f V4L2_CID_COLORFX     SetColorfxMode (S_CTRL)
 *   0x0800002c SinterDnsAttr        112-byte block (tuning ioctl, pointer)
 *   0x08000167 Temper type          TemperDnsAttr/Ctl (S_CTRL)
 *   0x08000083 Temper strength      TemperDnsAttr (tuning ioctl, value)
 *   0x08000082 Temper strength      TemperDnsCtl (tuning ioctl, value)
 *   0x08000025 Expr (get)           IMPISPExpr, 12 bytes (tuning ioctl, pointer)
 *   0x08000062 DPC strength         SetDPC_Strength (tuning ioctl, value)
 *   0x080000a6 CSC attribute        SetCsc_Attr, 64 bytes (tuning ioctl, pointer)
 *   0x080000e3 Front crop           SetFrontCrop, 20 bytes (tuning ioctl, pointer)
 *
 * Here they act on existing T21 mechanisms:
 *
 *  - Scene: stored and reported, nothing applied (the T20 3.12.0 driver
 *    returns before applying its scene mode too).
 *  - Colorfx: AUTO, BW (CCM saturation list 0), SEPIA (BW plus a tinted
 *    CSC matrix, see t21_sepia_params), VIVID (saturation list x1.5) and
 *    NEGATIVE (inverted gamma LUT).  The other V4L2 effects are refused
 *    (-1) and the effect in use stays.
 *  - Sinter (2D NR): every SDNS noise-profile field (npv * stren, as
 *    tisp_sdns_y/c_param_cfg write them) is scaled by strength/128
 *    (MANUAL; AUTO = 128), at most up to the 8-bit field limit.  The
 *    stren factors themselves are small integers (0..8 in the jxf23
 *    tuning), too coarse to scale.
 *  - IntegrationTime (same 0x800002c block, [0x52] RANGE): u16 [0x1a]
 *    becomes the AE exposure ceiling (tisp_ae_ctrls max_integration_time,
 *    re-seeded from the sensor by tisp_set_fps), as the T20 3.12.0 AE
 *    caps at stab.global_max_integration_time.  MANUAL fixes the
 *    exposure at u16 [0x18] lines through the stock manual-IT flag
 *    (tisp_ae_ctrls 0x34, what SetExpr MANUAL sets; the AE then only moves
 *    the gain), AUTO clears it.  The get side fills [0x03] (manual),
 *    u16 [0x18] (integration time in use) and u16 [0x1a] (AE ceiling), as
 *    the T20 3.12.0 libimp reads them back; the stock get left the block
 *    untouched, so GetIntegrationTime read mode 0 / it 0 / max 0.
 *  - WaitFrame (0x8000162, get, 0x18-byte isp_frame_done_info): not in the
 *    stock dispatcher (acknowledged, count never written).  A 64-bit
 *    frame-done count is ticked by the VIC frame-done interrupt; the call
 *    waits for the next tick (timeout in ms in word 0) and returns the
 *    count in words 2-3, also on a timeout (-ETIMEDOUT), as the T20 SDK.
 *  - Temper (3D NR): MANUAL applies the strength through the stock
 *    0x8000085 path, tisp_s_3dns_ratio (128 = tuning file), AUTO puts the
 *    ratio last set through 0x8000085 back (128 if none), DISABLE applies 0.
 *  - CSC and front crop: see ../common/tx_isp_csc.h (T31 presets on the
 *    0x1700 block; front crop scales + crops each channel that fits).
 *
 * Nothing changes until one of these controls is set: every hook is a
 * no-op in the default state (colorfx AUTO, sinter ratio 128, temper
 * ratio never applied).
 */
#ifndef TX_ISP_T21_TUNING_CTL_H
#define TX_ISP_T21_TUNING_CTL_H

#include "../common/tx_isp_csc.h"

#define T21_CID_SCENE_MODE	0x009a091a
#define T21_CID_COLORFX		0x0098091f
#define T21_CID_SINTER_DNS	0x0800002c
#define T21_CID_TEMPER_TYPE	0x08000167
#define T21_CID_TEMPER_ATTR	0x08000083
#define T21_CID_TEMPER_CTL	0x08000082
#define T21_CID_3DNS_RATIO	0x08000085
#define T21_CID_EXPR		0x08000025	/* GetExpr, get side only */
#define T21_CID_DPC_RATIO	0x08000062

#define T21_COLORFX_AUTO	0
#define T21_COLORFX_BW		1
#define T21_COLORFX_SEPIA	2
#define T21_COLORFX_NEGATIVE	3
#define T21_COLORFX_VIVID	9

#define T21_TEMPER_DISABLE	0
#define T21_TEMPER_AUTO		1
#define T21_TEMPER_MANUAL	2

/* IMPISPSinterDenoiseAttr as the T21 1.0.33 libimp sends it on 0x800002c:
 * a zeroed 0x70-byte block, [70] = 1, MANUAL also [10] = [98] = 1 and
 * [43] = strength.  The same CID carries the legacy integration-time
 * block ([0x3f]/[0x52] set, never [70]) and MoveState (a plain 0/1). */
#define T21_SINTER_BLOCK	0x70
#define T21_SINTER_B_MANUAL	10
#define T21_SINTER_B_STRENGTH	43
#define T21_SINTER_B_VALID	70
#define T21_SINTER_B_MANUAL2	98
#define T21_IT_B_AUTO		0x3f
#define T21_IT_B_RANGE		0x52
#define T21_IT_B_MAX		0x1a	/* u16, RANGE max_integration_time */
#define T21_IT_B_MANUAL		0x03	/* manual flag (set and get) */
#define T21_IT_B_TIME		0x18	/* u16, MANUAL integration_time */
#define T21_IT_B_MANUAL2	0x51
#define T21_CID_WAIT_FRAME	0x08000162	/* IMP_ISP_Tuning_WaitFrame */
#define T21_WAIT_FRAME_BYTES	0x18		/* isp_frame_done_info */

#define T21_IT_NONE		(-1)
#define T21_IT_AUTO		0
#define T21_IT_MANUAL		1
#define T21_IT_RANGE		2

static uint32_t t21_scene_mode;			/* V4L2 scene, 0 = auto */
static uint32_t t21_colorfx = T21_COLORFX_AUTO;
static uint32_t t21_colorfx_sat_base[9];	/* list before the effect */
static uint32_t t21_sinter_type;		/* 0 auto, 1 manual */
static uint32_t t21_sinter_strength = 128;
static uint32_t t21_sinter_ratio = 128;		/* applied, 128 = identity */
static uint32_t t21_temper_type = T21_TEMPER_AUTO;
static uint32_t t21_temper_strength = 128;
static uint32_t t21_3dns_auto_ratio = 128;	/* last 0x8000085 value */
static uint32_t t21_3dns_applied = 128;		/* ratio in the mdns banks */

/* ---- pure helpers (host-testable) ------------------------------------ */

static inline int t21_colorfx_scales_sat(uint32_t fx)
{
	return fx == T21_COLORFX_BW || fx == T21_COLORFX_SEPIA ||
	       fx == T21_COLORFX_VIVID;
}

/*
 * SEPIA = BW (CCM saturation 0, so the CSC sees R = G = B = g) plus a
 * tinted CSC matrix.  The block takes magnitudes only, the signs are fixed
 * (U row --+, V row +--, see tx_isp_csc.h), so the offset of a grey input
 * is set by the row sums: U row = 512 - T (U = 128 - g*T/1024) and V row
 * = 512 + T (V = 128 + g*T/1024).  The Y row stays the active preset's
 * (BT601 full: 1024 in total, luma unchanged).  T = 80 gives U about 20
 * below and V about 20 above neutral at white, 10 at mid grey.
 */
#define T21_SEPIA_TINT		80

/* The CSC parameter words with the sepia tint on U and V (p: 15 words as
 * tx_isp_csc_params, in preset 0 form: rows 3/4/5 = U -a -b +c, 6/7/8 =
 * V +d -e -f).  The tint comes from the third U and the first V entry. */
static inline void t21_sepia_params(const int32_t *in, int32_t *out)
{
	int i;

	for (i = 0; i < 15; i++)
		out[i] = in[i];
	/* U: c = a + b - T, V: d = e + f + T (magnitudes, clamped to 10 bit) */
	out[5] = (in[3] < 0 ? -in[3] : in[3]) + (in[4] < 0 ? -in[4] : in[4]) -
		 T21_SEPIA_TINT;
	out[6] = (in[7] < 0 ? -in[7] : in[7]) + (in[8] < 0 ? -in[8] : in[8]) +
		 T21_SEPIA_TINT;
	if (out[5] < 0)
		out[5] = 0;
	if (out[6] > 0x3ff)
		out[6] = 0x3ff;
}

/* The CCM saturation list (per EV, 256 = 1.0 in cm_control) for an effect. */
static inline void t21_colorfx_sat(uint32_t fx, const uint32_t *in,
				   uint32_t *out)
{
	int i;

	for (i = 0; i < 9; i++) {
		if (fx == T21_COLORFX_BW || fx == T21_COLORFX_SEPIA)
			out[i] = 0;
		else if (fx == T21_COLORFX_VIVID)
			out[i] = in[i] + (in[i] >> 1);
		else
			out[i] = in[i];
	}
}

/* One gamma register: two 12-bit LUT points, inverted for NEGATIVE. */
static inline uint32_t t21_gamma_word(uint32_t lo, uint32_t hi, uint32_t fx)
{
	if (fx == T21_COLORFX_NEGATIVE) {
		lo = 0xfff - (lo & 0xfff);
		hi = 0xfff - (hi & 0xfff);
	}
	return (hi << 12) | lo;
}

/*
 * One SDNS register field (npv * stren, 8 bits, written unmasked) scaled
 * by ratio/128.  A raise stops at 255 (or at the stock value, if that is
 * already larger); 128 returns the stock value unchanged.
 */
static inline uint32_t t21_sinter_px(uint32_t v, uint32_t ratio)
{
	uint32_t s, cap;

	if (ratio == 128)
		return v;
	s = (v * ratio + 64) >> 7;
	if (s <= v)
		return s;
	cap = v > 255 ? v : 255;
	return s > cap ? cap : s;
}

/*
 * Legacy integration-time block on 0x800002c (stock T20 3.12.0 / T21
 * 1.0.33 libimp, OpenIMP IMP_ISP_Tuning_SetIntegrationTime): which mode a
 * set block carries.  A sinter block ([70] set), MoveState's plain value
 * and an empty block are not one (T21_IT_NONE).  MANUAL also carries the
 * AUTO byte, so it is checked first.
 */
static inline int t21_it_block_mode(const uint8_t *blk)
{
	if (blk[T21_SINTER_B_VALID])
		return T21_IT_NONE;
	if (blk[T21_IT_B_MANUAL])
		return T21_IT_MANUAL;
	if (blk[T21_IT_B_RANGE])
		return T21_IT_RANGE;
	if (blk[T21_IT_B_AUTO])
		return T21_IT_AUTO;
	return T21_IT_NONE;
}

static inline uint32_t t21_it_block_u16(const uint8_t *blk, unsigned int off)
{
	return (uint32_t)blk[off] | ((uint32_t)blk[off + 1] << 8);
}

/* Get side: the bytes the legacy GetIntegrationTime reads ([0x03] manual,
 * u16 [0x18] time, u16 [0x1a] maximum); all other bytes stay as given. */
static inline void t21_it_block_fill(uint8_t *blk, int manual, uint32_t it,
				     uint32_t max)
{
	if (it > 0xffff)
		it = 0xffff;
	if (max > 0xffff)
		max = 0xffff;
	blk[T21_IT_B_MANUAL] = manual ? 1 : 0;
	blk[T21_IT_B_TIME] = (uint8_t)it;
	blk[T21_IT_B_TIME + 1] = (uint8_t)(it >> 8);
	blk[T21_IT_B_MAX] = (uint8_t)max;
	blk[T21_IT_B_MAX + 1] = (uint8_t)(max >> 8);
}

#ifndef T21_TUNING_CTL_HOST_TEST

/* ---- hooks called from the hand-written T21 code ------------------- */

/*
 * WaitFrame.  The count only ever grows (64 bit, written from the VIC
 * interrupt only); a waiter sleeps until it differs from the value it
 * saw on entry, so a frame between two calls is never lost and two
 * waiters cannot steal each other's wake-up.
 */
static atomic64_t t21_frame_done_cnt = ATOMIC64_INIT(0);
static DECLARE_WAIT_QUEUE_HEAD(t21_frame_done_wq);

static void t21_frame_done_wakeup(void)
{
	atomic64_inc(&t21_frame_done_cnt);
	wake_up_all(&t21_frame_done_wq);
}

static int t21_wait_frame(void __user *uptr)
{
	uint32_t info[T21_WAIT_FRAME_BYTES / 4];
	unsigned long jif;
	u64 start, cnt;
	long w;
	int ret;

	if (private_copy_from_user(info, uptr, sizeof(info)))
		return -EFAULT;
	if ((int32_t)info[0] < 0)
		return -EINVAL;
	jif = msecs_to_jiffies(info[0]);
	if (jif > MAX_SCHEDULE_TIMEOUT - 1)
		jif = MAX_SCHEDULE_TIMEOUT - 1;
	start = atomic64_read(&t21_frame_done_cnt);
	w = wait_event_interruptible_timeout(t21_frame_done_wq,
		atomic64_read(&t21_frame_done_cnt) != start, jif);
	if (w == -ERESTARTSYS)
		return w;
	ret = w ? 0 : -ETIMEDOUT;
	cnt = atomic64_read(&t21_frame_done_cnt);
	info[2] = (uint32_t)cnt;
	info[3] = (uint32_t)(cnt >> 32);
	if (private_copy_to_user(uptr, info, sizeof(info)))
		return -EFAULT;
	return ret;
}

/* tiziano_ccm_params_refresh just loaded cm_sat_list from tparams. */
static void t21_colorfx_after_ccm_refresh(void)
{
	if (!t21_colorfx_scales_sat(t21_colorfx))
		return;
	memcpy(t21_colorfx_sat_base, cm_sat_list, sizeof(t21_colorfx_sat_base));
	t21_colorfx_sat(t21_colorfx, t21_colorfx_sat_base,
			(uint32_t *)cm_sat_list);
}

/* tisp_set_saturation computed a new list: the effect goes on top. */
static const void *t21_colorfx_saturation(const int32_t *eff,
					  uint32_t *scratch)
{
	if (!t21_colorfx_scales_sat(t21_colorfx))
		return eff;
	memcpy(t21_colorfx_sat_base, eff, sizeof(t21_colorfx_sat_base));
	t21_colorfx_sat(t21_colorfx, t21_colorfx_sat_base, scratch);
	return scratch;
}

static void t21_csc_apply_matrix(void);

static int t21_colorfx_set(uint32_t fx)
{
	uint32_t old = t21_colorfx;
	uint32_t list[9];
	int32_t size = sizeof(list);

	if (fx != T21_COLORFX_AUTO && fx != T21_COLORFX_BW &&
	    fx != T21_COLORFX_SEPIA && fx != T21_COLORFX_NEGATIVE &&
	    fx != T21_COLORFX_VIVID)
		return -1;
	if (fx == old)
		return 0;

	if (!t21_colorfx_scales_sat(old))
		memcpy(t21_colorfx_sat_base, cm_sat_list,
		       sizeof(t21_colorfx_sat_base));
	t21_colorfx = fx;
	if (t21_colorfx_scales_sat(fx) || t21_colorfx_scales_sat(old)) {
		t21_colorfx_sat(fx, t21_colorfx_sat_base, list);
		tisp_ccm_param_array_set(0x81, list, &size);
	}
	if (fx == T21_COLORFX_NEGATIVE || old == T21_COLORFX_NEGATIVE)
		tiziano_gamma_lut_parameter();
	if (fx == T21_COLORFX_SEPIA || old == T21_COLORFX_SEPIA)
		t21_csc_apply_matrix();	/* tinted matrix on / user matrix back */
	return 0;
}

static void t21_sinter_set_ratio(uint32_t ratio)
{
	if (ratio == t21_sinter_ratio)
		return;
	t21_sinter_ratio = ratio;
	/* Re-interpolate at the gain in use, like the stock day/night
	 * refresh; before the first refresh the next frame does it. */
	if (sdns_gain_old != 0xffffffffU)
		tisp_sdns_intp_reg_refresh(sdns_gain_old);
}

static void t21_3dns_apply(uint32_t ratio)
{
	if (ratio > 255 || ratio == t21_3dns_applied)
		return;
	tisp_s_3dns_ratio(ratio);
	t21_3dns_applied = ratio;
}

static void t21_temper_update(void)
{
	switch (t21_temper_type) {
	case T21_TEMPER_MANUAL:
		t21_3dns_apply(t21_temper_strength);
		break;
	case T21_TEMPER_DISABLE:
		t21_3dns_apply(0);
		break;
	default:
		t21_3dns_apply(t21_3dns_auto_ratio);
		break;
	}
}

/*
 * SetIntegrationTime(MODE_RANGE): the AE exposure ceiling.  Stock
 * oem-t21.ko ignores the block (0x800002c is a no-op), so the cap read
 * back but had no effect.  T21 already has the field the T20 3.12.0 AE
 * caps at (stab.global_max_integration_time there): tisp_ae_ctrls
 * max_integration_time, seeded from the sensor by tisp_set_fps (ISP
 * start and every fps change, like the T20 SYNC_VIDEO_IN re-seed) and
 * copied into _exp_parameter by tisp_ae_ctrls_update each frame, which
 * also clamps it to the sensor maximum.  Never below the sensor minimum.
 */
static void t21_ae_set_it_max(uint32_t lines)
{
	const struct t21_sensor_ctrl_view *sctrl =
		(const struct t21_sensor_ctrl_view *)sensor_ctrl;
	struct t21_ae_ctrls_view *ae = (struct t21_ae_ctrls_view *)tisp_ae_ctrls;
	uint32_t lo = sctrl->min_integration_time ? sctrl->min_integration_time : 1;
	uint32_t hi = sctrl->max_integration_time;

	if (lines < lo)
		lines = lo;
	if (hi && lines > hi)
		lines = hi;
	ACCESS_ONCE(ae->max_integration_time) = lines;
}

/*
 * SetIntegrationTime(MODE_MANUAL / MODE_AUTO).  MANUAL does what the stock
 * tisp_ae_manual_set does for SetExpr MANUAL: the lines go to
 * tisp_ae_ctrls 0x0c and the manual-IT flag (0x34) is set, so
 * tisp_set_sensor_integration_time writes them instead of the AE's value
 * and the AE keeps only the gain; trig_cal = 0 as there.  Clamped to the
 * sensor's limits (the libimp already rejects 256 lines or more).  AUTO
 * clears the flag, the AE takes the exposure back with its next frame.
 */
static void t21_ae_set_it_manual(uint32_t lines)
{
	const struct t21_sensor_ctrl_view *sctrl =
		(const struct t21_sensor_ctrl_view *)sensor_ctrl;
	struct t21_ae_ctrls_view *ae = (struct t21_ae_ctrls_view *)tisp_ae_ctrls;
	uint32_t lo = sctrl->min_integration_time ? sctrl->min_integration_time : 1;
	uint32_t hi = sctrl->max_integration_time;

	if (lines < lo)
		lines = lo;
	if (hi && lines > hi)
		lines = hi;
	ACCESS_ONCE(ae->integration_time) = lines;
	ACCESS_ONCE(ae->it_manual) = 1;
	*(volatile uint32_t *)trig_cal = 0;
}

static void t21_ae_set_it_auto(void)
{
	struct t21_ae_ctrls_view *ae = (struct t21_ae_ctrls_view *)tisp_ae_ctrls;

	ACCESS_ONCE(ae->it_manual) = 0;
}

/*
 * Set side.  Returns 1 when the control was handled here (*ret holds the
 * result), 0 to pass it on to the stock dispatcher.
 */
/*
 * DPC strength (0x8000062, value 0..255, 128 = tuning file).  Stock T21
 * has the DPC block (tiziano_dpc, regs 0x204-0x23c) but no strength
 * control.  The OEM T23/T31 tisp_s_dpc_str_internal scales the m1/m3
 * detection thresholds; T21's equivalents are the m1 level-0 thresholds:
 * up to 128 the "f" threshold scales linearly (at least 5) and the "d"
 * threshold moves towards 1000, above 128 "f" moves towards 1200 and "d"
 * towards 5.  Applied to the gain-interpolated values (the mapping is
 * affine, so this equals scaling the bank arrays), every refresh.
 */
static uint32_t t21_dpc_ratio = 128;

static void t21_dpc_scale_intp(void)
{
	int32_t s = (int32_t)t21_dpc_ratio;
	int32_t f = (int32_t)dpc_d_m1_l0_fthres_intp;
	int32_t d = (int32_t)dpc_d_m1_l0_dthres_intp;

	if (s == 128)
		return;
	if (s < 128) {
		f = (f * s) >> 7;
		if (f < 5)
			f = 5;
		d = 1000 + (((d - 1000) * s) >> 7);
	} else {
		f += ((1200 - f) * (s - 128)) >> 7;
		d = ((256 - s) * d + 5 * (s - 128)) >> 7;
	}
	dpc_d_m1_l0_fthres_intp = (uintptr_t)(f < 0 ? 0 : f);
	dpc_d_m1_l0_dthres_intp = (uintptr_t)(d < 0 ? 0 : d);
}

static void t21_dpc_set_ratio(uint32_t ratio)
{
	t21_dpc_ratio = ratio;
	/* re-interpolate at the gain in use (as the day/night refresh) */
	if (dpc_gain_old != 0xffffffffU)
		tisp_dpc_intp_reg_refresh(dpc_gain_old);
}

/*
 * CSC presets (0x80000a6, tx_isp_csc.h).  The block at 0x1700 is the T31
 * 0x6000 one; tisp_init programs preset 0 and calls t21_csc_reset().
 * 0x1730 (clip) is shared with the day/night switch in the ISR, which
 * zeroes the chroma range for night and restores it for day: the ISR now
 * restores t21_csc_clip instead of the fixed preset 0 word, and a CSC set
 * at night leaves the mono clip alone (applied on the next day restore).
 * 0x1704 and the low byte of 0x1720 also carry the contrast
 * (tisp_set_contrast); a non-neutral contrast is put back on top.
 */
static DEFINE_SPINLOCK(t21_csc_lock);
static uint32_t t21_csc_attr[TX_ISP_CSC_ATTR_WORDS];
static uint32_t t21_csc_clip = 0xff00ff00;	/* day clip word, 0x1730 */
static int t21_csc_night;

static void t21_csc_reset(void)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&t21_csc_lock, flags);
	t21_csc_attr[0] = 0;
	for (i = 0; i < TX_ISP_CSC_WORDS; i++)
		t21_csc_attr[1 + i] = (uint32_t)tx_isp_csc_presets[0][i];
	t21_csc_clip = 0xff00ff00;
	t21_csc_night = 0;
	spin_unlock_irqrestore(&t21_csc_lock, flags);
	if (t21_colorfx == T21_COLORFX_SEPIA)
		t21_csc_apply_matrix();	/* tisp_init wrote the plain preset 0 */
}

/* ISR: running-mode switch.  night: 1 = going to night (mono clip). */
static void t21_csc_isr_mode(int night)
{
	spin_lock(&t21_csc_lock);
	t21_csc_night = night;
	system_reg_write(0x1730, night ? tx_isp_csc_tiziano_mono_clip(t21_csc_clip)
				       : t21_csc_clip);
	spin_unlock(&t21_csc_lock);
}

/* Matrix registers 0x1710..0x1718 from the stored CSC attribute, with the
 * sepia tint while that effect is on.  Takes the lock itself. */
static void t21_csc_apply_matrix(void)
{
	int32_t p[TX_ISP_CSC_WORDS], q[TX_ISP_CSC_WORDS];
	uint32_t reg[5];
	unsigned long flags;
	int i;

	/* tisp_init has not run yet: tisp_init writes preset 0 itself */
	if (!((uint32_t *)tispinfo)[0])
		return;
	spin_lock_irqsave(&t21_csc_lock, flags);
	for (i = 0; i < TX_ISP_CSC_WORDS; i++)
		p[i] = (int32_t)t21_csc_attr[1 + i];
	if (t21_colorfx == T21_COLORFX_SEPIA) {
		t21_sepia_params(p, q);
		tx_isp_csc_tiziano_regs(q, reg);
	} else {
		tx_isp_csc_tiziano_regs(p, reg);
	}
	system_reg_write(0x1710, reg[0]);
	system_reg_write(0x1714, reg[1]);
	system_reg_write(0x1718, reg[2]);
	spin_unlock_irqrestore(&t21_csc_lock, flags);
}

static int t21_csc_set(const void __user *uptr)
{
	uint32_t attr[TX_ISP_CSC_ATTR_WORDS];
	int32_t p[TX_ISP_CSC_WORDS];
	uint32_t reg[5];
	unsigned long flags;
	int ret, i;

	if (private_copy_from_user(attr, uptr, sizeof(attr)))
		return -EFAULT;
	ret = tx_isp_csc_check(attr);
	if (ret)
		return ret;
	/* tisp_init has not run yet (it sets the sensor width): no core */
	if (!((uint32_t *)tispinfo)[0])
		return -EBUSY;
	tx_isp_csc_params(attr, p);
	tx_isp_csc_tiziano_regs(p, reg);

	spin_lock_irqsave(&t21_csc_lock, flags);
	system_reg_write(0x1700, 0x1f);
	system_reg_write(0x1704, 0);
	system_reg_write(0x1710, reg[0]);
	system_reg_write(0x1714, reg[1]);
	system_reg_write(0x1718, reg[2]);
	system_reg_write(0x1720, reg[3]);
	t21_csc_clip = reg[4];
	if (!t21_csc_night)
		system_reg_write(0x1730, reg[4]);
	t21_csc_attr[0] = attr[0];
	for (i = 0; i < TX_ISP_CSC_WORDS; i++)
		t21_csc_attr[1 + i] = (uint32_t)p[i];
	spin_unlock_irqrestore(&t21_csc_lock, flags);

	if (t21_colorfx == T21_COLORFX_SEPIA)
		t21_csc_apply_matrix();	/* the tint stays on top of the new matrix */

	if (custom_eff[2] != 0x80)
		tisp_set_contrast(custom_eff[2]);
	return 0;
}

static int t21_csc_get(void __user *uptr)
{
	uint32_t attr[TX_ISP_CSC_ATTR_WORDS];
	unsigned long flags;

	spin_lock_irqsave(&t21_csc_lock, flags);
	memcpy(attr, t21_csc_attr, sizeof(attr));
	spin_unlock_irqrestore(&t21_csc_lock, flags);
	return private_copy_to_user(uptr, attr, sizeof(attr)) ? -EFAULT : 0;
}

/*
 * Front crop / ePTZ (0x80000e3, tx_isp_csc.h).  Each configured channel
 * whose output fits into the window scales the full frame by out/window
 * and crops the window's image (registers of tisp_channel_attr_set());
 * the others keep their own configuration.  Live changes go through the
 * stock channel stop/save -> write -> restore sequence of the OEM
 * crop/scaler update (0x3000009).  A channel configured later
 * (ispcore_frame_channel s_fmt) picks the window up in t21_fcrop_s_fmt().
 */
#define T21_FCROP_CHANNELS	3

static DEFINE_MUTEX(t21_fcrop_mutex);
static uint32_t t21_fcrop_win[TX_ISP_FCROP_WORDS];	/* [0] = 0: off */

static uint32_t t21_sensor_w(void)
{
	return ((uint32_t *)tispinfo)[0];
}

static uint32_t t21_sensor_h(void)
{
	return ((uint32_t *)tispinfo)[1];
}

static struct t21_ispcore_channel_abi *t21_fcrop_channel(unsigned int i)
{
	struct t21_ispcore_irq_view *core = (void *)ispcore_sd;
	struct t21_ispcore_irq_view *rt;

	if (!t21_isp_valid_ptr(core))
		return NULL;
	rt = core->runtime;
	if (!t21_isp_valid_ptr(rt) || !t21_isp_valid_ptr(rt->channels))
		return NULL;
	return (struct t21_ispcore_channel_abi *)(rt->channels + i * 0xa0);
}

/* Scaler output of a configured channel, as s_fmt programs it. */
static int t21_fcrop_chan_out(const struct t21_frame_format_view *fmt,
			      uint32_t *ow, uint32_t *oh)
{
	if (!fmt->width || !fmt->height)
		return 0;	/* not configured (stream off clears it) */
	*ow = fmt->scaler_enable ? fmt->scaler_width : t21_sensor_w();
	*oh = fmt->scaler_enable ? fmt->scaler_height : t21_sensor_h();
	return *ow && *oh;
}

/*
 * Program channel i for window win (NULL / full = its own config).
 * Returns 1 when the window was applied, 0 when the channel keeps the
 * full frame, -1 when it is not configured.
 */
static int t21_fcrop_program(unsigned int i, const uint32_t *win)
{
	struct t21_ispcore_channel_abi *ch = t21_fcrop_channel(i);
	const struct t21_frame_format_view *fmt;
	uint32_t W = t21_sensor_w(), H = t21_sensor_h();
	uint32_t ow, oh, cl, ct, cw, chh, sw, sh, px, py, base;
	int zoom;

	if (!ch || !W || !H)
		return -1;
	fmt = &ch->format;
	if (!t21_fcrop_chan_out(fmt, &ow, &oh))
		return -1;
	if (fmt->crop_enable) {
		cl = fmt->crop_left;
		ct = fmt->crop_top;
		cw = fmt->crop_width;
		chh = fmt->crop_height;
	} else {
		cl = 0;
		ct = 0;
		cw = ow;
		chh = oh;
	}
	zoom = win && !tx_isp_fcrop_is_full(win, W, H) &&
	       tx_isp_fcrop_fits(win, ow, oh);
	if (zoom) {
		tx_isp_fcrop_t21_axis(W, win[2], win[3], ow, cl, cw, &sw, &px);
		tx_isp_fcrop_t21_axis(H, win[1], win[4], oh, ct, chh, &sh, &py);
	} else {
		sw = ow;
		sh = oh;
		px = cl;
		py = ct;
	}
	base = (i + 0x23) << 8;
	system_reg_write((i + 0x24) << 8, (sw << 16) | sh);
	system_reg_write(base + 0x104, (((W << 9) / sw) << 16) |
				       (((H << 9) / sh) & 0xffff));
	system_reg_write(base + 0x12c, (cw << 16) | chh);
	system_reg_write(base + 0x128, (px << 16) | py);
	return zoom;
}

/* s_fmt just configured channel i (not streaming yet): add the window. */
static void t21_fcrop_s_fmt(unsigned int i)
{
	if (i >= T21_FCROP_CHANNELS)
		return;
	mutex_lock(&t21_fcrop_mutex);
	if (tx_isp_fcrop_enabled(t21_fcrop_win))
		t21_fcrop_program(i, t21_fcrop_win);
	mutex_unlock(&t21_fcrop_mutex);
}

/* tisp_init: a new pipeline starts without a window (like T31 boot). */
static void t21_fcrop_reset(void)
{
	memset(t21_fcrop_win, 0, sizeof(t21_fcrop_win));
}

static int t21_fcrop_set(const void __user *uptr)
{
	uint32_t f[TX_ISP_FCROP_WORDS];
	uint32_t W = t21_sensor_w(), H = t21_sensor_h();
	int full, fits = 0, any = 0, ret = 0;
	unsigned int i;

	if (private_copy_from_user(f, uptr, sizeof(f)))
		return -EFAULT;
	if (!W || !H)
		return -EBUSY;
	ret = tx_isp_fcrop_check(f, W, H);
	if (ret)
		return ret;
	full = tx_isp_fcrop_is_full(f, W, H);

	mutex_lock(&t21_fcrop_mutex);
	for (i = 0; i < T21_FCROP_CHANNELS; i++) {
		struct t21_ispcore_channel_abi *ch = t21_fcrop_channel(i);
		uint32_t ow, oh;

		if (!ch || !t21_fcrop_chan_out(&ch->format, &ow, &oh))
			continue;
		any = 1;
		if (tx_isp_fcrop_fits(f, ow, oh))
			fits = 1;
	}
	if (!full && any && !fits) {
		ret = -EOPNOTSUPP;	/* every channel would need enlarging */
		goto out;
	}
	if (any) {
		tisp_channel_stop_save();
		for (i = 0; i < T21_FCROP_CHANNELS; i++)
			t21_fcrop_program(i, full ? NULL : f);
		system_reg_write(0x2318, system_reg_read(0x2318) | 0xe);
		tisp_channel_start_restore();
	}
	if (full)
		memset(t21_fcrop_win, 0, sizeof(t21_fcrop_win));
	else
		memcpy(t21_fcrop_win, f, sizeof(t21_fcrop_win));
	t21_fcrop_win[0] = full ? 0 : 1;
out:
	mutex_unlock(&t21_fcrop_mutex);
	return ret;
}

static int t21_fcrop_get(void __user *uptr)
{
	uint32_t f[TX_ISP_FCROP_WORDS];

	mutex_lock(&t21_fcrop_mutex);
	tx_isp_fcrop_get(t21_fcrop_win, t21_sensor_w(), t21_sensor_h(), f);
	mutex_unlock(&t21_fcrop_mutex);
	return private_copy_to_user(uptr, f, sizeof(f)) ? -EFAULT : 0;
}

/*
 * AE hold, control 0x8000034 (the T23/T31 SetAeFreeze number; the T21 1.0.33
 * library has no such call, so this is an extension).  While set, the AE
 * keeps computing but no new integration time / sensor gain is queued to the
 * sensor: the sensor keeps the values of the moment it was set (used by the
 * OpenIMP sensor test pattern, whose JX-F23 bit 0x0c[0] the sensor driver
 * rewrites with every analogue gain write).
 */
#define T21_CID_AE_FREEZE	0x08000034
static int t21_ae_hold;

static int t21_tuning_ctl_s(int32_t *ctl, int32_t *ret)
{
	uint8_t blk[T21_SINTER_BLOCK];
	uint32_t v = (uint32_t)ctl[1];

	switch ((uint32_t)ctl[0]) {
	case T21_CID_AE_FREEZE:
		ACCESS_ONCE(t21_ae_hold) = v ? 1 : 0;
		*ret = 0;
		return 1;
	case TX_ISP_CID_CSC_ATTR:
		*ret = t21_csc_set((const void __user *)(uintptr_t)v);
		return 1;
	case TX_ISP_CID_FRONT_CROP:
		*ret = t21_fcrop_set((const void __user *)(uintptr_t)v);
		return 1;
	case T21_CID_SCENE_MODE:
		if (v > 14) {
			*ret = -1;
			return 1;
		}
		t21_scene_mode = v;
		*ret = 0;
		return 1;
	case T21_CID_COLORFX:
		*ret = t21_colorfx_set(v);
		return 1;
	case T21_CID_DPC_RATIO:
		if (v > 255) {
			*ret = -1;
			return 1;
		}
		t21_dpc_set_ratio(v);
		*ret = 0;
		return 1;
	case T21_CID_SINTER_DNS:
		/* MoveState passes 0/1, not a pointer: the copy fails and the
		 * stock no-op answers, as before. */
		if (private_copy_from_user(blk, (const void __user *)(uintptr_t)v,
					   sizeof(blk)))
			return 0;
		switch (t21_it_block_mode(blk)) {
		case T21_IT_RANGE:
			t21_ae_set_it_max(t21_it_block_u16(blk, T21_IT_B_MAX));
			*ret = 0;
			return 1;
		case T21_IT_MANUAL:
			t21_ae_set_it_manual(t21_it_block_u16(blk, T21_IT_B_TIME));
			*ret = 0;
			return 1;
		case T21_IT_AUTO:
			t21_ae_set_it_auto();
			*ret = 0;
			return 1;
		}
		if (!blk[T21_SINTER_B_VALID])
			return 0;
		if (blk[T21_SINTER_B_MANUAL]) {
			t21_sinter_type = 1;
			t21_sinter_strength = blk[T21_SINTER_B_STRENGTH];
			t21_sinter_set_ratio(t21_sinter_strength);
		} else {
			t21_sinter_type = 0;
			t21_sinter_set_ratio(128);
		}
		*ret = 0;
		return 1;
	case T21_CID_TEMPER_TYPE:
		if (v > T21_TEMPER_MANUAL) {
			*ret = -1;
			return 1;
		}
		t21_temper_type = v;
		t21_temper_update();
		*ret = 0;
		return 1;
	case T21_CID_TEMPER_ATTR:
	case T21_CID_TEMPER_CTL:
		if (v > 255) {
			*ret = -1;
			return 1;
		}
		t21_temper_strength = v;
		if (t21_temper_type == T21_TEMPER_MANUAL)
			t21_3dns_apply(v);
		*ret = 0;
		return 1;
	case T21_CID_3DNS_RATIO:
		/* stock path (tisp_s_3dns_ratio for < 256); remember it as
		 * the AUTO ratio and as what the banks now hold */
		if (v < 256) {
			t21_3dns_auto_ratio = v;
			t21_3dns_applied = v;
		}
		return 0;
	}
	return 0;
}

/* Get side, same convention; the value goes to ctl[1]. */
static int t21_tuning_ctl_g(int32_t *ctl, int32_t *ret)
{
	uint8_t blk[T21_SINTER_BLOCK];
	uint32_t v = (uint32_t)ctl[1];

	switch ((uint32_t)ctl[0]) {
	case TX_ISP_CID_CSC_ATTR:
		*ret = t21_csc_get((void __user *)(uintptr_t)v);
		return 1;
	case TX_ISP_CID_FRONT_CROP:
		*ret = t21_fcrop_get((void __user *)(uintptr_t)v);
		return 1;
	case T21_CID_SCENE_MODE:
		ctl[1] = (int32_t)t21_scene_mode;
		break;
	case T21_CID_COLORFX:
		ctl[1] = (int32_t)t21_colorfx;
		break;
	case T21_CID_DPC_RATIO:
		ctl[1] = (int32_t)t21_dpc_ratio;
		break;
	case T21_CID_TEMPER_TYPE:
		ctl[1] = (int32_t)t21_temper_type;
		break;
	case T21_CID_TEMPER_ATTR:
	case T21_CID_TEMPER_CTL:
		ctl[1] = (int32_t)t21_temper_strength;
		break;
	case T21_CID_EXPR:
		/* Stock g_ctrl acknowledges 0x8000025 without writing the
		 * record; the pre-lift glue answered it from the live AE
		 * (t21_g_expr), lost when the stock dispatcher was lifted.
		 * GetExpr's maximum and line time are what a caller converts
		 * an integration-time cap with. */
		*ret = t21_g_expr((void __user *)(uintptr_t)v);
		return 1;
	case T21_CID_WAIT_FRAME:
		*ret = t21_wait_frame((void __user *)(uintptr_t)v);
		return 1;
	case T21_CID_SINTER_DNS: {
		const struct t21_ae_ctrls_view *ae =
			(const struct t21_ae_ctrls_view *)tisp_ae_ctrls;
		u32 min_it, max_it;

		/* One getter block for both: the sinter getter reads [10],
		 * [43], [70] and [98], the integration-time getter [0x03],
		 * u16 [0x18] and u16 [0x1a]; both sets are filled. */
		if (private_copy_from_user(blk, (const void __user *)(uintptr_t)v,
					   sizeof(blk)))
			return 0;
		t21_ae_it_limits(&min_it, &max_it);
		t21_it_block_fill(blk, ae->it_manual || ae->manual_mode,
				  ae->integration_time, max_it);
		blk[T21_SINTER_B_VALID] = 1;
		blk[T21_SINTER_B_MANUAL] = t21_sinter_type ? 1 : 0;
		blk[T21_SINTER_B_MANUAL2] = t21_sinter_type ? 1 : 0;
		blk[T21_SINTER_B_STRENGTH] = (uint8_t)t21_sinter_strength;
		*ret = private_copy_to_user((void __user *)(uintptr_t)v, blk,
					    sizeof(blk)) ? -14 : 0;
		return 1;
	}
	default:
		return 0;
	}
	*ret = 0;
	return 1;
}

#endif /* !T21_TUNING_CTL_HOST_TEST */
#endif /* TX_ISP_T21_TUNING_CTL_H */
