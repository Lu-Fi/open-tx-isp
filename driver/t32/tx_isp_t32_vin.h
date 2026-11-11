/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIN (video input: the sensors behind the ISP) stream control from the
 * vendor module (Matt Davis' recovered C, PR #15): vin_set_stream (0x4930),
 * vin_s_stream (0x4b90), vin_oneshot_allsensor_stream (0x4ba0) and
 * vin_core_ops_ioctl (0x614c).
 *
 * VIN device (vendor offsets): notify +0x98, list of registered sensors
 * +0x120 (node at sensor entry +0x130, entry word +0x124), sensor subdevs
 * +0x128 + idx * 4, VIN mutex +0x138, per sensor mutex +0x144 + idx * 12,
 * per sensor stream state +0x174 + idx * 4 (3 off, 4 on), +0x188 (>= 2: the
 * sensors get the stream info words before the first start), one-shot flag
 * +0x1f4.  Sensor subdev: ops +0x108 (core ops +0, video ops +4; core +4 is
 * called by ioctl 0x1000000, core +12 with the stream info, video +0 is
 * s_stream), "stream info sent" +0x12c, info block +0x370 (count +0x144).
 * Request: { enable, sensor index }.
 *
 * Vendor quirks kept: no index check; the index is re-read from the request
 * at every use (the vendor code reloads it after each call); when the
 * one-shot flag is 1 and no force is given, a start is ignored (return 0);
 * without a sensor subdev only the state word follows the request; a start sends the info words once per sensor
 * and returns the result of the last one (or of s_stream); the info words
 * are mode << 16 | idx << 12 | n << 4 | 1 and idx.  ioctl 0x1000000 maps
 * -ENOIOCTLCMD (-515) to 0.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_vin_oracle).
 */
#ifndef TX_ISP_T32_VIN_H
#define TX_ISP_T32_VIN_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_VIN_LIST		0x120U
#define T32_VIN_SENSOR(i)	(0x128U + (i) * 4U)
#define T32_VIN_MUTEX		0x138U
#define T32_VIN_SMUTEX(i)	(0x144U + (i) * 12U)
#define T32_VIN_STATE(i)	(0x174U + (i) * 4U)
#define T32_VIN_MODE		0x188U
#define T32_VIN_ONESHOT		0x1f4U
#define T32_VIN_ST_OFF		3U
#define T32_VIN_ST_ON		4U
#define T32_VSE_NODE		0x130U	/* sensor list entry: node, word +0x124 */
#define T32_VSE_WORD		0x124U
#define T32_VSD_OPS		0x108U
#define T32_VSD_INFO_SENT	0x12cU
#define T32_VSD_INFO		0x370U
#define T32_VSD_INFO_COUNT	0x144U
#define T32_VIN_ENOIOCTL	(-515)

#define T32_VIN_IOC_INIT	0x1000000U
#define T32_VIN_IOC_EVENT	0x1000003U
#define T32_VIN_IOC_ONESHOT_ON	0x100000eU	/* flag 0, start all */
#define T32_VIN_IOC_ONESHOT_OFF	0x100000fU	/* flag 1, stop all */
#define T32_VIN_IOC_START_ALL	0x1000010U
#define T32_VIN_EVENT_CMD	0x200001bU

struct t32_vin_os {
	void (*lock)(u8 *mutex);	/* private_mutex_lock */
	void (*unlock)(u8 *mutex);	/* private_mutex_unlock */
};

typedef int (*t32_vin_sd_fn)(u8 *sd, void *arg);
typedef int (*t32_vin_notify_fn)(u8 *sd, u32 cmd, void *arg);

/* sensor op: ops +0x108 -> table at ops_off -> function at fn_off (0 = missing) */
static inline u32 t32_vin_sd_op(const u8 *sd, u32 ops_off, u32 fn_off)
{
	u8 *ops = T32_PTR(t32_g_rd32(sd, T32_VSD_OPS));
	u32 tab = t32_g_rd32(ops, ops_off);

	return tab ? t32_g_rd32(T32_PTR(tab), fn_off) : 0;
}

