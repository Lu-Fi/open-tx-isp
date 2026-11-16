/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 sensor control ops of the ISP core (the vendor's sensor_alloc_analog_gain, sensor_set_analog_gain,
 * sensor_set_integration_time, sensor_fps_control).  core is the block the vendor keeps in g_ispcore: per
 * channel a record of 104 bytes whose word at +372 points at the sensor attribute block, and a sensor update
 * block of 96 bytes at +1300.
 */
#ifndef TX_ISP_T33_R12E_H
#define TX_ISP_T33_R12E_H

#include "tx_isp_t33_blk.h"

typedef u32 (*t33_sen_alloc_fn)(u32 value, u32 shift, u32 *out);

static inline u32 *t33_sen_w(u8 *p, u32 off)
{
	return (u32 *)(p + off);
}

static inline u16 *t33_sen_h(u8 *p, u32 off)
{
	return (u16 *)(p + off);
}

/* the sensor attribute block of channel ch */
static inline u8 *t33_sen_attr(u8 *core, u32 ch)
{
	return (u8 *)(unsigned long)*t33_sen_w(core, ch * 104U + 372U);
}

/* the sensor update block of channel ch */
static inline u8 *t33_sen_upd(u8 *core, u32 ch)
{
	return core + ch * 96U + 1300U;
}

/* sensor_alloc_analog_gain: the sensor's callback (attribute +284) rounds the gain, result to blk +4 */
static inline u32 t33_sensor_alloc_again(u8 *core, u32 gain, u8 *blk)
{
	u32 out = 0, ret;
	u8 *attr = t33_sen_attr(core, *t33_sen_w(blk, 0));
	t33_sen_alloc_fn fn = (t33_sen_alloc_fn)(unsigned long)*t33_sen_w(attr, 284);

	ret = fn(gain, 16, &out);
	*t33_sen_h(blk, 4) = (u16)out;
	return ret;
}

/* sensor_set_analog_gain */
static inline void t33_sensor_set_again(u8 *core, u32 gain, u8 *blk)
{
	u32 ch = *t33_sen_w(blk, 0);
	u8 *attr = t33_sen_attr(core, ch);
	u8 *upd = t33_sen_upd(core, ch);

	*t33_sen_w(attr, 248) = *t33_sen_h(attr, 248) | gain << 16;
	if (gain == *t33_sen_w(attr, 152))
		return;
	*t33_sen_w(attr, 152) = gain;
	*t33_sen_w(upd, 0) = 1;
	*t33_sen_w(upd, 4) = gain;
	*t33_sen_w(upd, 48) = 1;
	*t33_sen_w(upd, 52) = *t33_sen_w(attr, 248);
}

/* sensor_set_integration_time (channel word at blk +8) */
static inline void t33_sensor_set_itime(u8 *core, u32 value, u8 *blk)
{
	u32 ch = *t33_sen_w(blk, 8);
	u8 *attr = t33_sen_attr(core, ch);
	u8 *upd = t33_sen_upd(core, ch);
	u32 val = value & 0xffffU;

	*t33_sen_w(attr, 248) = (*t33_sen_w(attr, 248) & 0xffff0000U) + val;
	if (val == *t33_sen_w(attr, 216))
		return;
	*t33_sen_w(attr, 216) = val;
	*t33_sen_w(upd, 24) = 1;
	*t33_sen_w(upd, 28) = val;
	*t33_sen_w(upd, 48) = 1;
	*t33_sen_w(upd, 52) = *t33_sen_w(attr, 248);
}

/* sensor_fps_control: the sensor limits into blk, returns the channel record's word at +388 */
static inline u32 t33_sensor_fps_ctl(u8 *core, u8 *blk)
{
	u32 ch = *t33_sen_w(blk, 8);
	u8 *rec = core + ch * 104U;
	u8 *p = (u8 *)(unsigned long)*t33_sen_w(rec, 372);

	*t33_sen_h(blk, 0) = *t33_sen_h(p, 276);
	*t33_sen_h(blk, 2) = *t33_sen_h(p, 278);
	*t33_sen_w(blk, 68) = *t33_sen_w(p, 220);
	*t33_sen_w(blk, 72) = *t33_sen_w(p, 224);
	*t33_sen_w(blk, 88) = *t33_sen_w(p, 236);
	*t33_sen_w(blk, 92) = *t33_sen_w(p, 240);
	return *t33_sen_w(rec, 388);
}

#endif /* TX_ISP_T33_R12E_H */
