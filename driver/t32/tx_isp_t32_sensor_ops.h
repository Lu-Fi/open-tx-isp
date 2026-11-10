/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 sensor control ops of the ISP core (the table the tisp 3A calls to set
 * exposure / gains / fps on the sensor), from the vendor module (Matt Davis'
 * recovered C, PR #15): sensor_init (0x15444) fills the table, sensor_early_init
 * (0x15634) remembers the ISP core, and the ops sensor_alloc_* (6),
 * sensor_set_{integration_time,analog_gain,digital_gain}[_short] (6),
 * sensor_get_normal_fps, sensor_fps_control, sensor_set_fps, sensor_get_id,
 * sensor_set_mode, sensor_set_register, sensor_get_register and the empty ops
 * sensor_hw_reset_{enable,disable}, sensor_start_changes, sensor_end_changes,
 * sensor_set_wdr_mode, sensor_disable_isp (void) and sensor_read_black_pedestal,
 * sensor_get_lines_per_second (return 0), plus sensor_setting_init (0xbccd0,
 * return 0, not in the table).
 *
 * Objects (offsets as the vendor uses them; field names after the layout,
 * "max" / "delay" are assumed from the pattern value, ?, max, u16 delay):
 *   ISP core (t32_isp_core = t32_data_da760, a struct tx_isp_subdev): notify
 *     callback at +0x98, per channel a struct tx_isp_video_in at +0x144 +
 *     ch * 0x68 (width +0, height +4, attribute pointer +0x34, fps word
 *     num << 16 | den +0x44, +0x48, +0x4c) and a sensor update block at +0x518 +
 *     ch * 0x60 (pairs { update flag = 1, value }).
 *   sensor attribute (struct tx_isp_sensor_attribute): name pointer +0, chip id
 *     u16 +4, current gains / times at +0x98 .. +0xe8, the packed words
 *     it | again << 16 at +0xf8 (+0xfc short frame), total size u16 +0x114 /
 *     +0x116 and the sensor's alloc callbacks at +0x11c .. +0x130
 *     (u32 (*)(u32 value, u32 shift, u32 *out)).
 *   value block (argument of alloc_* and set_{analog,digital}_gain*): channel
 *     u32 +0, results u16 again +4, dgain +6, again_short +14, it +16,
 *     it_short +18, dgain_short +20.
 *   control block (argument of sensor_init, set_mode, fps_control, set_fps,
 *     get_normal_fps and set_integration_time*): channel u32 +8, the limits
 *     copied from the attribute, the ops table at +124 .. +228.
 * The vendor uses the channel word +8 for set_integration_time[_short] but +0
 * for alloc_integration_time[_short]; both kept.
 *
 * Vendor UB / quirks kept: no channel or pointer checks; alloc_again,
 * alloc_again_short, alloc_dgain call their callback without a NULL check;
 * alloc_integration_time_short tests the callback +0x12c (alloc_it) but calls
 * +0x130 (alloc_it_short), so a NULL +0x130 with a set +0x12c crashes;
 * get_normal_fps divides by the fps denominator (0 = divide by zero);
 * set_register / get_register dereference the core before their NULL check
 * (the -2 return is dead code); the 64 byte notify buffer of set/get_register
 * is stack memory in the vendor, only the bytes it sets are defined (ours
 * zeroes the rest).  Return values of the void ops and of sensor_init are
 * leftover registers in the vendor code and not reproduced.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_sensop_oracle).
 */
#ifndef TX_ISP_T32_SENSOR_OPS_H
#define TX_ISP_T32_SENSOR_OPS_H

#include "tx_isp_t32_glue.h"

extern u8 *t32_isp_core;	/* t32_data_da760, set by t32_sensor_early_init */
int isp_printf(unsigned int level, const char *fmt, ...);

/* ISP core */
#define T32_CORE_VIN(ch)	(0x144U + (ch) * 0x68U)	/* struct tx_isp_video_in */
#define T32_VIN_WIDTH		0x00U
#define T32_VIN_HEIGHT		0x04U
#define T32_VIN_ATTR		0x34U
#define T32_VIN_FPS		0x44U
#define T32_VIN_FPS_48		0x48U
#define T32_VIN_FPS_4C		0x4cU
#define T32_CORE_UPD(ch)	(0x518U + (ch) * 0x60U)	/* sensor update block */
#define T32_UPD_AGAIN		0x00U	/* flag, value at +4 */
#define T32_UPD_AGAIN_S		0x08U
#define T32_UPD_DGAIN		0x10U
#define T32_UPD_IT		0x18U
#define T32_UPD_IT_S		0x20U
#define T32_UPD_PACKED		0x30U	/* it | again << 16 */
#define T32_UPD_PACKED_S	0x38U
#define T32_UPD_DGAIN_S		0x40U
#define T32_UPD_FPS		0x48U

