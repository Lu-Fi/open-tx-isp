// SPDX-License-Identifier: GPL-2.0
/*
 * Mainline V4L2 sensor bridge for the T31 tx-isp driver.
 *
 * The recovered tx-isp driver expects a sensor subdev that implements the
 * Ingenic vendor sensor ABI (tx_isp_sensor_attribute, alloc_again, synchronous
 * register programming).  Mainline kernels expose sensors as V4L2 subdevices
 * owned by drivers/media/i2c.  This bridge finds the already probed V4L2
 * subdevice on the I2C bus and implements the vendor ABI on top of the
 * mainline subdev pad ops and controls, so tx-isp can use the in-tree driver
 * without a vendor sensor module.
 */


#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "include/tx_isp.h"
#include "include/tx_isp_device.h"
#include "tx_isp_v4l2_sensor.h"

#define TX_V4L2_NAME_LEN	32
#define TX_V4L2_CHIP_ID		0xcb3a
#define TX_V4L2_MAX_GAIN	327680
#define TX_V4L2_MIN_EXPOSURE	2
#define TX_V4L2_EXPOSURE_OFFSET	4

struct tx_isp_v4l2_sensor {
	struct tx_isp_sensor sensor;
	struct platform_device pdev;

	struct v4l2_subdev *vsd;
	struct i2c_client *client;
	struct module *owner;

	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;

	char name[TX_V4L2_NAME_LEN];
	u32 width;
	u32 height;
	u32 hts;
	u32 vts;
	u32 lanes;
	u64 link_freq_hz;
	u32 pixel_rate_hz;
};

static struct tx_isp_subdev *tx_isp_v4l2_sensor_sd;

/*
 * The mainline sc2336 driver maps the V4L2 analogue gain control to the
 * vendor gain table by index.  Keep the same ISP-gain to index mapping as
 * the vendor driver so the AE algorithm behaves identically.
 */
static const u32 tx_v4l2_gain_lut[] = {
	0, 2886, 5776, 8494, 11136, 13706, 16287, 18723,
	21097, 23414, 25746, 27953, 30109, 32217, 34345, 36361,
	38336, 40270, 42226, 44082, 45904, 47690, 49500, 51220,
	52910, 54571, 56254, 57857, 59433, 60984, 62558, 64059,
	65536, 68422, 71312, 74030, 76672, 79242, 81823, 84259,
	86633, 88950, 91282, 93489, 95645, 97753, 99881, 101897,
	103872, 105806, 107762, 109618, 111440, 113226, 115036, 116756,
	118446, 120107, 121790, 123393, 124969, 126520, 128094, 129595,
	131072, 133958, 136848, 139566, 142208, 144778, 147359, 149795,
	152169, 154486, 156818, 159025, 161181, 163289, 165417, 167433,
	169408, 171342, 173298, 175154, 176976, 178762, 180572, 182292,
	183982, 185643, 187326, 188929, 190505, 192056, 193630, 195131,
	196608, 199494, 202384, 205102, 207744, 210314, 212895, 215331,
	217705, 220022, 222354, 224561, 226717, 228825, 230953, 232969,
	234944, 236878, 238834, 240690, 242512, 244298, 246108, 247828,
	249518, 251179, 252862, 254465, 256041, 257592, 259166, 260667,
	262144, 265030, 267920, 270638, 273280, 275850, 278431, 280867,
	283241, 285558, 287890, 290097, 292253, 294361, 296489, 298505,
	300480, 302414, 304370, 306226, 308048, 309834, 311644, 313364,
	315054, 316715, 318398, 320001, 321577, 323128, 324702, 326203,
	327680,
};

static unsigned int tx_v4l2_alloc_again(unsigned int isp_gain,
					unsigned char shift,
					unsigned int *sensor_again)
{
	unsigned int i;

	if (isp_gain == 0) {
		*sensor_again = 0;
		return 0;
	}

	for (i = 1; i < ARRAY_SIZE(tx_v4l2_gain_lut); i++) {
		if (isp_gain < tx_v4l2_gain_lut[i]) {
			*sensor_again = i - 1;
			return tx_v4l2_gain_lut[i - 1];
		}
	}

	*sensor_again = ARRAY_SIZE(tx_v4l2_gain_lut) - 1;
	return tx_v4l2_gain_lut[ARRAY_SIZE(tx_v4l2_gain_lut) - 1];
}

static unsigned int tx_v4l2_alloc_dgain(unsigned int isp_gain,
					unsigned char shift,
					unsigned int *sensor_dgain)
{
	*sensor_dgain = 0;
	return 0;
}

