/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TX_ISP_V4L2_SENSOR_H__
#define __TX_ISP_V4L2_SENSOR_H__

struct i2c_adapter;
struct tx_isp_subdev;

struct tx_isp_subdev *tx_isp_v4l2_sensor_get(struct i2c_adapter *adapter,
					     const char *name);
void tx_isp_v4l2_sensor_put(struct tx_isp_subdev *sd);

#endif /* __TX_ISP_V4L2_SENSOR_H__ */