/* sensor attribute */
#define T32_SA_NAME		0x000U
#define T32_SA_CHIP_ID		0x004U	/* u16 */
#define T32_SA_AGAIN		0x098U
#define T32_SA_AGAIN_MAX	0x0a0U
#define T32_SA_AGAIN_DELAY	0x0a4U	/* u16 */
#define T32_SA_AGAIN_S		0x0a8U
#define T32_SA_AGAIN_S_MAX	0x0b0U
#define T32_SA_AGAIN_S_DELAY	0x0b4U
#define T32_SA_DGAIN		0x0b8U
#define T32_SA_DGAIN_MAX	0x0c0U
#define T32_SA_DGAIN_DELAY	0x0c4U
#define T32_SA_DGAIN_S		0x0c8U
#define T32_SA_DGAIN_S_MAX	0x0d0U
#define T32_SA_DGAIN_S_DELAY	0x0d4U
#define T32_SA_IT		0x0d8U
#define T32_SA_IT_MIN		0x0dcU
#define T32_SA_IT_MAX		0x0e0U
#define T32_SA_IT_DELAY		0x0e4U
#define T32_SA_IT_S		0x0e8U
#define T32_SA_IT_S_MIN		0x0ecU
#define T32_SA_IT_S_MAX		0x0f0U
#define T32_SA_PACKED		0x0f8U	/* it | again << 16 */
#define T32_SA_PACKED_S		0x0fcU
#define T32_SA_TOTAL_W		0x114U	/* u16 */
#define T32_SA_TOTAL_H		0x116U	/* u16 */
#define T32_SA_CB_AGAIN		0x11cU
#define T32_SA_CB_AGAIN_S	0x120U
#define T32_SA_CB_DGAIN		0x124U
#define T32_SA_CB_DGAIN_S	0x128U
#define T32_SA_CB_IT		0x12cU
#define T32_SA_CB_IT_S		0x130U

/* value block */
#define T32_SV_CH		0U
#define T32_SV_AGAIN		4U
#define T32_SV_DGAIN		6U
#define T32_SV_AGAIN_S		14U
#define T32_SV_IT		16U
#define T32_SV_IT_S		18U
#define T32_SV_DGAIN_S		20U

/* control block */
#define T32_SC_TOTAL_W		0U	/* u16 */
#define T32_SC_TOTAL_H		2U
#define T32_SC_WIDTH		4U	/* u16, set_mode */
#define T32_SC_HEIGHT		6U
#define T32_SC_CH		8U
#define T32_SC_AGAIN_MAX	36U
#define T32_SC_DGAIN_MAX	40U
#define T32_SC_FPS		56U
#define T32_SC_FPS_48		60U
#define T32_SC_FPS_4C		64U
#define T32_SC_IT_MIN		68U
#define T32_SC_IT_MAX		72U
#define T32_SC_IT_MAX_2		76U	/* set_mode */
#define T32_SC_IT_MAX_3		80U	/* set_mode */
#define T32_SC_IT_S_MIN		88U
#define T32_SC_IT_S_MAX		92U
#define T32_SC_AGAIN_S_MAX	96U
#define T32_SC_DGAIN_S_MAX	100U
#define T32_SC_IT_DELAY		116U	/* u8 */
#define T32_SC_AGAIN_DELAY	117U
#define T32_SC_DGAIN_DELAY	118U
#define T32_SC_AGAIN_S_DELAY	119U
#define T32_SC_DGAIN_S_DELAY	120U
#define T32_SC_MODE		122U	/* u8 */
/* ops table */
#define T32_OP_ALLOC_AGAIN	124U
#define T32_OP_ALLOC_AGAIN_S	128U
#define T32_OP_ALLOC_DGAIN	132U
#define T32_OP_ALLOC_DGAIN_S	136U
#define T32_OP_ALLOC_IT		140U
#define T32_OP_FPS_CONTROL	144U
#define T32_OP_ALLOC_IT_S	148U
#define T32_OP_LINES_PER_SEC	152U
#define T32_OP_NORMAL_FPS	156U
#define T32_OP_BLACK_PEDESTAL	160U
#define T32_OP_GET_ID		164U
#define T32_OP_SET_IT		168U
#define T32_OP_SET_IT_S		172U
#define T32_OP_START_CHANGES	176U
#define T32_OP_END_CHANGES	180U
#define T32_OP_SET_AGAIN	184U
#define T32_OP_SET_AGAIN_S	188U
#define T32_OP_SET_DGAIN	192U
#define T32_OP_SET_DGAIN_S	196U
#define T32_OP_SET_MODE		200U
#define T32_OP_SET_WDR_MODE	204U
#define T32_OP_DISABLE_ISP	208U
#define T32_OP_SET_FPS		212U
#define T32_OP_HW_RESET_DIS	216U
#define T32_OP_HW_RESET_EN	220U
#define T32_OP_GET_REGISTER	224U
#define T32_OP_SET_REGISTER	228U