static struct tx_isp_v4l2_sensor *sd_to_v4l2_sensor(struct tx_isp_subdev *sd)
{
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);

	if (!sensor)
		return NULL;

	return container_of(sensor, struct tx_isp_v4l2_sensor, sensor);
}

static int tx_v4l2_sensor_update(struct tx_isp_v4l2_sensor *s)
{
	struct tx_isp_sensor_attribute *attr = &s->sensor.attr;
	struct v4l2_subdev_state *state;
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		.pad = 0,
		.stream = 0,
	};
	int ret;

	state = v4l2_subdev_lock_and_get_active_state(s->vsd);
	if (!state)
		return -EIO;

	ret = v4l2_subdev_call(s->vsd, pad, get_fmt, state, &fmt);
	v4l2_subdev_unlock_state(state);
	if (ret) {
		dev_err(&s->client->dev, "get_fmt failed: %d\n", ret);
		return ret;
	}

	s->width = fmt.format.width;
	s->height = fmt.format.height;
	s->hts = s->width + (u32)v4l2_ctrl_g_ctrl(s->hblank);
	s->vts = s->height + (u32)v4l2_ctrl_g_ctrl(s->vblank);
	{
		s64 pixel_rate = v4l2_ctrl_g_ctrl_int64(s->pixel_rate);

		if (pixel_rate <= 0 || pixel_rate > U32_MAX)
			return -EINVAL;
		s->pixel_rate_hz = pixel_rate;
	}
	if (!s->link_freq->qmenu_int)
		return -EINVAL;
	s->link_freq_hz = s->link_freq->qmenu_int[v4l2_ctrl_g_ctrl(s->link_freq)];

	if (!s->pixel_rate_hz || !s->hts || !s->vts)
		return -EINVAL;

	attr->total_width = s->hts;
	attr->total_height = s->vts;
	attr->mipi.clk = DIV_ROUND_CLOSEST_ULL(s->link_freq_hz * 2, 1000000);
	attr->mipi.lans = s->lanes;
	attr->mipi.image_twidth = s->width;
	attr->mipi.image_theight = s->height;
	attr->max_integration_time_native = s->vts - TX_V4L2_EXPOSURE_OFFSET;
	attr->integration_time_limit = s->vts - TX_V4L2_EXPOSURE_OFFSET;
	attr->max_integration_time = s->vts - TX_V4L2_EXPOSURE_OFFSET;
	attr->one_line_expr_in_us =
		DIV_ROUND_CLOSEST_ULL((u64)s->hts * 1000000, s->pixel_rate_hz);

	s->sensor.video.mbus = fmt.format;
	s->sensor.video.vi_max_width = s->width;
	s->sensor.video.vi_max_height = s->height;
	s->sensor.video.fps = (30 << 16) | 1;

	return 0;
}

static int tx_v4l2_sensor_g_chip_ident(struct tx_isp_subdev *sd,
				       struct tx_isp_chip_ident *chip)
{
	struct tx_isp_sensor *sensor = tx_isp_get_subdev_hostdata(sd);
	struct tx_isp_sensor_attribute *attr;

	if (!sensor || !chip)
		return -EINVAL;

	attr = sensor->video.attr;
	if (!attr || !attr->name)
		return -EINVAL;

	strscpy(chip->name, attr->name, sizeof(chip->name));
	chip->ident = attr->chip_id;
	chip->revision = "v4l2";

	return 0;
}

static int tx_v4l2_sensor_init(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_v4l2_sensor *s = sd_to_v4l2_sensor(sd);
	struct tx_isp_sensor *sensor;
	int ret;

	if (!s)
		return -ENODEV;
	if (!enable)
		return 0;

	ret = tx_v4l2_sensor_update(s);
	if (ret)
		return ret;

	sensor = &s->sensor;
	return tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR,
					 &sensor->video);
}

static int tx_v4l2_sensor_reset(struct tx_isp_subdev *sd, int on)
{
	return 0;
}

static int tx_v4l2_sensor_s_stream(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_v4l2_sensor *s = sd_to_v4l2_sensor(sd);
	int ret;

	if (!s)
		return -ENODEV;

	if (enable) {
		ret = tx_v4l2_sensor_update(s);
		if (ret)
			return ret;
	}

	ret = v4l2_subdev_call(s->vsd, video, s_stream, enable);
	if (ret == -ENOIOCTLCMD)
		ret = 0;

	return ret;
}