static inline int t32_vin_set_stream(const struct t32_vin_os *os, u8 *vin, const u32 *req, u32 force)
{
	u8 *sd = T32_PTR(t32_g_rd32(vin, T32_VIN_SENSOR(req[1])));
	u32 en, state, fn, video, n, k, word;
	u32 arg[2];
	int ret;

	os->lock(vin + T32_VIN_MUTEX);
	en = req[0];
	state = t32_g_rd32(vin, T32_VIN_STATE(req[1]));
	if ((en && state == T32_VIN_ST_ON) || (!en && state != T32_VIN_ST_ON))
		goto out_ok;	/* already in the requested state */
	if (!force && t32_g_rd32(vin, T32_VIN_ONESHOT) == 1) {
		if (en)
			goto out_ok;
		if (!sd)
			goto set_state;
	} else if (!sd) {
		goto set_state;
	}
	os->lock(vin + T32_VIN_SMUTEX(req[1]));
	ret = T32_VIN_ENOIOCTL;
	video = t32_g_rd32(T32_PTR(t32_g_rd32(sd, T32_VSD_OPS)), 4);
	if (video) {
		fn = t32_g_rd32(T32_PTR(video), 0);
		if (fn)
			ret = ((t32_vin_sd_fn)T32_PTR(fn))(sd, (void *)req);
	}
	if (t32_g_rd32(vin, T32_VIN_MODE) >= 2 && !t32_g_rd32(sd, T32_VSD_INFO_SENT)) {
		if (req[0]) {
			/* the stream info words, one per info entry of the sensor */
			n = t32_g_rd32(T32_PTR(t32_g_rd32(sd, T32_VSD_INFO)), T32_VSD_INFO_COUNT);
			word = t32_g_rd32(vin, T32_VIN_MODE) << 16 | 1 | req[1] << 12;
			ret = 0;
			for (k = 0; k < n; k++) {
				arg[1] = req[1];
				arg[0] = k << 4 | word;
				ret = T32_VIN_ENOIOCTL;
				fn = t32_vin_sd_op(sd, 0, 12);
				if (fn)
					ret = ((t32_vin_sd_fn)T32_PTR(fn))(sd, arg);
			}
		} else {
			ret = 0;
		}
		t32_g_wr32(sd, T32_VSD_INFO_SENT, 1);
	}
	os->unlock(vin + T32_VIN_SMUTEX(req[1]));
	if (ret) {
		os->unlock(vin + T32_VIN_MUTEX);
		return ret;
	}
set_state:
	t32_g_wr32(vin, T32_VIN_STATE(req[1]), req[0] ? T32_VIN_ST_ON : T32_VIN_ST_OFF);
out_ok:
	os->unlock(vin + T32_VIN_MUTEX);
	return 0;
}

static inline int t32_vin_s_stream(const struct t32_vin_os *os, u8 *vin, const u32 *req)
{
	return t32_vin_set_stream(os, vin, req, 0);
}

static inline int t32_vin_oneshot_allsensor_stream(const struct t32_vin_os *os, u8 *vin, u32 enable)
{
	u32 req[2] = { enable, 0 };
	u32 i;

	for (i = 0; i < 4; i++) {
		req[1] = i;
		t32_vin_set_stream(os, vin, req, 1);
	}
	return 0;
}

static inline int t32_vin_core_ops_ioctl(const struct t32_vin_os *os, u8 *vin, u32 cmd, u32 *arg)
{
	u8 *sd, *head, *ent;
	u32 fn, ev[3];
	int ret;

	switch (cmd) {
	case T32_VIN_IOC_ONESHOT_ON:
		t32_g_wr32(vin, T32_VIN_ONESHOT, 0);
		t32_vin_oneshot_allsensor_stream(os, vin, 1);
		return 0;
	case T32_VIN_IOC_ONESHOT_OFF:
		t32_g_wr32(vin, T32_VIN_ONESHOT, 1);
		t32_vin_oneshot_allsensor_stream(os, vin, 0);
		return 0;
	case T32_VIN_IOC_START_ALL:
		t32_vin_oneshot_allsensor_stream(os, vin, 1);
		return 0;
	case T32_VIN_IOC_INIT:
		sd = T32_PTR(t32_g_rd32(vin, T32_VIN_SENSOR(arg[1])));
		if (!sd)
			return 0;
		os->lock(vin + T32_VIN_SMUTEX(arg[1]));
		ret = T32_VIN_ENOIOCTL;
		fn = t32_vin_sd_op(sd, 0, 4);
		if (fn)
			ret = ((t32_vin_sd_fn)T32_PTR(fn))(sd, arg);
		os->unlock(vin + T32_VIN_SMUTEX(arg[1]));
		return ret == T32_VIN_ENOIOCTL ? 0 : ret;
	case T32_VIN_IOC_EVENT:
		if (!vin || (uintptr_t)vin >= (uintptr_t)(u32)-4095) {
			isp_printf(2, "%s[%d]: invalid vin\n", "vin_core_ops_ioctl", 577);
			return -22;
		}
		head = vin + T32_VIN_LIST;
		for (ent = T32_PTR(t32_g_rd32(head, 0)) - T32_VSE_NODE; ent + T32_VSE_NODE != head;
		     ent = T32_PTR(t32_g_rd32(ent, T32_VSE_NODE)) - T32_VSE_NODE) {
			ev[0] = t32_g_rd32(ent, T32_VSE_WORD);
			ev[1] = 4;
			ev[2] = 0;
			fn = t32_g_rd32(vin, T32_G_SD_NOTIFY);
			if (!fn || ((t32_vin_notify_fn)T32_PTR(fn))(vin, T32_VIN_EVENT_CMD, ev)) {
				isp_printf(2, "%s[%d]: sensor event failed\n", "vin_core_ops_ioctl", 590);
				return -22;
			}
		}
		return 0;
	default:
		return 0;
	}
}

#endif