/* notify commands */
#define T32_NOTIFY_SET_MODE	0x200000dU
#define T32_NOTIFY_SET_REG	0x2000011U
#define T32_NOTIFY_GET_REG	0x2000012U
#define T32_SENSOR_NOIOCTL	(-515)	/* -ENOIOCTLCMD */

typedef u32 (*t32_sensor_alloc_cb)(u32 value, u32 shift, u32 *out);
typedef int (*t32_sd_notify_fn)(u8 *sd, u32 cmd, void *arg);

static inline u8 *t32_sensor_vin(u32 ch)
{
	return t32_isp_core + T32_CORE_VIN(ch);
}

static inline u8 *t32_sensor_attr(u32 ch)
{
	return T32_PTR(t32_g_rd32(t32_sensor_vin(ch), T32_VIN_ATTR));
}

static inline u8 *t32_sensor_upd(u32 ch)
{
	return t32_isp_core + T32_CORE_UPD(ch);
}

/* request an update of the sensor: flag 1, then the value */
static inline void t32_sensor_request(u32 ch, u32 slot, u32 value)
{
	t32_g_wr32(t32_sensor_upd(ch), slot, 1);
	t32_g_wr32(t32_sensor_upd(ch), slot + 4, value);
}

/* ---- empty ops ---- */
static inline void t32_sensor_hw_reset_disable(void) {}
static inline void t32_sensor_hw_reset_enable(void) {}
static inline void t32_sensor_start_changes(void) {}
static inline void t32_sensor_end_changes(void) {}
static inline void t32_sensor_set_wdr_mode(void) {}
static inline void t32_sensor_disable_isp(void) {}
static inline int t32_sensor_read_black_pedestal(void) { return 0; }
static inline int t32_sensor_get_lines_per_second(void) { return 0; }
static inline int t32_sensor_setting_init(void) { return 0; }	/* 0xbccd0 */

/* ---- alloc: the sensor driver rounds the value, the result goes to the value block ---- */
static inline u32 t32_sensor_alloc_call(u32 value, u8 *val, u32 cb_off, u32 shift, u32 out_off)
{
	u32 out = 0, ret;

	ret = ((t32_sensor_alloc_cb)T32_PTR(t32_g_rd32(t32_sensor_attr(t32_g_rd32(val, T32_SV_CH)), cb_off)))(value, shift, &out);
	t32_g_wr16(val, out_off, (u16)out);
	return ret;
}

static inline u32 t32_sensor_alloc_analog_gain(u32 value, u8 *val)
{
	return t32_sensor_alloc_call(value, val, T32_SA_CB_AGAIN, 16, T32_SV_AGAIN);
}

static inline u32 t32_sensor_alloc_analog_gain_short(u32 value, u8 *val)
{
	return t32_sensor_alloc_call(value, val, T32_SA_CB_AGAIN_S, 16, T32_SV_AGAIN_S);
}

static inline u32 t32_sensor_alloc_digital_gain(u32 value, u8 *val)
{
	return t32_sensor_alloc_call(value, val, T32_SA_CB_DGAIN, 16, T32_SV_DGAIN);
}