static int tx_v4l2_sensor_set_fps(struct tx_isp_subdev *sd, int fps)
{
	struct tx_isp_v4l2_sensor *s = sd_to_v4l2_sensor(sd);
	struct tx_isp_sensor *sensor;
	struct tx_isp_sensor_attribute *attr;
	u32 num = (u32)fps >> 16;
	u32 den = (u32)fps & 0xffff;
	u64 vts;
	int ret;

	if (!s || !num || !den)
		return -EINVAL;

	vts = div_u64((u64)s->pixel_rate_hz * den, (u64)s->hts * num);
	if (vts <= s->height)
		return -EINVAL;

	ret = v4l2_ctrl_s_ctrl(s->vblank, vts - s->height);
	if (ret)
		return ret;

	s->vts = vts;
	sensor = &s->sensor;
	attr = &sensor->attr;
	attr->total_height = s->vts;
	attr->max_integration_time_native = s->vts - TX_V4L2_EXPOSURE_OFFSET;
	attr->integration_time_limit = s->vts - TX_V4L2_EXPOSURE_OFFSET;
	attr->max_integration_time = s->vts - TX_V4L2_EXPOSURE_OFFSET;

	return tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR,
					 &sensor->video);
}

static int tx_v4l2_sensor_set_expo(struct tx_isp_subdev *sd, int value)
{
	struct tx_isp_v4l2_sensor *s = sd_to_v4l2_sensor(sd);
	int it = value & 0xffff;
	int again = (value >> 16) & 0xffff;
	int ret;

	if (!s)
		return -ENODEV;

	if (it) {
		ret = v4l2_ctrl_s_ctrl(s->exposure, it);
		if (ret)
			return ret;
	}

	if (again) {
		ret = v4l2_ctrl_s_ctrl(s->gain, again);
		if (ret)
			return ret;
	}

	return 0;
}

static int tx_v4l2_sensor_set_vflip(struct tx_isp_subdev *sd, int enable)
{
	struct tx_isp_v4l2_sensor *s = sd_to_v4l2_sensor(sd);
	struct tx_isp_sensor *sensor;
	int ret;

	if (!s)
		return -ENODEV;

	ret = v4l2_ctrl_s_ctrl(s->hflip, !!(enable & 0x1));
	if (ret)
		return ret;

	ret = v4l2_ctrl_s_ctrl(s->vflip, !!(enable & 0x2));
	if (ret)
		return ret;

	sensor = &s->sensor;
	return tx_isp_call_subdev_notify(sd, TX_ISP_EVENT_SYNC_SENSOR_ATTR,
					 &sensor->video);
}

static int tx_v4l2_sensor_ioctl(struct tx_isp_subdev *sd, unsigned int cmd,
				void *arg)
{
	struct tx_isp_v4l2_sensor *s = sd_to_v4l2_sensor(sd);

	if (!s)
		return -ENODEV;

	switch (cmd) {
	case TX_ISP_EVENT_SENSOR_EXPO:
		return arg ? tx_v4l2_sensor_set_expo(sd, *(int *)arg) : 0;
	case TX_ISP_EVENT_SENSOR_FPS:
		return arg ? tx_v4l2_sensor_set_fps(sd, *(int *)arg) : 0;
	case TX_ISP_EVENT_SENSOR_VFLIP:
		return arg ? tx_v4l2_sensor_set_vflip(sd, *(int *)arg) : 0;
	case TX_ISP_EVENT_SENSOR_RESIZE:
		return tx_v4l2_sensor_update(s);
	case TX_ISP_EVENT_SENSOR_PREPARE_CHANGE: {
		int ret = v4l2_subdev_call(s->vsd, video, s_stream, 0);

		return ret == -ENOIOCTLCMD ? 0 : ret;
	}
	case TX_ISP_EVENT_SENSOR_FINISH_CHANGE: {
		int ret = v4l2_subdev_call(s->vsd, video, s_stream, 1);

		return ret == -ENOIOCTLCMD ? 0 : ret;
	}
	default:
		return 0;
	}
}

static struct tx_isp_subdev_core_ops tx_v4l2_sensor_core_ops = {
	.g_chip_ident = tx_v4l2_sensor_g_chip_ident,
	.init = tx_v4l2_sensor_init,
	.reset = tx_v4l2_sensor_reset,
};

static struct tx_isp_subdev_video_ops tx_v4l2_sensor_video_ops = {
	.s_stream = tx_v4l2_sensor_s_stream,
};

