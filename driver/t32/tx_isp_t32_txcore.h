/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp core glue: subdevice module init / deinit, register field set,
 * event forwarding, the ISP device's open / notify and the platform PM hooks
 * (prepare, complete, suspend, resume, resume_early), platform teardown
 * (vendor tx_isp_module_init, _module_deinit, _reg_set,
 * _send_event_to_remote, _unlocked_ioctl_first, _release_device, _notify,
 * _open, _prepare, _complete, _resume_early, _resume, _suspend,
 * _unregister_platforms, _exit).
 *
 * Layouts (offsets the vendor uses):
 *   ISP device (platform drvdata, vendor global da784):
 *     +68 / +72 completion "waiting" flag / completion, +72.. or +84.. the
 *     subdevice table (open walks +72..+132, notify / suspend +84..+144,
 *     the reverse walks +148 down to +84), +292 open count, +312..+320
 *     state cleared on the first open unless the vendor global d9008 is set.
 *   subdevice: +4 pads, +8 name (char *), +52 / +56, +84..+147 cleared,
 *     +148 / +152 notify, +244 register base, +256 / +260 clocks / count,
 *     +264 ops.
 *   ops: +0 core ops (+32 notify), +12 video ops (+8 notify), +16 internal
 *     (PM) ops: +0 open, +8 prepare, +12 suspend, +16 resume_early,
 *     +20 resume, +24 complete.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txcore_oracle): every hook / callback
 * call in order with its arguments, prints, the device and subdevice
 * bytes, the return values.
 */
#ifndef TX_ISP_T32_TXCORE_H
#define TX_ISP_T32_TXCORE_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txirq.h"

#define T32_ENOIOCTLCMD		515U

#define T32_DEV_DONE_WAIT	68U
#define T32_DEV_DONE		72U
#define T32_DEV_OPEN_SD		72U	/* open: 16 subdevices +72 .. +132 */
#define T32_DEV_SD		84U	/* notify / suspend: 16 subdevices +84 .. +144 */
#define T32_DEV_SD_LAST		148U	/* complete / prepare / resume*: +148 down to +84 */
#define T32_DEV_SD_N		16U
#define T32_DEV_OPENS		292U
#define T32_DEV_STATE0		312U

#define T32_SD_PADS		4U
#define T32_SD_NAME		8U
#define T32_SD_W52		52U
#define T32_SD_W56		56U
#define T32_SD_CLEAR		84U	/* 64 bytes cleared by module init */
#define T32_SD_CLEAR_N		64U
#define T32_SD_W148		148U
#define T32_SD_NOTIFY		152U
#define T32_SD_REGS		244U
#define T32_SD_CLKS		256U
#define T32_SD_NCLK		260U
#define T32_SD_OPS		264U
#define T32_SD_INIT_BYTES	156U	/* module deinit clears this much */

#define T32_OPS_CORE		0U
#define T32_OPS_VIDEO		12U
#define T32_OPS_INTERNAL	16U
#define T32_CORE_NOTIFY		32U
#define T32_VIDEO_NOTIFY	8U
#define T32_PM_OPEN		0U
#define T32_PM_PREPARE		8U
#define T32_PM_SUSPEND		12U
#define T32_PM_RESUME_EARLY	16U
#define T32_PM_RESUME		20U
#define T32_PM_COMPLETE		24U

#define T32_EV_DONE		0x1000011U
#define T32_EV_CLASS(e)		((e) & 0xff000000U)
#define T32_EV_CORE		0x1000000U
#define T32_EV_VIDEO		0x2000000U
#define T32_EV_CORE5		0x5000000U

#define T32_PDEV_DATA		88U	/* platform device: 4 bytes copied to the subdevice */
#define T32_PDEV_OFF		16U	/* struct device inside the platform device */
#define T32_PLAT_N		16U	/* platform table: { pdev, drv }, drv +4 = remove */

/* vendor globals, provided by the driver glue (kernel) or the oracle (host) */
extern u32 t32_tx_isp_dev;		/* da784: the ISP device */
extern u32 t32_tx_g_d9008;		/* d9008: non-zero keeps +312..+320 on the first open */

typedef int (*t32_tx_sd_fn)(u32 sd);
typedef int (*t32_tx_ev_fn)(u32 sd, u32 event, u32 arg);
typedef int (*t32_tx_open_fn)(u32 sd, u32 file);

/* provided by the driver glue (kernel) or by the oracle (host) */
void t32_tx_os_complete(u32 done);
u32 t32_tx_os_get_drvdata(u32 dev);
int t32_tx_os_unlocked_ioctl(u32 a0, u32 file, u32 cmd, u32 arg);
void t32_tx_os_platform_driver_unregister(u32 drv);
void t32_tx_os_platform_device_unregister(u32 pdev);
void t32_tx_os_clk_prepare_enable(u32 clk);
void t32_tx_os_clk_disable_unprepare(u32 clk);
int t32_tx_os_riscv_run(void);
int t32_tx_os_riscv_stop(void);

