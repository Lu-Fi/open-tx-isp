/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 11: isp_i2c_new_subdev_board (0x634c).
 * Loads the sensor module (request_module with the board info), creates the i2c client and
 * returns the sensor subdev its driver attached as client data (0 on any failure; a client
 * without driver or subdev is unregistered again).  The driver is the client's word +100
 * (an i2c_driver embedded at +28 of the sensor driver structure, owner module at +8 of that).
 *
 * Vendor behaviour kept: the client's driver word is read before the NULL check of the client.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispi2c_oracle).
 */
#ifndef TX_ISP_T32_ISPI2C_H
#define TX_ISP_T32_ISPI2C_H

#include "tx_isp_t32_ispcore.h"

#define T32_I2C_INFO_ADDR	22	/* u16 */
#define T32_I2C_CLIENT_DRV	100
#define T32_I2C_DRV_EMBED	28
#define T32_I2C_DRV_OWNER	8

struct t32_i2c_ops {
	void (*request_module)(u32 wait, u32 info);
	void (*log)(u32 level, u32 line);
	u32 (*new_device)(u32 adap, u32 info);
	int (*try_module_get)(u32 mod);
	u32 (*get_clientdata)(u32 client);
	void (*module_put)(u32 mod);
	void (*unregister)(u32 client);
};

static inline u32 t32_isp_i2c_new_subdev_board(const struct t32_i2c_ops *ops, u32 adap, u32 info)
{
	u32 client, drv, sd;

	ops->request_module(1, info);
	if (!*(volatile u16 *)T32_IC_P(info + T32_I2C_INFO_ADDR)) {
		ops->log(1, 41);
		return 0;
	}
	client = ops->new_device(adap, info);
	drv = t32_ic_r32(client, T32_I2C_CLIENT_DRV);
	if (!client)
		return 0;
	if (drv != T32_I2C_DRV_EMBED && ops->try_module_get(t32_ic_r32(drv, T32_I2C_DRV_OWNER))) {
		sd = ops->get_clientdata(client);
		ops->module_put(t32_ic_r32(drv, T32_I2C_DRV_OWNER));
		if (sd)
			return sd;
	}
	ops->unregister(client);
	return 0;
}

#endif /* TX_ISP_T32_ISPI2C_H */