static struct tx_isp_subdev_sensor_ops tx_v4l2_sensor_sensor_ops = {
	.ioctl = tx_v4l2_sensor_ioctl,
};

static struct tx_isp_subdev_ops tx_v4l2_sensor_ops = {
	.core = &tx_v4l2_sensor_core_ops,
	.video = &tx_v4l2_sensor_video_ops,
	.sensor = &tx_v4l2_sensor_sensor_ops,
};

struct tx_v4l2_lookup {
	const char *name;
	struct i2c_client *client;
	struct v4l2_subdev *vsd;
};

static int tx_v4l2_lookup_client(struct device *dev, void *data)
{
	struct tx_v4l2_lookup *lookup = data;
	struct i2c_client *client;
	struct v4l2_subdev *vsd;

	client = i2c_verify_client(dev);
	if (!client)
		return 0;

	if (!client->dev.driver || !client->dev.of_node)
		return 0;

	if (strcmp(client->name, lookup->name))
		return 0;

	if (!client->dev.driver->of_match_table ||
	    !of_match_node(client->dev.driver->of_match_table,
			   client->dev.of_node))
		return 0;

	vsd = i2c_get_clientdata(client);
	if (!vsd || !vsd->ctrl_handler)
		return 0;

	lookup->client = client;
	lookup->vsd = vsd;

	return 1;
}

static int tx_v4l2_parse_lanes(struct tx_isp_v4l2_sensor *s)
{
	struct v4l2_fwnode_endpoint ep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *endpoint;
	int ret = 0;

	s->lanes = 2;

	endpoint = fwnode_graph_get_next_endpoint(s->client->dev.fwnode, NULL);
	if (!endpoint)
		return 0;

	if (!v4l2_fwnode_endpoint_parse(endpoint, &ep) &&
	    ep.bus.mipi_csi2.num_data_lanes)
		s->lanes = ep.bus.mipi_csi2.num_data_lanes;
	else
		ret = -EINVAL;

	fwnode_handle_put(endpoint);

	return ret;
}

static void tx_v4l2_sensor_free(struct tx_isp_v4l2_sensor *s)
{
	if (!s)
		return;

	if (s->owner)
		module_put(s->owner);

	kfree(s);
}