static inline u32 t32_sensor_alloc_digital_gain_short(u32 value, u8 *val)
{
	if (!t32_g_rd32(t32_sensor_attr(t32_g_rd32(val, T32_SV_CH)), T32_SA_CB_DGAIN_S)) {
		t32_g_wr16(val, T32_SV_DGAIN_S, 0);
		return 0;
	}
	return t32_sensor_alloc_call(value, val, T32_SA_CB_DGAIN_S, 16, T32_SV_DGAIN_S);
}

static inline u32 t32_sensor_alloc_integration_time(u32 value, u8 *val)
{
	if (!t32_g_rd32(t32_sensor_attr(t32_g_rd32(val, T32_SV_CH)), T32_SA_CB_IT)) {
		t32_g_wr16(val, T32_SV_IT, (u16)value);
		return value;
	}
	return t32_sensor_alloc_call(value, val, T32_SA_CB_IT, 0, T32_SV_IT);
}

/* vendor: tests the alloc_it callback, calls alloc_it_short */
static inline u32 t32_sensor_alloc_integration_time_short(u32 value, u8 *val)
{
	if (!t32_g_rd32(t32_sensor_attr(t32_g_rd32(val, T32_SV_CH)), T32_SA_CB_IT)) {
		t32_g_wr16(val, T32_SV_IT_S, (u16)value);
		return value;
	}
	return t32_sensor_alloc_call(value, val, T32_SA_CB_IT_S, 0, T32_SV_IT_S);
}

/* ---- set: remember the value in the attribute, request the update once per change ---- */
static inline void t32_sensor_set_gain(u32 ch, u32 value, u32 packed_off, u32 cur_off,
				       u32 upd_slot, u32 packed_slot)
{
	u8 *attr = t32_sensor_attr(ch);

	t32_g_wr32(attr, packed_off, t32_g_rd16(attr, packed_off) | value << 16);
	if (value == t32_g_rd32(attr, cur_off))
		return;
	t32_g_wr32(attr, cur_off, value);
	t32_sensor_request(ch, upd_slot, value);
	t32_sensor_request(ch, packed_slot, t32_g_rd32(attr, packed_off));
}

static inline void t32_sensor_set_it(u32 ch, u32 value, u32 packed_off, u32 cur_off,
				     u32 upd_slot, u32 packed_slot)
{
	u8 *attr = t32_sensor_attr(ch);

	value &= 0xffff;
	t32_g_wr32(attr, packed_off, (t32_g_rd32(attr, packed_off) & 0xffff0000U) + value);
	if (value == t32_g_rd32(attr, cur_off))
		return;
	t32_g_wr32(attr, cur_off, value);
	t32_sensor_request(ch, upd_slot, value);
	t32_sensor_request(ch, packed_slot, t32_g_rd32(attr, packed_off));
}

static inline void t32_sensor_set_analog_gain(u32 value, u8 *val)
{
	t32_sensor_set_gain(t32_g_rd32(val, T32_SV_CH), value, T32_SA_PACKED, T32_SA_AGAIN,
			    T32_UPD_AGAIN, T32_UPD_PACKED);
}

static inline void t32_sensor_set_analog_gain_short(u32 value, u8 *val)
{
	t32_sensor_set_gain(t32_g_rd32(val, T32_SV_CH), value, T32_SA_PACKED_S, T32_SA_AGAIN_S,
			    T32_UPD_AGAIN_S, T32_UPD_PACKED_S);
}

static inline void t32_sensor_set_integration_time(u32 value, u8 *ctl)
{
	t32_sensor_set_it(t32_g_rd32(ctl, T32_SC_CH), value, T32_SA_PACKED, T32_SA_IT,
			  T32_UPD_IT, T32_UPD_PACKED);
}

static inline void t32_sensor_set_integration_time_short(u32 value, u8 *ctl)
{
	t32_sensor_set_it(t32_g_rd32(ctl, T32_SC_CH), value, T32_SA_PACKED_S, T32_SA_IT_S,
			  T32_UPD_IT_S, T32_UPD_PACKED_S);
}

static inline void t32_sensor_set_dgain(u32 ch, u32 value, u32 cur_off, u32 upd_slot)
{
	u8 *attr = t32_sensor_attr(ch);

	if (t32_g_rd32(attr, cur_off) == value)
		return;
	t32_g_wr32(attr, cur_off, value);
	t32_sensor_request(ch, upd_slot, value);
}