static inline u32 t32_tx_sd_pm(u32 sd, u32 slot)
{
	u32 pm = t32_tx_ld(t32_tx_ld(sd, T32_SD_OPS), T32_OPS_INTERNAL);

	return pm ? t32_tx_ld(pm, slot) : 0;
}

/* tx_isp_release_device: empty */
static inline int t32_tx_isp_release_device(void)
{
	return 0;
}

/* tx_isp_module_deinit */
static inline void t32_tx_isp_module_deinit(u32 sd)
{
	if (sd)
		memset(T32_TX_P(sd), 0, T32_SD_INIT_BYTES);
}

/* tx_isp_reg_set(sd, reg, lo, hi, val): bits lo..hi of the register at +244 base + reg */
static inline int t32_tx_isp_reg_set(u32 sd, u32 reg, u32 lo, u32 hi, u32 val)
{
	u32 mask = 0, a = t32_tx_ld(sd, T32_SD_REGS) + reg;
	int i;

	for (i = 0; i < (int)(hi - lo + 1); i++)
		mask += 1U << ((i + lo) & 31);
	t32_tx_st(a, 0, (val << (lo & 31)) | (~mask & t32_tx_ld(a, 0)));
	return 0;
}

/* tx_isp_send_event_to_remote(pad, event, arg): the linked pad's event callback */
static inline int t32_tx_isp_send_event_to_remote(u32 pad, u32 event, u32 arg)
{
	u32 remote, fn;

	if (!pad || !(remote = t32_tx_ld(pad, 12)) || !(fn = t32_tx_ld(remote, 28)))
		return -(int)T32_ENOIOCTLCMD;
	return ((t32_tx_ev_fn)T32_TX_P(fn))(remote, event, arg);
}

/*
 * tx_isp_unlocked_ioctl_first(a0, a1): tail call tx_isp_unlocked_ioctl(0, a0, a1, a3);
 * the vendor passes its own a3 register on unchanged (x here).
 */
static inline int t32_tx_isp_unlocked_ioctl_first(u32 a0, u32 a1, u32 x)
{
	return t32_tx_os_unlocked_ioctl(0, a0, a1, x);
}

/* tx_isp_notify(sd, event, arg): forward to every subdevice (core / video ops) */
static inline int t32_tx_isp_notify(u32 sd, u32 event, u32 arg)
{
	u32 dev = t32_tx_isp_dev, k, cls = T32_EV_CLASS(event);
	int ret = 0;

	(void)sd;
	if (event == T32_EV_DONE) {
		if (t32_tx_ld(dev, T32_DEV_DONE_WAIT)) {
			t32_tx_st(dev, T32_DEV_DONE_WAIT, 0);
			t32_tx_os_complete(dev + T32_DEV_DONE);
		}
		return 0;
	}
	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 s = t32_tx_ld(dev, T32_DEV_SD + 4 * k), ops, fn = 0;
		int r;

		if (!s)
			continue;
		ops = t32_tx_ld(s, T32_SD_OPS);
		if (cls == T32_EV_CORE || cls == T32_EV_CORE5) {
			u32 core = t32_tx_ld(ops, T32_OPS_CORE);

			if (!core) {
				ret = -(int)T32_ENOIOCTLCMD;
				continue;
			}
			fn = t32_tx_ld(core, T32_CORE_NOTIFY);
		} else if (cls == T32_EV_VIDEO) {
			u32 video = t32_tx_ld(ops, T32_OPS_VIDEO);

			if (!video) {
				ret = -(int)T32_ENOIOCTLCMD;
				continue;
			}
			fn = t32_tx_ld(video, T32_VIDEO_NOTIFY);
		} else {
			ret = 0;
			continue;
		}
		if (!fn) {
			ret = -(int)T32_ENOIOCTLCMD;
			continue;
		}
		r = ((t32_tx_ev_fn)T32_TX_P(fn))(s, event, arg);
		if (r && r != -(int)T32_ENOIOCTLCMD)
			return r;
		ret = r;
	}
	return ret == -(int)T32_ENOIOCTLCMD ? 0 : ret;
}

