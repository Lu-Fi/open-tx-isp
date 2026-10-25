// SPDX-License-Identifier: GPL-2.0
/*
 * T32 adapter for the shared sensor registry (driver/common/tx_isp_sinfo.c)
 * and the module entry points.
 *
 * Offsets: struct tx_isp_sensor of the T32 4.4.94 SDK, measured with the
 * target toolchain (see tx_isp_t32_abi.h).  The wiring lives in the sensor's
 * inline register-info copy (T32_SENSOR_INFO).
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>

#include "../include/tx_isp/tx_isp_sinfo.h"
#include "tx_isp_t32_abi.h"

static const struct tx_isp_sinfo_config tx_isp_sinfo_config = {
	.flags = TX_ISP_SINFO_REGINFO_WIRING,
	.client_offset = T32_SD_DEV_PRIV,
	.attr_offset = T32_SENSOR_VIDEO + T32_VIDEO_ATTR,
	.width_offset = T32_SENSOR_VIDEO + T32_VIDEO_MBUS_WIDTH,
	.height_offset = T32_SENSOR_VIDEO + T32_VIDEO_MBUS_HEIGHT,
	.fps_offset = T32_SENSOR_VIDEO + T32_VIDEO_FPS,
	.adapter_nr_offset = T32_SENSOR_INFO + 0x3c,
	.attr_name_offset = 0,
	.attr_chip_id_offset = 4,
	.info_offset = T32_SENSOR_INFO,
	.info_mclk_offset = 0x64,
	.info_boot_offset = 0x68,
	.info_interface_offset = 0x60,
	.info_rst_gpio_offset = 0x48,
	.info_pwdn_gpio_offset = 0x4c,
};

#include "../common/tx_isp_sinfo.c"

#include "tx_isp_t32.h"

static int __init tx_isp_t32_init(void)
{
	int ret;

	ret = tx_isp_t32_core_init();
	if (ret)
		return ret;
	ret = tx_isp_sinfo_init();
	if (ret)
		tx_isp_t32_core_exit();
	return ret;
}

static void __exit tx_isp_t32_exit(void)
{
	tx_isp_sinfo_exit();
	tx_isp_t32_core_exit();
}

module_init(tx_isp_t32_init);
module_exit(tx_isp_t32_exit);
MODULE_DESCRIPTION("Open Ingenic T32 ISP driver (S1 skeleton)");
MODULE_LICENSE("GPL");