static inline void t32_sensor_set_digital_gain(u32 value, u8 *val)
{
	t32_sensor_set_dgain(t32_g_rd32(val, T32_SV_CH), value, T32_SA_DGAIN, T32_UPD_DGAIN);
}

static inline void t32_sensor_set_digital_gain_short(u32 value, u8 *val)
{
	t32_sensor_set_dgain(t32_g_rd32(val, T32_SV_CH), value, T32_SA_DGAIN_S, T32_UPD_DGAIN_S);
}

/* ---- fps / id / mode ---- */
/* fps word num << 16 | den as 8.8 fixed point (den 0: divide by zero, vendor UB) */
static inline u16 t32_sensor_get_normal_fps(u8 *ctl)
{
	u32 fps = t32_g_rd32(t32_sensor_vin(t32_g_rd32(ctl, T32_SC_CH)), T32_VIN_FPS);
	u32 num = fps >> 16, den = fps & 0xffff;

	return (u16)(((num / den) << 8) + ((num % den) << 8) / den);
}

static inline void t32_sensor_copy_limits(u8 *ctl, const u8 *attr)
{
	t32_g_wr16(ctl, T32_SC_TOTAL_W, t32_g_rd16(attr, T32_SA_TOTAL_W));
	t32_g_wr16(ctl, T32_SC_TOTAL_H, t32_g_rd16(attr, T32_SA_TOTAL_H));
	t32_g_wr32(ctl, T32_SC_IT_MIN, t32_g_rd32(attr, T32_SA_IT_MIN));
	t32_g_wr32(ctl, T32_SC_IT_MAX, t32_g_rd32(attr, T32_SA_IT_MAX));
	t32_g_wr32(ctl, T32_SC_IT_S_MIN, t32_g_rd32(attr, T32_SA_IT_S_MIN));
	t32_g_wr32(ctl, T32_SC_IT_S_MAX, t32_g_rd32(attr, T32_SA_IT_S_MAX));
}

static inline u32 t32_sensor_fps_control(u32 unused, u8 *ctl)
{
	u8 *vin = t32_sensor_vin(t32_g_rd32(ctl, T32_SC_CH));

	(void)unused;
	t32_sensor_copy_limits(ctl, T32_PTR(t32_g_rd32(vin, T32_VIN_ATTR)));
	return t32_g_rd32(vin, T32_VIN_FPS);
}

static inline void t32_sensor_set_fps(u32 fps, u8 *ctl)
{
	u32 ch = t32_g_rd32(ctl, T32_SC_CH);
	u8 *vin = t32_sensor_vin(ch);

	if (t32_g_rd32(vin, T32_VIN_FPS) == fps)
		return;
	t32_sensor_request(ch, T32_UPD_FPS, fps);
	t32_g_wr32(vin, T32_VIN_FPS, fps);
}

static inline u16 t32_sensor_get_id(u8 *val)
{
	return t32_g_rd16(t32_sensor_attr(t32_g_rd32(val, T32_SV_CH)), T32_SA_CHIP_ID);
}

static inline int t32_sensor_set_mode(u32 mode, u8 *ctl)
{
	u8 *sd = t32_isp_core, *vin, *attr;
	u8 arg = 1;
	int ret;

	if (!sd || !t32_g_rd32(sd, T32_G_SD_NOTIFY))
		return isp_printf(1, "sensor_set_mode: notify failed %d\n", arg);
	vin = t32_sensor_vin(t32_g_rd32(ctl, T32_SC_CH));
	attr = T32_PTR(t32_g_rd32(vin, T32_VIN_ATTR));
	ret = ((t32_sd_notify_fn)T32_PTR(t32_g_rd32(sd, T32_G_SD_NOTIFY)))(sd, T32_NOTIFY_SET_MODE, &arg);
	if (ret)
		return isp_printf(1, "sensor_set_mode: notify failed %d\n", arg);
	t32_g_wr16(ctl, T32_SC_WIDTH, (u16)t32_g_rd32(vin, T32_VIN_WIDTH));
	t32_g_wr16(ctl, T32_SC_HEIGHT, (u16)t32_g_rd32(vin, T32_VIN_HEIGHT));
	t32_sensor_copy_limits(ctl, attr);
	t32_g_wr32(ctl, T32_SC_IT_MAX_2, t32_g_rd32(attr, T32_SA_IT_MAX));
	ctl[T32_SC_MODE] = (u8)mode;
	t32_g_wr32(ctl, T32_SC_IT_MAX_3, t32_g_rd32(attr, T32_SA_IT_MAX));
	return (int)t32_g_rd32(attr, T32_SA_IT_MAX);
}