/* tx_isp_module_init(pdev, sd) */
static inline int t32_tx_isp_module_init(u32 pdev, u32 sd)
{
	u32 data;

	if (!sd) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] the parameters are invalid!\n", "tx_isp_module_init", 2896);
		return -(int)T32_EINVAL;
	}
	/* vendor: pdev 0 passes this test and faults on the read of pdev->name below */
	if (pdev && (data = t32_tx_ld(pdev, T32_PDEV_DATA)))
		memcpy(T32_TX_P(sd), T32_TX_P(data), 4);
	t32_tx_st(sd, T32_SD_W148, 0);
	memset(T32_TX_P(sd + T32_SD_CLEAR), 0, T32_SD_CLEAR_N);
	t32_tx_st(sd, T32_SD_W52, 0);
	t32_tx_st(sd, T32_SD_NAME, t32_tx_ld(pdev, 0));
	t32_tx_st(sd, T32_SD_NOTIFY, T32_TX_A(t32_tx_isp_notify));
	t32_tx_st(sd, T32_SD_W56, 0);
	t32_tx_st(sd, T32_SD_PADS, pdev + T32_PDEV_OFF);
	return 0;
}

/* tx_isp_open(inode, file): the first open runs every subdevice's open */
static inline int t32_tx_isp_open(u32 inode, u32 file)
{
	u32 dev = t32_tx_ld(file, 120), k, n = t32_tx_ld(dev, T32_DEV_OPENS);
	int ret = 0;

	(void)inode;
	if (n) {
		t32_tx_st(dev, T32_DEV_OPENS, n + 1);
		return 0;
	}
	if (!t32_tx_g_d9008) {
		t32_tx_st(dev, T32_DEV_STATE0, 0);
		t32_tx_st(dev, T32_DEV_STATE0 + 4, 0);
		t32_tx_st(dev, T32_DEV_STATE0 + 8, 0);
	}
	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 s = t32_tx_ld(dev, T32_DEV_OPEN_SD + 4 * k), fn;
		int r;

		if (!s)
			continue;
		fn = t32_tx_sd_pm(s, T32_PM_OPEN);
		if (!fn) {
			ret = -(int)T32_ENOIOCTLCMD;
			continue;
		}
		r = ((t32_tx_open_fn)T32_TX_P(fn))(s, file);
		if (r && r != -(int)T32_ENOIOCTLCMD)
			return r;
		ret = r;
	}
	return ret == -(int)T32_ENOIOCTLCMD ? 0 : ret;
}

static inline int t32_tx_sd_ok(u32 s)
{
	return s && !T32_IS_ERR(s);
}

/* tx_isp_complete(dev): +148 down to +84 */
static inline void t32_tx_isp_complete(u32 dev)
{
	u32 drv = t32_tx_os_get_drvdata(dev - T32_PDEV_OFF), a;

	t32_tx_os_isp_printf(0, "[%s %d] Enter the complete... \n", "tx_isp_complete", 3465);
	for (a = drv + T32_DEV_SD_LAST; a != drv + T32_DEV_SD - 4; a -= 4) {
		u32 s = t32_tx_ld(a, 0), fn;

		if (t32_tx_sd_ok(s) && (fn = t32_tx_sd_pm(s, T32_PM_COMPLETE)))
			((t32_tx_sd_fn)T32_TX_P(fn))(s);
	}
}

/*
 * The reverse PM walk of prepare / resume_early: the callback result
 * decides, an error other than -ENOIOCTLCMD stops with -EINVAL; the
 * vendor returns the last entry's state (0 or -ENOIOCTLCMD).
 */
static inline int t32_tx_pm_walk_down(u32 drv, u32 slot, const char *fn_name, const char *fail,
				      int fail_line, const char *each, int each_line)
{
	u32 a;
	int ret = 0;

	for (a = drv + T32_DEV_SD_LAST; a != drv + T32_DEV_SD - 4; a -= 4) {
		u32 s = t32_tx_ld(a, 0), fn;
		int r;

		if (!t32_tx_sd_ok(s))
			continue;
		if (each)
			t32_tx_os_isp_printf(0, each, fn_name, each_line, t32_tx_ld(s, T32_SD_NAME));
		fn = t32_tx_sd_pm(s, slot);
		if (!fn) {
			ret = -(int)T32_ENOIOCTLCMD;
			continue;
		}
		r = ((t32_tx_sd_fn)T32_TX_P(fn))(s);
		if (!r) {
			ret = 0;
			continue;
		}
		if (r == -(int)T32_ENOIOCTLCMD) {
			ret = r;
			continue;
		}
		t32_tx_os_isp_printf(2, fail, fn_name, fail_line, t32_tx_ld(s, T32_SD_NAME));
		return -(int)T32_EINVAL;
	}
	return ret;
}

