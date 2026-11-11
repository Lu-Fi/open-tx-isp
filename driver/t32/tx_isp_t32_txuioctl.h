/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp device ioctl (vendor tx_isp_unlocked_ioctl, the /dev/tx-isp
 * VIDIOC_* dispatcher of the ISP device).
 *
 * file == 0: kernel call, the ISP device is the vendor global da784 and +60
 * (kernel caller flag) is set to 1; else the device is file +120 (private
 * data) - 12 and +60 is cleared.  Unknown commands return 0.
 *
 * Command groups (each proven by tests/t32_oracle/t32_txuio_oracle against
 * the vendor flow, Matt Davis' recovered C, PR #15):
 *   helpers  driver version, default bin path set / get, sensor register /
 *            release, multi-sensor mode, AE algorithm open, video s_stream /
 *            link stream (tx_isp_t32_txioctl.h);
 *   core     core ioctl (subdevice ops +0, +32) broadcast to the 16
 *            subdevices +84 .. +144 in three vendor flavours.
 */
#ifndef TX_ISP_T32_TXUIOCTL_H
#define TX_ISP_T32_TXUIOCTL_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txcore.h"
#include "tx_isp_t32_txioctl.h"

#define T32_FILE_PRIV		120U	/* file->private_data: ISP device + 12 */
#define T32_FILE_PRIV_ADJ	12U

/* VIDIOC_* command words of the ISP device */
#define T32_UIO_VERSION		0x80045401U
#define T32_UIO_SENSOR_REG	0x80745405U
#define T32_UIO_SENSOR_REL	0x80745406U
#define T32_UIO_S_STREAM	0x80085407U
#define T32_UIO_S_STREAM2	0x80085408U
#define T32_UIO_LINK_STREAM	0xc008540bU
#define T32_UIO_LINK_STREAM2	0xc008540cU
#define T32_UIO_MULTISENSOR	0xc0045415U
#define T32_UIO_CORE_16		0xc004541bU
#define T32_UIO_CORE_17		0xc004541cU
#define T32_UIO_AE_ALGO		0xc004541dU
#define T32_UIO_CORE_1A		0xc0045423U
#define T32_UIO_SET_BINPATH	0xc004542aU
#define T32_UIO_GET_BINPATH	0xc004542bU
#define T32_UIO_CORE_SUM	0xc00454b8U

/* core ioctl command words the broadcasts hand on */
#define T32_CORE_CMD_16		0x1000016U
#define T32_CORE_CMD_17		0x1000017U
#define T32_CORE_CMD_1A		0x100001aU
#define T32_CORE_CMD_SUM	0x5000005U

typedef int (*t32_uio_core_fn)(u32 sd, u32 cmd, u32 arg);

/* subdevice k's core ioctl (0 = none); the ops table itself is not checked (vendor) */
static inline u32 t32_uio_core_ioctl(u32 sd)
{
	u32 core = t32_tx_ld(t32_tx_ld(sd, T32_SD_OPS), T32_OPS_CORE);

	return core ? t32_tx_ld(core, T32_CORE_IOCTL) : 0;
}

/* broadcast: an error other than -ENOIOCTLCMD stops and is returned, else 0 */
static inline int t32_uio_core_each(u32 isp, u32 cmd, u32 arg)
{
	u32 k;

	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(isp, T32_DEV_SD + 4 * k), fn;
		int r;

		if (!sd || !(fn = t32_uio_core_ioctl(sd)))
			continue;
		r = ((t32_uio_core_fn)T32_TX_P(fn))(sd, cmd, arg);
		if (r && r != -(int)T32_ENOIOCTLCMD)
			return r;
	}
	return 0;
}

/* broadcast: like _each, but returns the last subdevice's result (-ENOIOCTLCMD for no ioctl) */
static inline int t32_uio_core_last(u32 isp, u32 cmd, u32 arg)
{
	int r = 0;
	u32 k;

	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(isp, T32_DEV_SD + 4 * k), fn;
		if (!sd)
			continue;
		fn = t32_uio_core_ioctl(sd);
		if (!fn) {
			r = -(int)T32_ENOIOCTLCMD;
			continue;
		}
		r = ((t32_uio_core_fn)T32_TX_P(fn))(sd, cmd, arg);
		if (r == -(int)T32_ENOIOCTLCMD)
			continue;
		if (r)
			return r;
	}
	return r;
}

/* broadcast: the sum of all results (-ENOIOCTLCMD for no ioctl) */
static inline int t32_uio_core_sum(u32 isp, u32 cmd, u32 arg)
{
	u32 k, sum = 0;

	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(isp, T32_DEV_SD + 4 * k), fn;
		if (!sd)
			continue;
		fn = t32_uio_core_ioctl(sd);
		sum += fn ? (u32)((t32_uio_core_fn)T32_TX_P(fn))(sd, cmd, arg) : (u32)-(int)T32_ENOIOCTLCMD;
	}
	return (int)sum;
}

/* tx_isp_unlocked_ioctl(file, cmd, arg) */
static inline int t32_tx_isp_unlocked_ioctl(u32 file, u32 cmd, u32 arg)
{
	u32 isp;

	if (file) {
		isp = t32_tx_ld(file, T32_FILE_PRIV) - T32_FILE_PRIV_ADJ;
		t32_tx_st(isp, T32_ISP_KCALL, 0);
	} else {
		isp = t32_tx_isp_dev;
		t32_tx_st(isp, T32_ISP_KCALL, 1);
	}
	switch (cmd) {
	case T32_UIO_VERSION:		return t32_tx_driver_version(arg);
	case T32_UIO_SENSOR_REG:	return t32_tx_sensor_register_sensor(isp, arg);
	case T32_UIO_SENSOR_REL:	return t32_tx_sensor_release_sensor(isp, arg);
	case T32_UIO_S_STREAM:
	case T32_UIO_S_STREAM2:		return t32_tx_video_s_stream(isp, arg);
	case T32_UIO_LINK_STREAM:
	case T32_UIO_LINK_STREAM2:	return t32_tx_video_link_stream(isp, arg);
	case T32_UIO_MULTISENSOR:	return t32_tx_multisensor_mode(isp, arg);
	case T32_UIO_AE_ALGO:		return t32_tx_set_ae_algo_open(isp, arg);
	case T32_UIO_SET_BINPATH:	return t32_tx_set_default_bin_path(isp + T32_DEV_SD, arg);
	case T32_UIO_GET_BINPATH:	return t32_tx_get_default_bin_path(isp + T32_DEV_SD, arg);
	case T32_UIO_CORE_16:		return t32_uio_core_each(isp, T32_CORE_CMD_16, arg);
	case T32_UIO_CORE_17:		return t32_uio_core_each(isp, T32_CORE_CMD_17, arg);
	case T32_UIO_CORE_1A:		return t32_uio_core_last(isp, T32_CORE_CMD_1A, arg);
	case T32_UIO_CORE_SUM:		return t32_uio_core_sum(isp, T32_CORE_CMD_SUM, arg);
	}
	return 0;
}

#endif