/* ---- sensor register access through the core's notify (struct of 64 bytes) ---- */
#define T32_REGARG_NAME		0U	/* 32 bytes of the sensor name */
#define T32_REGARG_CH		32U
#define T32_REGARG_REG		48U	/* u32 reg, u32 0 */
#define T32_REGARG_VAL		56U	/* u32 val, u32 0 */

static inline int t32_sensor_reg_notify(u8 *buf, u32 ch, u32 reg, u32 cmd)
{
	u8 *sd;

	memcpy(buf + T32_REGARG_NAME, T32_PTR(t32_g_rd32(t32_sensor_attr(ch), T32_SA_NAME)), 32);
	t32_g_wr32(buf, T32_REGARG_CH, ch);
	t32_g_wr32(buf, T32_REGARG_REG, reg);
	t32_g_wr32(buf, T32_REGARG_REG + 4, 0);
	sd = t32_isp_core;
	if (!sd)
		return -2;
	if (!t32_g_rd32(sd, T32_G_SD_NOTIFY))
		return T32_SENSOR_NOIOCTL;
	return ((t32_sd_notify_fn)T32_PTR(t32_g_rd32(sd, T32_G_SD_NOTIFY)))(sd, cmd, buf);
}

static inline int t32_sensor_set_register(const u32 *reg, const u32 *val, const u32 *ch)
{
	u8 buf[64] = { 0 };

	t32_g_wr32(buf, T32_REGARG_VAL, *val);
	return t32_sensor_reg_notify(buf, *ch, *reg, T32_NOTIFY_SET_REG);
}

static inline int t32_sensor_get_register(const u32 *reg, u32 *val, const u32 *ch)
{
	u8 buf[64] = { 0 };
	int ret = t32_sensor_reg_notify(buf, *ch, *reg, T32_NOTIFY_GET_REG);

	*val = t32_g_rd32(buf, T32_REGARG_VAL);
	return ret;
}

