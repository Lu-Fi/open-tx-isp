/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp ioctl helpers (vendor .isra / static helpers of
 * tx_isp_unlocked_ioctl): driver version, default bin path set / get,
 * sensor register / release, multi-sensor mode, AE algorithm open, video
 * stream on/off and link stream over the 16 subdevices of the ISP device
 * (+84 .. +144).
 *
 * ISP device: +60 "kernel caller" flag (argument is a kernel pointer:
 * memcpy instead of copy_from_user), +84 + 4 i subdevices.  Subdevice ops:
 * +0 core (+32 ioctl), +4 video (+0 s_stream, +4 link_stream), +12 sensor
 * (+8 ioctl).  Core device (+280 of the subdevice): +1836 / +1844 bin path
 * table, 544 bytes per input, 136 bytes per entry.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txioctl_oracle).
 */
#ifndef TX_ISP_T32_TXIOCTL_H
#define TX_ISP_T32_TXIOCTL_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txcore.h"

#define T32_EFAULT		14U
#define T32_ISP_KCALL		60U
#define T32_SD_PRIV_CORE	280U
#define T32_OPS_VIDEO		4U
#define T32_VIDEO_S_STREAM	0U
#define T32_VIDEO_LINK_STREAM	4U

#define T32_VERSION_BYTES	96U
#define T32_BINPATH_BYTES	136U
#define T32_BINPATH_SET		1836U
#define T32_BINPATH_GET		1844U
#define T32_BINPATH_INPUT	544U
#define T32_SENSOR_BUF		116U
#define T32_MULTI_BUF		108U
#define T32_AE_ALGO_BUF		136U
#define T32_AE_ALGO_VERSION	0x13501f4U

#define T32_SENSOR_REGISTER	0x2000000U
#define T32_SENSOR_RELEASE	0x2000001U
#define T32_SENSOR_MULTI	0x200001cU
#define T32_CORE_AE_ALGO	0x1000014U

typedef int (*t32_tx_cmd_fn)(u32 sd, u32 cmd, void *buf);
typedef int (*t32_tx_buf_fn)(u32 sd, void *buf);

/* the module's version block (vendor be5b0, data.c:1423): "PRJ007", +32 "20251107a" */
static const char t32_tx_version[T32_VERSION_BYTES] = {
	'P', 'R', 'J', '0', '0', '7', [32] = '2', '0', '2', '5', '1', '1', '0', '7', 'a'
};

/* provided by the driver glue (kernel) or by the oracle (host) */
u32 t32_tx_os_copy_from_user(void *dst, u32 user, u32 n);
u32 t32_tx_os_copy_to_user(u32 user, const void *src, u32 n);
void t32_tx_os_sprintf(char *dst, const char *fmt);
extern u32 t32_tx_stream_count[];	/* vendor da774: per input stream-on count (d9008 mode) */

/* user or kernel argument (+60) */
static inline u32 t32_tx_arg_in(u32 isp, void *dst, u32 arg, u32 n)
{
	if (t32_tx_ld(isp, T32_ISP_KCALL)) {
		memcpy(dst, T32_TX_P(arg), n);
		return 0;
	}
	return t32_tx_os_copy_from_user(dst, arg, n);
}

/* tx_isp_driver_version */
static inline int t32_tx_driver_version(u32 user)
{
	char v[T32_VERSION_BYTES];

	memcpy(v, t32_tx_version, sizeof(v));
	if (t32_tx_os_copy_to_user(user, v, T32_VERSION_BYTES)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy to user error\n", "tx_isp_driver_version", 1414);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/* the bin path entry { input, slot, path } of the core: input < 4; the vendor does not bound slot */
static inline u32 t32_tx_binpath(u32 isp, const u32 *b, u32 base)
{
	return t32_tx_ld(t32_tx_ld(isp, 0), T32_SD_PRIV_CORE) + b[0] * T32_BINPATH_INPUT +
	       b[1] * T32_BINPATH_BYTES + base;
}

/* tx_isp_set_default_bin_path(isp, user) */
static inline int t32_tx_set_default_bin_path(u32 isp, u32 user)
{
	u32 b[T32_BINPATH_BYTES / 4];

	if (t32_tx_os_copy_from_user(b, user, T32_BINPATH_BYTES)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_set_default_bin_path", 1657);
		return -(int)T32_EFAULT;
	}
	if (b[0] >= 4) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] vinum exceeds upper limit!!!\n", "tx_isp_set_default_bin_path", 1670);
		return 0;
	}
	memcpy(T32_TX_P(t32_tx_binpath(isp, b, T32_BINPATH_SET)), b, T32_BINPATH_BYTES);
	return 0;
}