struct tx_isp_subdev *tx_isp_v4l2_sensor_get(struct i2c_adapter *adapter,
					     const char *name)
{
	struct tx_v4l2_lookup lookup = { .name = name };
	struct tx_isp_v4l2_sensor *s;
	struct tx_isp_sensor *sensor;
	struct tx_isp_sensor_attribute *attr;
	int ret;

	if (!adapter || !name || !name[0])
		return NULL;

	if (tx_isp_v4l2_sensor_sd) {
		s = sd_to_v4l2_sensor(tx_isp_v4l2_sensor_sd);
		if (s && !strcmp(s->name, name))
			return tx_isp_v4l2_sensor_sd;
	}

	device_for_each_child(&adapter->dev, &lookup, tx_v4l2_lookup_client);
	if (!lookup.vsd) {
		dev_err(&adapter->dev,
			"tx-isp-v4l2: no mainline V4L2 sensor named %s\n",
			name);
		return NULL;
	}

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return NULL;

	s->client = lookup.client;
	s->vsd = lookup.vsd;
	if (s->vsd->dev && s->vsd->dev->driver)
		s->owner = s->vsd->dev->driver->owner;
	if (s->owner && !try_module_get(s->owner)) {
		kfree(s);
		return NULL;
	}

	strscpy(s->name, name, sizeof(s->name));

	s->exposure = v4l2_ctrl_find(s->vsd->ctrl_handler, V4L2_CID_EXPOSURE);
	s->gain = v4l2_ctrl_find(s->vsd->ctrl_handler,
				 V4L2_CID_ANALOGUE_GAIN);
	s->hblank = v4l2_ctrl_find(s->vsd->ctrl_handler, V4L2_CID_HBLANK);
	s->vblank = v4l2_ctrl_find(s->vsd->ctrl_handler, V4L2_CID_VBLANK);
	s->hflip = v4l2_ctrl_find(s->vsd->ctrl_handler, V4L2_CID_HFLIP);
	s->vflip = v4l2_ctrl_find(s->vsd->ctrl_handler, V4L2_CID_VFLIP);
	s->link_freq = v4l2_ctrl_find(s->vsd->ctrl_handler,
				      V4L2_CID_LINK_FREQ);
	s->pixel_rate = v4l2_ctrl_find(s->vsd->ctrl_handler,
				       V4L2_CID_PIXEL_RATE);

	if (!s->exposure || !s->gain || !s->hblank || !s->vblank ||
	    !s->hflip || !s->vflip || !s->link_freq || !s->pixel_rate) {
		dev_err(&s->client->dev, "missing controls: exp=%p gain=%p hb=%p vb=%p hf=%p vf=%p lf=%p pr=%p\n",
			s->exposure, s->gain, s->hblank, s->vblank,
			s->hflip, s->vflip, s->link_freq, s->pixel_rate);
		tx_v4l2_sensor_free(s);
		return NULL;
	}

	if (tx_v4l2_parse_lanes(s)) {
		dev_err(&s->client->dev, "failed to parse MIPI lanes\n");
		tx_v4l2_sensor_free(s);
		return NULL;
	}

	sensor = &s->sensor;
	attr = &sensor->attr;

	attr->name = s->name;
	attr->chip_id = TX_V4L2_CHIP_ID;
	attr->cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
	attr->cbus_mask = V4L2_SBUS_MASK_SAMPLE_8BITS |
			  V4L2_SBUS_MASK_ADDR_16BITS;
	attr->cbus_device = s->client->addr;
	attr->dbus_type = TX_SENSOR_DATA_INTERFACE_MIPI;
	attr->mipi.mode = SENSOR_MIPI_OTHER_MODE;
	attr->mipi.settle_time_apative_en = 1;
	attr->mipi.mipi_sc.sensor_csi_fmt = TX_SENSOR_RAW10;
	attr->mipi.mipi_sc.data_type_value = RAW10;
	attr->mipi.mipi_sc.sensor_frame_mode = TX_SENSOR_DEFAULT_FRAME_MODE;
	attr->mipi.mipi_sc.sensor_mode = TX_SENSOR_DEFAULT_MODE;
	attr->data_type = TX_SENSOR_DATA_TYPE_LINEAR;
	attr->max_again = TX_V4L2_MAX_GAIN;
	attr->max_dgain = 0;
	attr->min_integration_time = TX_V4L2_MIN_EXPOSURE;
	attr->min_integration_time_native = TX_V4L2_MIN_EXPOSURE;
	attr->integration_time_apply_delay = 2;
	attr->again_apply_delay = 2;
	attr->sensor_ctrl.alloc_again = tx_v4l2_alloc_again;
	attr->sensor_ctrl.alloc_dgain = tx_v4l2_alloc_dgain;

	sensor->video.attr = attr;
	sensor->video.vi_max_width = s->width;
	sensor->video.vi_max_height = s->height;

	s->pdev.name = s->name;
	s->pdev.id = -1;

	ret = tx_isp_subdev_init(&s->pdev, &sensor->sd, &tx_v4l2_sensor_ops);
	if (ret) {
		dev_err(&s->client->dev, "tx_isp_subdev_init failed: %d\n",
			ret);
		tx_v4l2_sensor_free(s);
		return NULL;
	}
	tx_isp_set_subdevdata(&sensor->sd, NULL);
	tx_isp_set_subdev_hostdata(&sensor->sd, sensor);

	ret = tx_v4l2_sensor_update(s);
	if (ret) {
		dev_err(&s->client->dev, "initial sensor update failed: %d\n",
			ret);
		tx_isp_subdev_deinit(&sensor->sd);
		tx_v4l2_sensor_free(s);
		return NULL;
	}

	tx_isp_v4l2_sensor_sd = &sensor->sd;

	pr_info("attached mainline V4L2 sensor %s at i2c-%d/0x%02x (%ux%u, %u lanes, link %llu Hz)\n",
		name, adapter->nr, s->client->addr, s->width, s->height,
		s->lanes, s->link_freq_hz);

	return &sensor->sd;
}

void tx_isp_v4l2_sensor_put(struct tx_isp_subdev *sd)
{
	struct tx_isp_v4l2_sensor *s;

	if (!sd || sd != tx_isp_v4l2_sensor_sd)
		return;

	s = sd_to_v4l2_sensor(sd);
	tx_isp_v4l2_sensor_sd = NULL;

	if (!s)
		return;

	tx_isp_subdev_deinit(sd);
	tx_v4l2_sensor_free(s);
}

struct i2c_client *tx_isp_v4l2_sensor_client(struct tx_isp_subdev *sd)
{
	if (!sd || sd != tx_isp_v4l2_sensor_sd)
		return NULL;

	return sd_to_v4l2_sensor(sd)->client;
}