/* ---- table ---- */
static inline void t32_sensor_init(u8 *ctl, u32 ch)
{
	u8 *vin = t32_sensor_vin(ch);
	u8 *attr = T32_PTR(t32_g_rd32(vin, T32_VIN_ATTR));

	t32_g_wr32(ctl, T32_SC_AGAIN_MAX, t32_g_rd32(attr, T32_SA_AGAIN_MAX));
	t32_g_wr32(ctl, T32_SC_DGAIN_MAX, t32_g_rd32(attr, T32_SA_DGAIN_MAX));
	ctl[T32_SC_IT_DELAY] = (u8)t32_g_rd16(attr, T32_SA_IT_DELAY);
	ctl[T32_SC_AGAIN_DELAY] = (u8)t32_g_rd16(attr, T32_SA_AGAIN_DELAY);
	ctl[T32_SC_DGAIN_DELAY] = (u8)t32_g_rd16(attr, T32_SA_DGAIN_DELAY);
	ctl[T32_SC_AGAIN_S_DELAY] = (u8)t32_g_rd16(attr, T32_SA_AGAIN_S_DELAY);
	ctl[T32_SC_DGAIN_S_DELAY] = (u8)t32_g_rd16(attr, T32_SA_DGAIN_S_DELAY);
	t32_g_wr32(ctl, T32_SC_IT_MIN, t32_g_rd32(attr, T32_SA_IT_MIN));
	t32_g_wr32(ctl, T32_SC_IT_MAX, t32_g_rd32(attr, T32_SA_IT_MAX));
	t32_g_wr32(ctl, T32_SC_IT_S_MIN, t32_g_rd32(attr, T32_SA_IT_S_MIN));
	t32_g_wr32(ctl, T32_SC_IT_S_MAX, t32_g_rd32(attr, T32_SA_IT_S_MAX));
	t32_g_wr32(ctl, T32_SC_AGAIN_S_MAX, t32_g_rd32(attr, T32_SA_AGAIN_S_MAX));
	t32_g_wr32(ctl, T32_SC_DGAIN_S_MAX, t32_g_rd32(attr, T32_SA_DGAIN_S_MAX));
	t32_g_wr32(ctl, T32_SC_FPS, t32_g_rd32(vin, T32_VIN_FPS));
	t32_g_wr32(ctl, T32_SC_FPS_48, t32_g_rd32(vin, T32_VIN_FPS_48));
	t32_g_wr32(ctl, T32_SC_FPS_4C, t32_g_rd32(vin, T32_VIN_FPS_4C));
	t32_g_wr16(ctl, T32_SC_TOTAL_W, t32_g_rd16(attr, T32_SA_TOTAL_W));
	t32_g_wr16(ctl, T32_SC_TOTAL_H, t32_g_rd16(attr, T32_SA_TOTAL_H));
	t32_g_wr32(ctl, T32_OP_HW_RESET_DIS, T32_WORD(t32_sensor_hw_reset_disable));
	t32_g_wr32(ctl, T32_OP_HW_RESET_EN, T32_WORD(t32_sensor_hw_reset_enable));
	t32_g_wr32(ctl, T32_OP_ALLOC_AGAIN, T32_WORD(t32_sensor_alloc_analog_gain));
	t32_g_wr32(ctl, T32_OP_ALLOC_AGAIN_S, T32_WORD(t32_sensor_alloc_analog_gain_short));
	t32_g_wr32(ctl, T32_OP_ALLOC_DGAIN, T32_WORD(t32_sensor_alloc_digital_gain));
	t32_g_wr32(ctl, T32_OP_ALLOC_DGAIN_S, T32_WORD(t32_sensor_alloc_digital_gain_short));
	t32_g_wr32(ctl, T32_OP_ALLOC_IT, T32_WORD(t32_sensor_alloc_integration_time));
	t32_g_wr32(ctl, T32_OP_ALLOC_IT_S, T32_WORD(t32_sensor_alloc_integration_time_short));
	t32_g_wr32(ctl, T32_OP_SET_IT, T32_WORD(t32_sensor_set_integration_time));
	t32_g_wr32(ctl, T32_OP_SET_IT_S, T32_WORD(t32_sensor_set_integration_time_short));
	t32_g_wr32(ctl, T32_OP_START_CHANGES, T32_WORD(t32_sensor_start_changes));
	t32_g_wr32(ctl, T32_OP_END_CHANGES, T32_WORD(t32_sensor_end_changes));
	t32_g_wr32(ctl, T32_OP_SET_AGAIN, T32_WORD(t32_sensor_set_analog_gain));
	t32_g_wr32(ctl, T32_OP_SET_AGAIN_S, T32_WORD(t32_sensor_set_analog_gain_short));
	t32_g_wr32(ctl, T32_OP_SET_DGAIN, T32_WORD(t32_sensor_set_digital_gain));
	t32_g_wr32(ctl, T32_OP_SET_DGAIN_S, T32_WORD(t32_sensor_set_digital_gain_short));
	t32_g_wr32(ctl, T32_OP_NORMAL_FPS, T32_WORD(t32_sensor_get_normal_fps));
	t32_g_wr32(ctl, T32_OP_BLACK_PEDESTAL, T32_WORD(t32_sensor_read_black_pedestal));
	t32_g_wr32(ctl, T32_OP_SET_MODE, T32_WORD(t32_sensor_set_mode));
	t32_g_wr32(ctl, T32_OP_SET_WDR_MODE, T32_WORD(t32_sensor_set_wdr_mode));
	t32_g_wr32(ctl, T32_OP_FPS_CONTROL, T32_WORD(t32_sensor_fps_control));
	t32_g_wr32(ctl, T32_OP_GET_ID, T32_WORD(t32_sensor_get_id));
	t32_g_wr32(ctl, T32_OP_DISABLE_ISP, T32_WORD(t32_sensor_disable_isp));
	t32_g_wr32(ctl, T32_OP_LINES_PER_SEC, T32_WORD(t32_sensor_get_lines_per_second));
	t32_g_wr32(ctl, T32_OP_SET_FPS, T32_WORD(t32_sensor_set_fps));
	t32_g_wr32(ctl, T32_OP_GET_REGISTER, T32_WORD(t32_sensor_get_register));
	t32_g_wr32(ctl, T32_OP_SET_REGISTER, T32_WORD(t32_sensor_set_register));
}

static inline int t32_sensor_early_init(u8 *core)
{
	if (!core)
		return -22;
	if (!t32_isp_core)
		t32_isp_core = core;
	return 0;
}

#endif