/* tx_isp_get_default_bin_path(isp, user).  Vendor: the stored path is used as the sprintf format */
static inline int t32_tx_get_default_bin_path(u32 isp, u32 user)
{
	u32 b[T32_BINPATH_BYTES / 4];

	if (t32_tx_os_copy_from_user(b, user, T32_BINPATH_BYTES)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_get_default_bin_path", 1685);
		return -(int)T32_EFAULT;
	}
	if (b[0] >= 4) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] vinum exceeds upper limit!!!\n", "tx_isp_get_default_bin_path", 1703);
		return 0;
	}
	t32_tx_os_sprintf((char *)&b[2], (const char *)T32_TX_P(t32_tx_binpath(isp, b, T32_BINPATH_GET)));
	if (t32_tx_os_copy_to_user(user, b, T32_BINPATH_BYTES)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy to user error\n", "tx_isp_get_default_bin_path", 1698);
		return -(int)T32_EFAULT;
	}
	return 0;
}

/* the sensor ioctl (sensor ops +8) of every subdevice; an error other than -ENOIOCTLCMD stops */
static inline int t32_tx_sensor_cmd_all(u32 isp, u32 cmd, void *buf, int stop)
{
	u32 k;

	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(isp, T32_DEV_SD + 4 * k), sops, fn;
		int r;

		if (!sd || !(sops = t32_tx_ld(t32_tx_ld(sd, T32_SD_OPS), T32_OPS_SENSOR)) ||
		    !(fn = t32_tx_ld(sops, T32_SENSOR_IOCTL)))
			continue;
		r = ((t32_tx_cmd_fn)T32_TX_P(fn))(sd, cmd, buf);
		if (stop && r && r != -(int)T32_ENOIOCTLCMD)
			return r;
	}
	return 0;
}

/* tx_isp_sensor_register_sensor(isp, arg) */
static inline int t32_tx_sensor_register_sensor(u32 isp, u32 arg)
{
	u8 buf[T32_SENSOR_BUF];

	if (t32_tx_arg_in(isp, buf, arg, T32_SENSOR_BUF)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_sensor_register_sensor", 783);
		return -(int)T32_EFAULT;
	}
	return t32_tx_sensor_cmd_all(isp, T32_SENSOR_REGISTER, buf, 1);
}

/* tx_isp_sensor_release_sensor(isp, user): always a user pointer */
static inline int t32_tx_sensor_release_sensor(u32 isp, u32 user)
{
	u8 buf[T32_SENSOR_BUF];

	if (t32_tx_os_copy_from_user(buf, user, T32_SENSOR_BUF)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_sensor_release_sensor", 812);
		return -(int)T32_EFAULT;
	}
	return t32_tx_sensor_cmd_all(isp, T32_SENSOR_RELEASE, buf, 1);
}

/* tx_isp_multisensor_mode(isp, arg): callback results ignored */
static inline int t32_tx_multisensor_mode(u32 isp, u32 arg)
{
	u8 buf[T32_MULTI_BUF];

	if (t32_tx_arg_in(isp, buf, arg, T32_MULTI_BUF)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_multisensor_mode", 1341);
		return -(int)T32_EFAULT;
	}
	return t32_tx_sensor_cmd_all(isp, T32_SENSOR_MULTI, buf, 0);
}

/*
 * tx_isp_set_ae_algo_open(isp, user): 8 bytes in (version word), the core
 * ioctl of every subdevice, 136 bytes back.  Vendor: the 128 bytes after the
 * first 8 are whatever is on its stack unless a callback wrote them (ours:
 * zero); it returns the last subdevice's state (0 or -ENOIOCTLCMD).
 */
