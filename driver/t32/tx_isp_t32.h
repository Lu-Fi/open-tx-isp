/* SPDX-License-Identifier: GPL-2.0 */
/* T32 driver-internal declarations. */
#ifndef TX_ISP_T32_H
#define TX_ISP_T32_H

#include <linux/types.h>

struct i2c_driver;
struct platform_device;

extern int t32_print_level;

int isp_printf(unsigned int level, const char *fmt, ...);

/* tx_isp_t32_core.c */
int tx_isp_t32_core_init(void);
void tx_isp_t32_core_exit(void);
void t32_core_sensor_driver_gone(struct i2c_driver *drv);
int tx_isp_subdev_init(struct platform_device *pdev, void *sd, void *ops);
void tx_isp_subdev_deinit(void *sd);

/* tx_isp_t32_sinfo.c (shared registry, driver/common/tx_isp_sinfo.c) */
int tx_isp_sinfo_init(void);
void tx_isp_sinfo_exit(void);
int tx_isp_sinfo_sensor_bind(void *subdev, struct module *owner);
void tx_isp_sinfo_sensor_unbind(void *subdev, struct module *owner);

/* raw access to objects owned by the sensor modules */
#define T32_AT(base, off, type)		(*(type *)((char *)(base) + (off)))

#endif /* TX_ISP_T32_H */
