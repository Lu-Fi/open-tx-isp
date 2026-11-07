/* T20 adapter for the shared Thingino sensor registry. */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>

#include <linux/version.h>
#include <linux/stddef.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-mediabus.h>

#include "../include/tx_isp/tx_isp_sinfo.h"
#include "tx-isp-common.h"

/*
 * The registry reads the driver's own structures, so derive the offsets from
 * the structures the driver is compiled against instead of carrying the
 * numbers recovered from the 3.10 vendor tx-isp-sinfo.o.  On other kernels
 * struct v4l2_subdev has a different size and the old numbers would point
 * into it.
 *
 * Based on a patch by Paul Philippov <paul@themactep.com>.
 */
#define T20_SENSOR_VIDEO_OFFSET(member) \
	(offsetof(struct tx_isp_sensor, video) + \
	 offsetof(struct tx_isp_video_in, member))

#define T20_SINFO_CLIENT_OFFSET \
	(offsetof(struct tx_isp_sensor, sd) + \
	 offsetof(struct v4l2_subdev, dev_priv))
#define T20_SINFO_ATTR_OFFSET		T20_SENSOR_VIDEO_OFFSET(attr)
#define T20_SINFO_WIDTH_OFFSET \
	(T20_SENSOR_VIDEO_OFFSET(mbus) + \
	 offsetof(struct v4l2_mbus_framefmt, width))
#define T20_SINFO_HEIGHT_OFFSET \
	(T20_SENSOR_VIDEO_OFFSET(mbus) + \
	 offsetof(struct v4l2_mbus_framefmt, height))
#define T20_SINFO_FPS_OFFSET		T20_SENSOR_VIDEO_OFFSET(fps)
#define T20_SINFO_ADAPTER_NR_OFFSET	offsetof(struct i2c_adapter, nr)
#define T20_SINFO_ATTR_NAME_OFFSET \
	offsetof(struct tx_isp_sensor_attribute, name)
#define T20_SINFO_ATTR_CHIP_ID_OFFSET \
	offsetof(struct tx_isp_sensor_attribute, chip_id)

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
/*
 * 3.10 build: the layout is the one the vendor tx-isp-sinfo.o was recovered
 * from (offsets from its instruction stream).  A drift in the struct
 * definitions must fail the build instead of silently changing /proc/jz/sensor.
 */
_Static_assert(T20_SINFO_CLIENT_OFFSET == 0x094, "T20 sinfo client offset");
_Static_assert(T20_SINFO_ATTR_OFFSET == 0x1c0, "T20 sinfo attr offset");
_Static_assert(T20_SINFO_WIDTH_OFFSET == 0x190, "T20 sinfo width offset");
_Static_assert(T20_SINFO_HEIGHT_OFFSET == 0x194, "T20 sinfo height offset");
_Static_assert(T20_SINFO_FPS_OFFSET == 0x1cc, "T20 sinfo fps offset");
_Static_assert(T20_SINFO_ADAPTER_NR_OFFSET == 0x0e0, "T20 sinfo adapter nr offset");
_Static_assert(T20_SINFO_ATTR_NAME_OFFSET == 0, "T20 sinfo attr name offset");
_Static_assert(T20_SINFO_ATTR_CHIP_ID_OFFSET == 4, "T20 sinfo attr chip_id offset");
#endif

static const struct tx_isp_sinfo_config tx_isp_sinfo_config = {
	.client_offset = T20_SINFO_CLIENT_OFFSET,
	.attr_offset = T20_SINFO_ATTR_OFFSET,
	.width_offset = T20_SINFO_WIDTH_OFFSET,
	.height_offset = T20_SINFO_HEIGHT_OFFSET,
	.fps_offset = T20_SINFO_FPS_OFFSET,
	.adapter_nr_offset = T20_SINFO_ADAPTER_NR_OFFSET,
	.attr_name_offset = T20_SINFO_ATTR_NAME_OFFSET,
	.attr_chip_id_offset = T20_SINFO_ATTR_CHIP_ID_OFFSET,
};

#include "../common/tx_isp_sinfo.c"