static inline int t32_tx_set_ae_algo_open(u32 isp, u32 user)
{
	u32 b[T32_AE_ALGO_BUF / 4], k;
	int ret = 0;

	memset(b, 0, sizeof(b));
	if (t32_tx_os_copy_from_user(b, user, 8)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_set_ae_algo_open", 1467);
		return -(int)T32_EFAULT;
	}
	if (b[0] != T32_AE_ALGO_VERSION) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Ae Algo Function register version error\n", "tx_isp_set_ae_algo_open", 1472);
		return -1;
	}
	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(isp, T32_DEV_SD + 4 * k), core, fn;
		int r;

		if (!sd)
			continue;
		core = t32_tx_ld(t32_tx_ld(sd, T32_SD_OPS), T32_OPS_CORE);
		if (!core || !(fn = t32_tx_ld(core, T32_CORE_IOCTL))) {
			ret = -(int)T32_ENOIOCTLCMD;
			continue;
		}
		r = ((t32_tx_cmd_fn)T32_TX_P(fn))(sd, T32_CORE_AE_ALGO, b);
		if (r && r != -(int)T32_ENOIOCTLCMD)
			return r;
		ret = r;
	}
	if (t32_tx_os_copy_to_user(user, b, T32_AE_ALGO_BUF)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy to user error\n", "tx_isp_set_ae_algo_open", 1487);
		return -(int)T32_EFAULT;
	}
	return ret;
}

static inline u32 t32_tx_video_fn(u32 sd, u32 slot)
{
	u32 v = t32_tx_ld(t32_tx_ld(sd, T32_SD_OPS), T32_OPS_VIDEO);

	return v ? t32_tx_ld(v, slot) : 0;
}

/*
 * The video walk of s_stream / link_stream: on an error other than
 * -ENOIOCTLCMD at subdevice i the call is repeated with the reverted
 * argument for i down to 1 (the vendor never reverts subdevice 0).
 */
static inline int t32_tx_video_walk(u32 isp, u32 slot, u32 *buf, int s_stream)
{
	u32 k;

	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(isp, T32_DEV_SD + 4 * k), fn;
		int r;

		if (!sd || !(fn = t32_tx_video_fn(sd, slot)))
			continue;
		r = ((t32_tx_buf_fn)T32_TX_P(fn))(sd, buf);
		if (!r || r == -(int)T32_ENOIOCTLCMD)
			continue;
		if (s_stream) {
			t32_tx_os_isp_printf(1, "\n==> [%s %d] %s \n", "tx_isp_video_s_stream", 859,
					     t32_tx_ld(sd, T32_SD_NAME));
			t32_tx_os_isp_printf(2, "s_stream error\n", "tx_isp_video_s_stream", 859);
			buf[0] = 0;
		} else {
			buf[0] = buf[0] == 0;
		}
		for (; k > 0; k--) {
			u32 s = t32_tx_ld(isp, T32_DEV_SD + 4 * k), f;

			if (s && (f = t32_tx_video_fn(s, slot)))
				((t32_tx_buf_fn)T32_TX_P(f))(s, buf);
		}
		return r;
	}
	return 0;
}

/* tx_isp_video_s_stream(isp, arg): { enable, input } */
static inline int t32_tx_video_s_stream(u32 isp, u32 arg)
{
	u32 b[2];

	if (t32_tx_arg_in(isp, b, arg, 8)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_video_s_stream", 842);
		return -(int)T32_EFAULT;
	}
	if (t32_tx_g_d9008 && ++t32_tx_stream_count[b[1]] == 2)
		return 0;
	return t32_tx_video_walk(isp, T32_VIDEO_S_STREAM, b, 1);
}

/* tx_isp_video_link_stream(isp, arg) */
static inline int t32_tx_video_link_stream(u32 isp, u32 arg)
{
	u32 b[2];

	if (t32_tx_arg_in(isp, b, arg, 8)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] copy from user error\n", "tx_isp_video_link_stream", 890);
		return -(int)T32_EFAULT;
	}
	return t32_tx_video_walk(isp, T32_VIDEO_LINK_STREAM, b, 0);
}

#endif
