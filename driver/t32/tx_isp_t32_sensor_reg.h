/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 sensor subdev: input enumeration and I2C sensor registration from the
 * vendor module (Matt Davis' recovered C, PR #15): subdev_sensor_ops_enum_input
 * (0x65a0) and subdev_sensor_register_i2c_device (0x6448).
 *
 * Sensor subdev (vendor offsets): sensor list head +0x120, mutex +0x138,
 * index mode +0x188 (1: the index is the sensor's own +0x194 u16, else the
 * list position).  Sensor: index +0x124, input type +0x128, list node +0x130,
 * name +0x138 (32 bytes), own index +0x194 (u16).  Input (v4l2_input):
 * index +0, name +4 (32 bytes), type +36.  Register info: name +36 (20 bytes),
 * address +56, adapter number +60, sensor id +92 (u16).  The I2C calls and the
 * mutex are hooks of struct t32_sreg_os.
 *
 * Vendor quirks kept: enum_input copies the name like strncpy without the
 * zero padding; when no sensor matches, the loop cursor ends on the list
 * head, so the word at subdev +0x114 (where a sensor's index would be) is
 * compared with the wanted index and a match there returns 0 although
 * nothing was filled in.  register_i2c_device returns 0 on every failure and
 * keeps the adapter reference on success.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_sreg_oracle).
 */
#ifndef TX_ISP_T32_SENSOR_REG_H
#define TX_ISP_T32_SENSOR_REG_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_SSD_LIST		0x120U
#define T32_SSD_MUTEX		0x138U
#define T32_SSD_IDXMODE		0x188U
#define T32_SEN_INDEX		0x124U
#define T32_SEN_TYPE		0x128U
#define T32_SEN_NODE		0x130U
#define T32_SEN_NAME		0x138U
#define T32_SEN_OWNIDX		0x194U
#define T32_INP_NAME		4U
#define T32_INP_TYPE		36U
#define T32_NAME_LEN		32U
#define T32_RINFO_NAME		36U
#define T32_RINFO_ADDR		56U
#define T32_RINFO_ADAPTER	60U
#define T32_RINFO_ID		92U
#define T32_I2C_TYPE_LEN	20U
#define T32_I2C_INFO_BYTES	44U
#define T32_I2C_INFO_ADDR	22U

struct t32_sreg_os {
	void (*mutex_lock)(u8 *m);
	void (*mutex_unlock)(u8 *m);
	u32 (*i2c_get_adapter)(u32 nr);
	u32 (*new_subdev_board)(u32 adapter, u8 *info, u32 pdata);	/* isp_i2c_new_subdev_board */
	void (*i2c_put_adapter)(u32 adapter);
};

static inline int t32_subdev_sensor_ops_enum_input(const struct t32_sreg_os *os, u8 *sd, u8 *input)
{
	u8 *head, *sen;
	u32 pos = 0;

	if (!sd || !input)
		return -22;
	os->mutex_lock(sd + T32_SSD_MUTEX);
	head = sd + T32_SSD_LIST;
	for (sen = T32_PTR(t32_g_rd32(head, 0) - T32_SEN_NODE); sen + T32_SEN_NODE != head;
	     sen = T32_PTR(t32_g_rd32(sen, T32_SEN_NODE) - T32_SEN_NODE)) {
		u32 i;

		if (t32_g_rd32(sd, T32_SSD_IDXMODE) == 1)
			t32_g_wr32(sen, T32_SEN_INDEX, (u32)sen[T32_SEN_OWNIDX] | (u32)sen[T32_SEN_OWNIDX + 1] << 8);
		else
			t32_g_wr32(sen, T32_SEN_INDEX, pos++);
		if (t32_g_rd32(sen, T32_SEN_INDEX) != t32_g_rd32(input, 0))
			continue;
		t32_g_wr32(input, T32_INP_TYPE, t32_g_rd32(sen, T32_SEN_TYPE));
		for (i = 0; i < T32_NAME_LEN; i++) {
			input[T32_INP_NAME + i] = sen[T32_SEN_NAME + i];
			if (!sen[T32_SEN_NAME + i])
				break;
		}
		break;
	}
	os->mutex_unlock(sd + T32_SSD_MUTEX);
	return t32_g_rd32(sen, T32_SEN_INDEX) == t32_g_rd32(input, 0) ? 0 : -22;
}

static inline u32 t32_subdev_sensor_register_i2c_device(const struct t32_sreg_os *os, u8 *ri)
{
	static const char fn[] = "subdev_sensor_register_i2c_device";
	u8 info[T32_I2C_INFO_BYTES];
	u32 adap, sd;

	adap = os->i2c_get_adapter(t32_g_rd32(ri, T32_RINFO_ADAPTER));
	if (!adap) {
		isp_printf(2, "[ %s:%d ] Failed to get I2C adapter %d, deferring probe!!!\n", fn, 99,
			   t32_g_rd32(ri, T32_RINFO_ADAPTER));
		return 0;
	}
	memset(info, 0, sizeof(info));
	memcpy(info, ri + T32_RINFO_NAME, T32_I2C_TYPE_LEN);
	info[T32_I2C_INFO_ADDR] = (u8)t32_g_rd32(ri, T32_RINFO_ADDR);
	info[T32_I2C_INFO_ADDR + 1] = (u8)(t32_g_rd32(ri, T32_RINFO_ADDR) >> 8);
	isp_printf(0, "[ %s:%d ] sensor id is %d, i2c name size is %d, type is %s, addr is 0x%02x, adapter id is %d\n",
		   fn, 105, (u32)ri[T32_RINFO_ID] | (u32)ri[T32_RINFO_ID + 1] << 8, T32_I2C_TYPE_LEN, (char *)info,
		   t32_g_rd32(ri, T32_RINFO_ADDR), t32_g_rd32(ri, T32_RINFO_ADAPTER));
	sd = os->new_subdev_board(adap, info, 0);
	if (sd && sd < (u32)-4095)
		return sd;
	os->i2c_put_adapter(adap);
	isp_printf(2, "[ %s:%d ] Failed to acquire subdev %s, deferring probe\n", fn, 110, (char *)(ri + T32_RINFO_NAME));
	return 0;
}

#endif
