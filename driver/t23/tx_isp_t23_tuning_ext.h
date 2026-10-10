/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T23 tuning controls of the stock 1.3.0 libimp that the recovered driver
 * did not route (they failed with -EINVAL): the data handling of the stock
 * apical_isp_core_ops_s_ctrl / g_ctrl handlers and the tisp_* helpers they
 * call, taken from libt23-firmware-1.3.0-540-31014.a.
 *
 * Only plain buffers in, plain buffers out: the driver passes its copies of
 * the stock objects (the lifted AE's oem_* arrays, tparams), the host tests
 * pass their own.  No locking, no user copies here.
 */
#ifndef TX_ISP_T23_TUNING_EXT_H
#define TX_ISP_T23_TUNING_EXT_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdint.h>
#include <string.h>
#endif

/* ---- AE ------------------------------------------------------------- */

#define T23X_ZONES 225U                 /* 15 x 15 */
#define T23X_WEIGHT_MAX 8U
#define T23X_AE_CTRLS_WORDS 44U         /* tisp_ae_ctrls, 176 bytes */
#define T23X_AE_HIST_BYTES 1052U        /* tisp_ae_hist(_last) */
#define T23X_AE_HIST_THRESH 1044U       /* 4 bin thresholds */
#define T23X_AE_HIST_BINS 1024U         /* 5 x u32 bin sums */
#define T23X_AE_HIST_NODES 1048U        /* nodeh, nodev */
#define T23X_AE_ZONE_OFF 13872U         /* IspAeStatic: 15 x 15 luma */
#define T23X_GAIN_UNITY 1024U

/* tparams offsets (active bank) and the same block in the day/night banks */
#define T23X_TP_BANK_BASE 0x13100U
#define T23X_TP_AE_ZONE_WEIGHT 0x13448U /* bank +840 */
#define T23X_TP_AE_ROUI_WEIGHT 0x137ccU /* bank +1740 */
#define T23X_TP_AE_ROI_WEIGHT 0x13b50U  /* bank +2640 */

/*
 * IMPISPWeight (15 x 15 bytes, 0..8) to the stock u32 table
 * (apical_isp_ae_s_roi / apical_isp_ae_zone_weight_s_attr).  The stock code
 * stops at the first entry above 8 and fails; nothing is applied then.
 */
static inline int t23x_weight_expand(const uint8_t *in, uint32_t *out)
{
	unsigned int i;

	for (i = 0; i < T23X_ZONES; i++)
		if (in[i] > T23X_WEIGHT_MAX)
			return -EINVAL;
	for (i = 0; i < T23X_ZONES; i++)
		out[i] = in[i];
	return 0;
}

/* the get direction: the low byte of each word */
static inline void t23x_weight_pack(const uint32_t *in, uint8_t *out)
{
	unsigned int i;

	for (i = 0; i < T23X_ZONES; i++)
		out[i] = (uint8_t)in[i];
}

/* tisp_s_aeroi_weight: the "roui" table is 8 - roi per zone */
static inline void t23x_roui_from_roi(const uint32_t *roi, uint32_t *roui)
{
	unsigned int i;

	for (i = 0; i < T23X_ZONES; i++)
		roui[i] = T23X_WEIGHT_MAX - roi[i];
}

static inline uint32_t t23x_at_least_unity(uint32_t v)
{
	return v < T23X_GAIN_UNITY ? T23X_GAIN_UNITY : v;
}

/*
 * tisp_ae_manual_set on tisp_ae_ctrls (c) from a 44-word block (w), as the
 * stock code does it:
 *   w0 freeze: c0; then it (w3), again (w1), dgain (w2) and ISP dgain (w4)
 *              are all taken, the gains raised to at least 1x (1024);
 *   else each manual enable (w15 it, w16 again, w38 dgain, w17 ISP dgain)
 *              is stored and, when set, its value;
 *   w26 short-frame freeze, else w18/w19/w28/w39 for the short frame.
 * The caller clears IspAeFlag[7] (the stock tail does).
 */
static inline void t23x_ae_manual_set(uint32_t *c, const uint32_t *w)
{
	c[0] = w[0];
	if (w[0]) {
		c[1] = t23x_at_least_unity(w[1]);
		c[4] = t23x_at_least_unity(w[4]);
		c[2] = t23x_at_least_unity(w[2]);
		c[3] = w[3];
	} else {
		c[15] = w[15];
		if (w[15])
			c[3] = w[3];
		c[16] = w[16];
		if (w[16])
			c[1] = t23x_at_least_unity(w[1]);
		c[17] = w[17];
		if (w[17])
			c[4] = t23x_at_least_unity(w[4]);
		c[38] = w[38];
		if (w[38])
			c[2] = t23x_at_least_unity(w[2]);
	}
	c[26] = w[26];
	if (w[26]) {
		c[20] = t23x_at_least_unity(w[20]);
		c[27] = t23x_at_least_unity(w[27]);
		c[21] = w[21];
		c[29] = t23x_at_least_unity(w[29]);
	} else {
		c[18] = w[18];
		if (w[18])
			c[21] = w[21];
		c[19] = w[19];
		if (w[19])
			c[20] = t23x_at_least_unity(w[20]);
		c[28] = w[28];
		if (w[28])
			c[29] = t23x_at_least_unity(w[29]);
		c[39] = w[39];
		if (w[39])
			c[27] = t23x_at_least_unity(w[27]);
	}
}

/*
 * tisp_set_ae_freeze: the current ctrls with freeze (word 0) and the
 * short-frame freeze (word 26) both set to 0 or 1, through
 * tisp_ae_manual_set.  Other values fail.
 */
static inline int t23x_ae_freeze(uint32_t *c, uint32_t on)
{
	uint32_t w[T23X_AE_CTRLS_WORDS];

	if (on > 1U)
		return -EINVAL;
	memcpy(w, c, sizeof(w));
	w[0] = on;
	w[26] = on;
	t23x_ae_manual_set(c, w);
	return 0;
}

/*
 * apical_isp_expr_s_ctrl: IMPISPExpr.s_attr {mode, unit, time} through
 * tisp_s_ae_attr (the current ctrls with it = w3 and the it manual enable
 * = w15).  mode 0 = auto (enable 0), 1 = manual; unit 0 = lines, 1 = us
 * (divided by the line time, which must not be 0).  Other values fail.
 * The stock auto path passes an uninitialised it, used only while AE is
 * frozen; the current it is kept here.
 */
static inline int t23x_expr_set(uint32_t *c, uint32_t mode, uint32_t unit,
				uint16_t time, uint32_t line_us)
{
	uint32_t w[T23X_AE_CTRLS_WORDS];

	memcpy(w, c, sizeof(w));
	if (mode == 0U) {
		w[15] = 0;
	} else if (mode == 1U) {
		w[15] = 1;
		if (unit == 1U) {
			if (!line_us)
				return -EINVAL;
			w[3] = (uint32_t)time / line_us;
		} else if (unit == 0U) {
			w[3] = time;
		} else {
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}
	t23x_ae_manual_set(c, w);
	return 0;
}

/*
 * GetAeHist: IMPISPAEHist (16 bytes) from tisp_ae_hist_last: the four bin
 * thresholds, the five bin sums as u16, nodeh and nodev.
 */
static inline void t23x_ae_hist_pack(const uint8_t *h, uint8_t *out)
{
	unsigned int i;

	memset(out, 0, 16);
	memcpy(out, h + T23X_AE_HIST_THRESH, 4);
	for (i = 0; i < 5U; i++) {
		uint32_t v;

		memcpy(&v, h + T23X_AE_HIST_BINS + 4U * i, sizeof(v));
		out[4U + 2U * i] = (uint8_t)v;
		out[5U + 2U * i] = (uint8_t)(v >> 8);
	}
	out[14] = h[T23X_AE_HIST_NODES];
	out[15] = h[T23X_AE_HIST_NODES + 1U];
}

/*
 * IMPISPAEState (12 bytes) from _ae_stat: stable = word 1 != 0, target =
 * word 0, ae_mean = word 2.  The stock code leaves bytes 1..3 as stack.
 */
static inline void t23x_ae_state_pack(const uint32_t *stat, uint8_t *out)
{
	memset(out, 0, 12);
	out[0] = stat[1] ? 1U : 0U;
	memcpy(out + 4, &stat[0], 4);
	memcpy(out + 8, &stat[2], 4);
}

/*
 * tisp_ae_s_min on ae_exp_th (IspAeExp[k] points at ae_exp_th[k]) and the
 * reload flags in ae_extra_at_list_wdr: a minimum integration time above
 * 0 and not above the maximum (th[0]) goes to th[4] (flag 0), a minimum
 * analog gain of at least 1x and not above th[1] to th[5] (flag 1).  Bad
 * values are only logged by the stock code; the call still returns 0.
 * Returns a mask of the values taken (bit 0 it, bit 1 again).
 */
static inline unsigned int t23x_ae_min_set(uint32_t *th, uint32_t *wdr,
					   uint32_t it_min, uint32_t again_min)
{
	unsigned int taken = 0;

	if (it_min && th[0] >= it_min) {
		wdr[0] = 1;
		th[4] = it_min;
		taken |= 1U;
	}
	if (again_min >= T23X_GAIN_UNITY && th[1] >= again_min) {
		wdr[1] = 1;
		th[5] = again_min;
		taken |= 2U;
	}
	return taken;
}

/* ---- AWB ------------------------------------------------------------ */

#define T23X_AWB_ZONE_BYTES 675U        /* IMPISPAWBZone: r, g, b x 225 */
#define T23X_TP_AWB_ZONE_WEIGHT 0x14a34U /* _awb_wght, bank +0x1934 */

/*
 * GetAwbZone (stock Tiziano_awb_fpga): per zone the channel sum divided by
 * the zone's pixel count, stored as a byte (truncated, not saturated); an
 * empty zone reads 0.  r at 0, g at 225, b at 450.
 */
static inline void t23x_awb_zone_pack(const uint32_t *r, const uint32_t *g,
				      const uint32_t *b, const uint32_t *pix,
				      uint8_t *out)
{
	unsigned int i;

	for (i = 0; i < T23X_ZONES; i++) {
		uint32_t n = pix[i];

		out[i] = n ? (uint8_t)(r[i] / n) : 0U;
		out[T23X_ZONES + i] = n ? (uint8_t)(g[i] / n) : 0U;
		out[2U * T23X_ZONES + i] = n ? (uint8_t)(b[i] / n) : 0U;
	}
}

/*
 * The colour temperature the AWB run hands on (stock JZ_Isp_Awb): in the
 * manual WB mode (1) the one set by SetAwbCt (when not 0) replaces it; in
 * the auto mode (0) the set value follows the measured one; the preset
 * modes leave both alone.
 */
static inline uint32_t t23x_awb_ct_select(uint32_t wb_mode, uint32_t ct,
					  uint32_t *custom)
{
	if (wb_mode == 1U)
		return *custom ? *custom : ct;
	if (wb_mode == 0U)
		*custom = ct;
	return ct;
}

/* ---- Gamma, frame wait ------------------------------------------------ */

#define T23X_GAMMA_BYTES 258U           /* 129 x u16 */
#define T23X_TP_GAMMA 0x15998U          /* bank +0x2898 */
#define T23X_WAIT_FRAME_BYTES 24U       /* stock libimp/kernel block */
#define T23X_CID_WAIT_FRAME 0x08000162U

/*
 * A gamma curve the LUT writer takes: 129 points, 12 bit, not falling (the
 * check tiziano_gamma_params_refresh applies to an IQ bank's curve).
 */
static inline int t23x_gamma_valid(const uint16_t *lut)
{
	unsigned int i;

	for (i = 0; i < T23X_GAMMA_BYTES / 2U; i++)
		if (lut[i] > 0xfffU || (i && lut[i] < lut[i - 1U]))
			return -EINVAL;
	return 0;
}

/*
 * isp_frame_done_wait result: 0 when a frame-done came, -ERESTARTSYS when
 * interrupted, else -ETIMEDOUT.  wait_ret is the wait_event_*_timeout
 * return value (remaining jiffies, 0 on timeout, or -ERESTARTSYS).
 */
static inline long t23x_wait_frame_result(long wait_ret, int cond)
{
	if (wait_ret < 0)
		return wait_ret;
	return cond ? 0 : -ETIMEDOUT;
}

/* ---- AWB cluster / colour-temperature trend ------------------------------ */

/*
 * SetAwbClust / GetAwbClust (0x0800000e), SetAwbCtTrend / GetAwbCtTrend
 * (0x0800000f); stock apical_isp_core_ops_s_ctrl / g_ctrl copy exactly
 * IMPISPAWBCluster (40 bytes) and IMPISPAWBCtTrend (24 bytes) and call
 * tisp_awb_set/get_cluster_awb_params and tisp_awb_set/get_ct_trend.
 *
 * The stock objects are not in the user order:
 *   _awb_cluster[10]: [0] ClusterEn, [1..7] awb_cluster[0..6],
 *                     [8] ToleranceEn, [9] tolerance_th
 *   _awb_trend[7]:    [0] enable (set to 1 by every set), [1..6] trend_array
 * The set marks the *_api_status[1] word with 2 (a user value is pending)
 * and, unless status[0] is 1, mirrors the object into *_api_para (what the
 * IQ refresh reads).
 */
#define T23X_AWB_CLUSTER_USER_BYTES 40U /* sizeof(IMPISPAWBCluster) */
#define T23X_AWB_CLUSTER_WORDS 10U
#define T23X_AWB_TREND_USER_BYTES 24U   /* sizeof(IMPISPAWBCtTrend) */
#define T23X_AWB_TREND_WORDS 7U

static inline void t23x_awb_cluster_set(uint32_t *cl, uint32_t *status,
					uint32_t *api_para, const uint32_t *in)
{
	unsigned int i;

	for (i = 0; i < 7U; i++)
		cl[1U + i] = in[3U + i];
	cl[0] = in[0];
	cl[8] = in[1];
	cl[9] = in[2];
	status[1] = 2;
	if (status[0] != 1)
		memcpy(api_para, cl, T23X_AWB_CLUSTER_WORDS * sizeof(uint32_t));
}

static inline void t23x_awb_cluster_get(const uint32_t *cl, uint32_t *out)
{
	unsigned int i;

	out[0] = cl[0];
	out[1] = cl[8];
	out[2] = cl[9];
	for (i = 0; i < 7U; i++)
		out[3U + i] = cl[1U + i];
}

static inline void t23x_awb_trend_set(uint32_t *tr, uint32_t *status,
				      uint32_t *api_para, const uint32_t *in)
{
	unsigned int i;

	for (i = 0; i < 6U; i++)
		tr[1U + i] = in[i];
	tr[0] = 1;
	status[1] = 2;
	if (status[0] != 1)
		memcpy(api_para, tr, T23X_AWB_TREND_WORDS * sizeof(uint32_t));
}

static inline void t23x_awb_trend_get(const uint32_t *tr, uint32_t *out)
{
	unsigned int i;

	for (i = 0; i < 6U; i++)
		out[i] = tr[1U + i];
}

/* ---- Mask blocks (SetMaskBlock / GetMaskBlock) ----------------------------- */

/*
 * SetMaskBlock / GetMaskBlock (0x08000183): stock apical_isp_core_ops_s_ctrl
 * copies exactly sizeof(IMPISPMaskBlockAttr) = 20 bytes (T23 1.3.0 imp_isp.h:
 * u8 chx @0, u8 pinum @1, u8 mask_en @2, u16 mask_pos_top @4, mask_pos_left
 * @6, mask_width @8, mask_height @10, IMPISP_MASK_TYPE (int) mask_type @12,
 * 3 colour bytes @16) and calls tisp_s_mscaler_mask_block_attr; g_ctrl calls
 * tisp_g_mscaler_mask_block_attr on a local and copies the same 20 bytes
 * out.
 *
 * The stock table lives in the mscaler object (stock .bss, 2316 bytes): one
 * 16 byte entry per block at 1836 + 16 * (chx * 4 + pinum):
 *   +0 u8 enable, +2 u16 left, +4 u16 top, +6 u16 width, +8 u16 height,
 *   +12 u32 colour (c0 << 16 | c1 << 8 | c2, the three user bytes)
 * and the dirty bit mask (1 << (chx * 4 + pinum)) is or-ed into the word at
 * 2308, which the mscaler update (tisp_msca_Shd_ctrl -> tisp_msca_set_omi_api
 * -> tisp_msca_api_set_mask) consumes.  Set takes the geometry and colour
 * only for mask_en == 1, else just clears the entry's enable; the type is
 * not stored.  Stock checks neither chx nor pinum (an index beyond the 12
 * entries writes past the table), here a block outside chx 0..2, pinum 0..3
 * is refused (-EINVAL), nothing written.
 *
 * Stock quirks kept: Get always reports mask_en = 0 (it clears the byte and
 * never sets it), and for a disabled entry zeroes the geometry and colour.
 * Beyond stock: stock Get passes an uninitialised local, so it neither
 * knows chx / pinum nor defines the bytes it leaves (kernel stack); here the
 * user's block is read first (chx, pinum), and the bytes stock leaves out
 * (mask_type, padding) are 0.
 */
#define T23X_MASK_BLOCK_BYTES 20U       /* sizeof(IMPISPMaskBlockAttr) */
#define T23X_MASK_CHANNELS 3U
#define T23X_MASK_PER_CHANNEL 4U
#define T23X_MSCA_BYTES 2316U           /* sizeof(mscaler) */
#define T23X_MSCA_MASK_OFF 1836U
#define T23X_MSCA_MASK_STRIDE 16U
#define T23X_MSCA_MASK_DIRTY_OFF 2308U

static inline uint16_t t23x_le16_get(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static inline void t23x_le16_put(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static inline int t23x_mask_block_index(const uint8_t *blk)
{
	if (blk[0] >= T23X_MASK_CHANNELS || blk[1] >= T23X_MASK_PER_CHANNEL)
		return -EINVAL;
	return blk[0] * (int)T23X_MASK_PER_CHANNEL + blk[1];
}

/* tisp_s_mscaler_mask_block_attr on the mscaler object msca */
static inline int t23x_mask_block_set(uint8_t *msca, const uint8_t *in)
{
	int idx = t23x_mask_block_index(in);
	uint8_t *e;
	uint32_t dirty;

	if (idx < 0)
		return idx;
	e = msca + T23X_MSCA_MASK_OFF + T23X_MSCA_MASK_STRIDE * (unsigned int)idx;
	if (in[2] == 1U) {
		e[0] = 1;
		t23x_le16_put(e + 4, t23x_le16_get(in + 4));    /* top */
		t23x_le16_put(e + 2, t23x_le16_get(in + 6));    /* left */
		t23x_le16_put(e + 6, t23x_le16_get(in + 8));    /* width */
		t23x_le16_put(e + 8, t23x_le16_get(in + 10));   /* height */
		e[12] = in[18];
		e[13] = in[17];
		e[14] = in[16];
		e[15] = 0;
	} else {
		e[0] = 0;
	}
	memcpy(&dirty, msca + T23X_MSCA_MASK_DIRTY_OFF, sizeof(dirty));
	dirty |= 1U << (unsigned int)idx;
	memcpy(msca + T23X_MSCA_MASK_DIRTY_OFF, &dirty, sizeof(dirty));
	return 0;
}

/* tisp_g_mscaler_mask_block_attr: the block for in's chx / pinum */
static inline int t23x_mask_block_get(const uint8_t *msca, const uint8_t *in,
				      uint8_t *out)
{
	int idx = t23x_mask_block_index(in);
	const uint8_t *e;

	if (idx < 0)
		return idx;
	e = msca + T23X_MSCA_MASK_OFF + T23X_MSCA_MASK_STRIDE * (unsigned int)idx;
	memset(out, 0, T23X_MASK_BLOCK_BYTES);
	out[0] = in[0];
	out[1] = in[1];
	if (e[0] == 1U) {
		t23x_le16_put(out + 4, t23x_le16_get(e + 4));
		t23x_le16_put(out + 6, t23x_le16_get(e + 2));
		t23x_le16_put(out + 8, t23x_le16_get(e + 6));
		t23x_le16_put(out + 10, t23x_le16_get(e + 8));
		out[16] = e[14];
		out[17] = e[13];
		out[18] = e[12];
	}
	return 0;
}

/* ---- ISP OSD attribute / OSD block / draw block (0x08000181 / 182 / 180) ---- */

/*
 * The stock handlers of SetOSDAttr / SetOSDBlock / SetDrawBlock (and the
 * getters) keep their state in the same mscaler object as the mask blocks
 * (stock .bss, 2316 bytes); tisp_msca_Shd_ctrl -> tisp_msca_set_omi_api ->
 * tisp_msca_api_set_osd / _set_line then program it into the MSCA.
 *
 *   OSD attribute (3 x u32 in the user block, kept as 3 bytes @2300..2302):
 *     Set: when the first word differs from the stored byte, the 8 block
 *     enables (@2186 + 16 * n) are cleared and the OSD dirty word (@2304) is
 *     set to 255 (an assignment, not an or); then the three bytes are stored.
 *     Get: the three bytes as u32.
 *   OSD block (20 bytes: u8 chx @0, u8 enable @1, u16 x @2, y @4, w @6, h @8,
 *     u32 picture address @12, u16 stride @16): the geometry of block n is
 *     the u16 x/y/w/h at 1836 + 16 * (21 + n) + 0/2/4/6, the picture address
 *     (u32) and stride (u16) at 16 * (136 + n) + 4 / + 8 and the enable byte
 *     at 16 * (136 + n) + 10.  Set with enable == 1 first compares the
 *     STORED width / height of the block with the channel 0 limits
 *     (msca +48 / +52: stale values, stock quirk; a violation logs and
 *     returns 0 without storing), then ignores a block with x + w or y + h
 *     beyond the limits (logs; the helper's -1 is dropped by s_ctrl, the
 *     user call returns 0), stores the block, trims w / h to the limits and sets the
 *     block's bit in the dirty word @2304.  Set with enable != 1 zeroes the
 *     block's geometry, address, stride and enable.  Get clears the 20 bytes
 *     and, for an enabled block, reports enable = 1, x, y, w, h, the address
 *     plus 0x80000000 and the stride.
 *   Draw block (32 bytes: u8 pinum @0, u32 type @4, u8 @12, u16 @14/16/18/20,
 *     colour bytes @22..24, u8 @25 / @26, u16 @28): entry n at 2028 + 24 * n
 *     (+0 type byte, +4 u8, +6/+8/+10/+12 u16, +16 u32 colour (@22 << 16 |
 *     @23 << 8 | @24), +20 u8, +21 u8, +22 u16).  Types 0 and 2 store all but
 *     +22, type 1 stores +22 too, any other type only the type byte; the
 *     block's bit is set in the draw dirty word @2312.  Get writes the same
 *     fields back and the colour: byte 22 = colour byte 2, byte 23 = colour
 *     byte 1 and then (stock slip) byte 23 again = colour byte 0; byte 24
 *     is never written.
 *
 * Stock checks no block / entry index; here an OSD block above 7 and a draw
 * block above 5 (the 8 and 6 entries the MSCA update walks) are refused with
 * -EINVAL, nothing written.  Stock Get passes an uninitialised stack local,
 * so it neither knows the index nor defines the bytes it leaves; here the
 * user's block is read first (index) and the bytes stock leaves out are 0 /
 * the user's.  Like the mask blocks, a stored OSD or draw block has no image
 * effect yet in this driver (the MSCA update that consumes it is not run).
 */
#define T23X_OSD_ATTR_BYTES 12U
#define T23X_OSD_BLOCK_BYTES 20U
#define T23X_DRAW_BLOCK_BYTES 32U
#define T23X_OSD_BLOCKS 8U
#define T23X_DRAW_BLOCKS 6U
#define T23X_MSCA_OSD_ATTR_OFF 2300U
#define T23X_MSCA_OSD_DIRTY_OFF 2304U
#define T23X_MSCA_DRAW_DIRTY_OFF 2312U
#define T23X_MSCA_OSD_EN_OFF 2186U     /* 16 * 136 + 10 */
#define T23X_MSCA_DRAW_OFF 2028U
#define T23X_MSCA_DRAW_STRIDE 24U
#define T23X_MSCA_LIMIT_W_OFF 48U      /* channel 0 record of msca */
#define T23X_MSCA_LIMIT_H_OFF 52U

static inline uint32_t t23x_le32_get(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	       (uint32_t)p[3] << 24;
}

static inline void t23x_le32_put(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* tisp_s_osd_attr on the mscaler object m; in = 3 x u32 */
static inline void t23x_osd_attr_set(uint8_t *m, const uint8_t *in)
{
	unsigned int i;

	if ((uint32_t)m[T23X_MSCA_OSD_ATTR_OFF] != t23x_le32_get(in)) {
		for (i = 0; i < T23X_OSD_BLOCKS; i++)
			m[T23X_MSCA_OSD_EN_OFF + 16U * i] = 0;
		t23x_le32_put(m + T23X_MSCA_OSD_DIRTY_OFF, 255U);
	}
	m[T23X_MSCA_OSD_ATTR_OFF] = (uint8_t)t23x_le32_get(in);
	m[T23X_MSCA_OSD_ATTR_OFF + 1U] = (uint8_t)t23x_le32_get(in + 4);
	m[T23X_MSCA_OSD_ATTR_OFF + 2U] = (uint8_t)t23x_le32_get(in + 8);
}

/* tisp_g_osd_attr: out = 3 x u32 */
static inline void t23x_osd_attr_get(const uint8_t *m, uint8_t *out)
{
	t23x_le32_put(out, m[T23X_MSCA_OSD_ATTR_OFF]);
	t23x_le32_put(out + 4, m[T23X_MSCA_OSD_ATTR_OFF + 1U]);
	t23x_le32_put(out + 8, m[T23X_MSCA_OSD_ATTR_OFF + 2U]);
}

/* geometry entry (x y w h as u16) and data entry (addr, stride, enable) of block n */
static inline uint8_t *t23x_osd_geo(uint8_t *m, unsigned int n)
{
	return m + T23X_MSCA_MASK_OFF + 16U * (21U + n);
}

static inline uint8_t *t23x_osd_dat(uint8_t *m, unsigned int n)
{
	return m + 16U * (136U + n);
}

/*
 * tisp_s_osd_block_attr.  msca: the stock msca object (channel 0 limits).
 * Returns 0, -EINVAL (index refused, beyond stock) or -ERANGE (x + w or
 * y + h beyond the limits: the stock helper logs and returns -1, but the
 * stock s_ctrl discards that result, so the user call still returns 0 and
 * nothing is stored).  *quirk is set to 1 when the stock code logged the
 * stale-size violation and returned 0 without storing (the caller may log
 * it), else 0.
 */
static inline int t23x_osd_block_set(uint8_t *m, const uint8_t *msca,
				     const uint8_t *in, int *quirk)
{
	unsigned int n = in[0];
	uint8_t *g, *d;
	uint32_t lim_w, lim_h, dirty;

	if (quirk)
		*quirk = 0;
	if (n >= T23X_OSD_BLOCKS)
		return -EINVAL;
	g = t23x_osd_geo(m, n);
	d = t23x_osd_dat(m, n);
	lim_w = t23x_le32_get(msca + T23X_MSCA_LIMIT_W_OFF);
	lim_h = t23x_le32_get(msca + T23X_MSCA_LIMIT_H_OFF);
	if (in[1] == 1U) {
		if (lim_w < t23x_le16_get(g + 4) || lim_h < t23x_le16_get(g + 6)) {
			if (quirk)
				*quirk = 1;
			return 0;
		}
		if (lim_w < (uint32_t)t23x_le16_get(in + 2) + t23x_le16_get(in + 6) ||
		    lim_h < (uint32_t)t23x_le16_get(in + 4) + t23x_le16_get(in + 8))
			return -ERANGE;
		d[10] = 1;
		t23x_le16_put(g, t23x_le16_get(in + 2));
		t23x_le16_put(g + 2, t23x_le16_get(in + 4));
		t23x_le16_put(g + 4, t23x_le16_get(in + 6));
		t23x_le16_put(g + 6, t23x_le16_get(in + 8));
		t23x_le32_put(d + 4, t23x_le32_get(in + 12));
		t23x_le16_put(d + 8, t23x_le16_get(in + 16));
		if (lim_w < (uint32_t)t23x_le16_get(g + 4) + t23x_le16_get(g))
			t23x_le16_put(g + 4, (uint16_t)(lim_w - t23x_le16_get(g)));
		if (lim_h < (uint32_t)t23x_le16_get(g + 6) + t23x_le16_get(g + 2))
			t23x_le16_put(g + 6, (uint16_t)(lim_h - t23x_le16_get(g + 2)));
	} else {
		d[10] = 0;
		t23x_le16_put(g, 0);
		t23x_le16_put(g + 2, 0);
		t23x_le16_put(g + 4, 0);
		t23x_le16_put(g + 6, 0);
		t23x_le32_put(d + 4, 0);
		t23x_le16_put(d + 8, 0);
	}
	dirty = t23x_le32_get(m + T23X_MSCA_OSD_DIRTY_OFF) | (1U << n);
	t23x_le32_put(m + T23X_MSCA_OSD_DIRTY_OFF, dirty);
	return 0;
}

/* tisp_g_osd_block_attr for the block in[0]; out = 20 bytes */
static inline int t23x_osd_block_get(uint8_t *m, const uint8_t *in, uint8_t *out)
{
	unsigned int n = in[0];
	const uint8_t *g, *d;

	if (n >= T23X_OSD_BLOCKS)
		return -EINVAL;
	g = t23x_osd_geo(m, n);
	d = t23x_osd_dat(m, n);
	memset(out, 0, T23X_OSD_BLOCK_BYTES);
	out[0] = (uint8_t)n;
	if (d[10] == 1U) {
		out[1] = 1;
		t23x_le16_put(out + 2, t23x_le16_get(g));
		t23x_le16_put(out + 4, t23x_le16_get(g + 2));
		t23x_le16_put(out + 6, t23x_le16_get(g + 4));
		t23x_le16_put(out + 8, t23x_le16_get(g + 6));
		t23x_le32_put(out + 12, t23x_le32_get(d + 4) + 0x80000000U);
		t23x_le16_put(out + 16, t23x_le16_get(d + 8));
	}
	return 0;
}

static inline uint8_t *t23x_draw_entry(uint8_t *m, unsigned int n)
{
	return m + T23X_MSCA_DRAW_OFF + T23X_MSCA_DRAW_STRIDE * n;
}

/* tisp_s_draw_block_attr; in = 32 bytes */
static inline int t23x_draw_block_set(uint8_t *m, const uint8_t *in)
{
	unsigned int n = in[0];
	uint32_t type = t23x_le32_get(in + 4);
	uint8_t *e;
	uint32_t dirty;

	if (n >= T23X_DRAW_BLOCKS)
		return -EINVAL;
	e = t23x_draw_entry(m, n);
	e[0] = (uint8_t)type;
	if (type <= 2U) {
		e[4] = in[12];
		t23x_le16_put(e + 6, t23x_le16_get(in + 14));
		t23x_le16_put(e + 8, t23x_le16_get(in + 16));
		t23x_le16_put(e + 10, t23x_le16_get(in + 18));
		t23x_le16_put(e + 12, t23x_le16_get(in + 20));
		t23x_le32_put(e + 16, (uint32_t)in[22] << 16 |
				      (uint32_t)in[23] << 8 | in[24]);
		e[20] = in[25];
		e[21] = in[26];
		if (type == 1U)
			t23x_le16_put(e + 22, t23x_le16_get(in + 28));
	}
	dirty = t23x_le32_get(m + T23X_MSCA_DRAW_DIRTY_OFF) | (1U << n);
	t23x_le32_put(m + T23X_MSCA_DRAW_DIRTY_OFF, dirty);
	return 0;
}

/* tisp_g_draw_block_attr for the block in[0]; out = 32 bytes, starts as in */
static inline int t23x_draw_block_get(uint8_t *m, const uint8_t *in, uint8_t *out)
{
	unsigned int n = in[0];
	const uint8_t *e;
	uint32_t type;

	if (n >= T23X_DRAW_BLOCKS)
		return -EINVAL;
	e = t23x_draw_entry(m, n);
	memcpy(out, in, T23X_DRAW_BLOCK_BYTES);
	type = e[0];
	t23x_le32_put(out + 4, type);
	if (type <= 2U) {
		out[12] = e[4];
		t23x_le16_put(out + 14, t23x_le16_get(e + 6));
		t23x_le16_put(out + 16, t23x_le16_get(e + 8));
		t23x_le16_put(out + 18, t23x_le16_get(e + 10));
		t23x_le16_put(out + 20, t23x_le16_get(e + 12));
		out[25] = e[20];
		out[26] = e[21];
		if (type == 1U)
			t23x_le16_put(out + 28, t23x_le16_get(e + 22));
		out[22] = e[18];
		out[23] = e[17];
		out[23] = e[16];        /* stock slip: overwrites byte 23 */
	}
	return 0;
}

/* ---- AutoZoom (0x80000e8) ------------------------------------------- */

/*
 * Stock apical_isp_autozoom_s_attr + tisp_s_autozoom_control: the 36-byte
 * IMPISPAutoZoom { chan, scaler_enable, scaler_outwidth, scaler_outheight,
 * crop_enable, crop_left, crop_top, crop_width, crop_height } is applied to
 * the 56-byte channel record of the MSCA ("cfg"):
 *   crop enabled      cfg[3] = 1 (locks the crop against the channel
 *                     attributes), +0x10 left, +0x14 top, +0x18 width,
 *                     +0x1c height;
 *   crop not enabled  window (0, 0, full_width, full_height), the lock byte
 *                     is left as it was;
 *   scaler enabled    cfg[4] = 1 (lock), +0x20 out width, +0x24 out height;
 *   scaler not enabled only +0x24 = +0x1c (the stock code leaves +0x20).
 * full_width/height are the 16-bit words the stock code reads at
 * mscaler + 2 * chan and + 2 * (chan + 3).  The stock code then runs
 * tisp_msca_crop_api for the channel.
 *
 * Beyond stock (the stock code takes anything and can stall the MSCA, as
 * the front crop does): a channel above 2, a window outside the sensor
 * picture or below 64x64 or odd, and a scaler output that is odd, below
 * 64 or larger than the window (also the stale width the stock code
 * leaves with the scaler off) are refused with -EINVAL and nothing is
 * written.  The stock code takes a channel above 2 as channel 0 with every
 * switch off.
 */
#define T23X_AUTOZOOM_BYTES 36U
#define T23X_AUTOZOOM_MIN 64U

struct t23x_autozoom_req {
	uint32_t chan, scaler_en, scaler_w, scaler_h;
	uint32_t crop_en, crop_left, crop_top, crop_w, crop_h;
};

static inline uint32_t t23x_u32_get(const uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}

static inline void t23x_u32_put(uint8_t *p, uint32_t v)
{
	memcpy(p, &v, sizeof(v));
}

/* returns 0 and the new record in out (56 bytes), or -EINVAL */
static inline int t23x_autozoom_apply(uint8_t *out, const uint8_t *cfg,
				      uint32_t full_w, uint32_t full_h,
				      const struct t23x_autozoom_req *r,
				      uint32_t sensor_w, uint32_t sensor_h)
{
	uint32_t crop_w, crop_h;

	if (r->chan > 2U)
		return -EINVAL;
	memcpy(out, cfg, 56);
	if (r->crop_en == 1U) {
		crop_w = r->crop_w;
		crop_h = r->crop_h;
		if (crop_w < T23X_AUTOZOOM_MIN || crop_h < T23X_AUTOZOOM_MIN ||
		    (crop_w & 1U) || (crop_h & 1U) ||
		    r->crop_left > sensor_w || crop_w > sensor_w - r->crop_left ||
		    r->crop_top > sensor_h || crop_h > sensor_h - r->crop_top)
			return -EINVAL;
		out[3] = 1;
		t23x_u32_put(out + 0x10, r->crop_left);
		t23x_u32_put(out + 0x14, r->crop_top);
		t23x_u32_put(out + 0x18, crop_w);
		t23x_u32_put(out + 0x1c, crop_h);
	} else {
		crop_w = full_w;
		crop_h = full_h;
		t23x_u32_put(out + 0x10, 0);
		t23x_u32_put(out + 0x14, 0);
		t23x_u32_put(out + 0x18, crop_w);
		t23x_u32_put(out + 0x1c, crop_h);
	}
	if (r->scaler_en == 1U) {
		if (r->scaler_w < T23X_AUTOZOOM_MIN ||
		    r->scaler_h < T23X_AUTOZOOM_MIN ||
		    (r->scaler_w & 1U) || (r->scaler_h & 1U) ||
		    r->scaler_w > crop_w || r->scaler_h > crop_h)
			return -EINVAL;
		out[4] = 1;
		t23x_u32_put(out + 0x20, r->scaler_w);
		t23x_u32_put(out + 0x24, r->scaler_h);
	} else {
		t23x_u32_put(out + 0x24, t23x_u32_get(out + 0x1c));
	}
	/* the width the stock code leaves must not end up above the window */
	if (t23x_u32_get(out + 0x20) > crop_w ||
	    t23x_u32_get(out + 0x24) > crop_h)
		return -EINVAL;
	return 0;
}

#endif /* TX_ISP_T23_TUNING_EXT_H */
