/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TX_ISP_T41_V4L2_SENSOR_H__
#define __TX_ISP_T41_V4L2_SENSOR_H__

struct i2c_adapter;

#ifdef CONFIG_TX_ISP_T41_V4L2

void *tx_isp_t41_v4l2_sensor_get(struct i2c_adapter *adapter,
				 const char *name);
void tx_isp_t41_v4l2_sensor_put(void *sd);
int tx_isp_t41_v4l2_sensor_is(void *sd);
void tx_isp_t41_v4l2_sensor_publish(void *sd);
void tx_isp_t41_v4l2_sensor_unpublish(void *sd);

#else

static inline void *tx_isp_t41_v4l2_sensor_get(struct i2c_adapter *adapter,
					       const char *name)
{
	(void)adapter;
	(void)name;
	return NULL;
}

static inline void tx_isp_t41_v4l2_sensor_put(void *sd)
{
	(void)sd;
}

static inline int tx_isp_t41_v4l2_sensor_is(void *sd)
{
	(void)sd;
	return 0;
}

static inline void tx_isp_t41_v4l2_sensor_publish(void *sd)
{
	(void)sd;
}

static inline void tx_isp_t41_v4l2_sensor_unpublish(void *sd)
{
	(void)sd;
}

#endif

#endif /* __TX_ISP_T41_V4L2_SENSOR_H__ */