/* tx_isp_prepare(dev) */
static inline int t32_tx_isp_prepare(u32 dev)
{
	u32 drv = t32_tx_os_get_drvdata(dev - T32_PDEV_OFF);

	t32_tx_os_isp_printf(0, "[%s %d] Enter the prepare... \n", "tx_isp_prepare", 3494);
	return t32_tx_pm_walk_down(drv, T32_PM_PREPARE, "tx_isp_prepare", "[ %s:%d ] Failed to prepare %s\n",
				   3506, "[ %s:%d ] prepare %s\n", 3501);
}

/* tx_isp_resume_early(dev) */
static inline int t32_tx_isp_resume_early(u32 dev)
{
	u32 drv = t32_tx_os_get_drvdata(dev - T32_PDEV_OFF);

	t32_tx_os_isp_printf(0, "[%s %d] Enter the resume early... \n", "tx_isp_resume_early", 3435);
	return t32_tx_pm_walk_down(drv, T32_PM_RESUME_EARLY, "tx_isp_resume_early",
				   "[ %s:%d ] Failed to resume early %s\n", 3446, NULL, 0);
}

/* tx_isp_resume(dev): RISC-V run, clocks on (+148 down), then the resume callbacks */
static inline int t32_tx_isp_resume(u32 dev)
{
	u32 drv = t32_tx_os_get_drvdata(dev - T32_PDEV_OFF), a;
	int i;

	t32_tx_os_isp_printf(0, "[%s %d] Enter the resume ... \n", "tx_isp_resume", 3576);
	t32_tx_os_riscv_run();
	for (a = drv + T32_DEV_SD_LAST; a != drv + T32_DEV_SD - 4; a -= 4) {
		u32 s = t32_tx_ld(a, 0);

		if (!t32_tx_sd_ok(s))
			continue;
		for (i = 0; i < (int)t32_tx_ld(s, T32_SD_NCLK); i++)
			t32_tx_os_clk_prepare_enable(t32_tx_ld(t32_tx_ld(s, T32_SD_CLKS), 4 * i));
	}
	/* the vendor's failure print says "suspend" */
	return t32_tx_pm_walk_down(drv, T32_PM_RESUME, "tx_isp_resume", "[ %s:%d ] Failed to suspend %s\n",
				   3601, NULL, 0);
}

/* tx_isp_suspend(dev): suspend callbacks (+84 up), RISC-V stop, clocks off in reverse */
static inline int t32_tx_isp_suspend(u32 dev)
{
	u32 drv = t32_tx_os_get_drvdata(dev - T32_PDEV_OFF), k;
	int ret = 0, i;

	t32_tx_os_isp_printf(0, "[%s %d] Enter the suspend ... \n", "tx_isp_suspend", 3528);
	t32_tx_os_riscv_stop();
	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 s = t32_tx_ld(drv, T32_DEV_SD + 4 * k), fn;
		int r;

		if (!t32_tx_sd_ok(s))
			continue;
		fn = t32_tx_sd_pm(s, T32_PM_SUSPEND);
		if (!fn) {
			ret = -(int)T32_ENOIOCTLCMD;
			continue;
		}
		r = ((t32_tx_sd_fn)T32_TX_P(fn))(s);
		if (r && r != -(int)T32_ENOIOCTLCMD) {
			t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to suspend %s\n", "tx_isp_suspend", 3540,
					     t32_tx_ld(s, T32_SD_NAME));
			return -(int)T32_EINVAL;
		}
		ret = r;
	}
	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 s = t32_tx_ld(drv, T32_DEV_SD + 4 * k);

		if (!t32_tx_sd_ok(s))
			continue;
		for (i = (int)(t32_tx_ld(s, T32_SD_NCLK) - 1); i >= 0; i--)
			t32_tx_os_clk_disable_unprepare(t32_tx_ld(t32_tx_ld(s, T32_SD_CLKS), 4 * i));
	}
	return ret;
}

/* tx_isp_unregister_platforms(table): 16 { pdev, drv } entries, last first */
static inline void t32_tx_isp_unregister_platforms(u32 tab)
{
	int k;

	for (k = T32_PLAT_N - 1; k >= 0; k--) {
		u32 e = tab + 8 * k, drv = t32_tx_ld(e, 4);

		if (drv)	/* vendor: no check of the remove pointer itself */
			((t32_tx_sd_fn)T32_TX_P(t32_tx_ld(drv, 4)))(t32_tx_ld(e, 0));
		if (t32_tx_ld(e, 0))
			t32_tx_os_platform_device_unregister(t32_tx_ld(e, 0));
	}
}

/* tx_isp_exit(platform driver, platform device) */
static inline void t32_tx_isp_exit(u32 drv, u32 pdev)
{
	t32_tx_os_platform_driver_unregister(drv);
	t32_tx_os_platform_device_unregister(pdev);
}

#endif
