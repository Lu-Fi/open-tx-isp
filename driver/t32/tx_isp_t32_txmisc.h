/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp subdevice teardown and the core's sensor flip: vendor
 * tx_isp_subdev_deinit and tx_isp_core_shvflip.
 *
 * Subdevice: +12 misc device (registered when +52), +156 irq info,
 * +232 / +236 / +240 memory resources, +244 / +248 / +252 mappings,
 * +264 ops, +272 / +276 buffers.
 * Core (vendor global f3310 = core subdevice, +280 core device): per input
 * 96 bytes from +1344 (+1344 flip pending, +1348 flip mode).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txmisc_oracle).
 */
#ifndef TX_ISP_T32_TXMISC_H
#define TX_ISP_T32_TXMISC_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txirq.h"
#include "tx_isp_t32_txcore.h"
#include "tx_isp_t32_txioctl.h"

#define T32_SD_MISC		12U
#define T32_SD_MISC_ON		52U
#define T32_SD_RES(i)		(232U + 4U * (i))
#define T32_SD_MAP(i)		(244U + 4U * (i))
#define T32_SD_BUF(i)		(272U + 4U * (i))

#define T32_CORE_IN_BYTES	96U
#define T32_CORE_FLIP_PEND	1344U
#define T32_CORE_FLIP		1348U
#define T32_VIC_CTRL_FLIP	17476U
#define T32_SENSOR_FLIP		0x2000010U
#define T32_CORE_FLIP_CMD	0x1000007U
#define T32_CORE_FLIP_ARG	0x1000009U
#define T32_ENODEV		19U

/* provided by the driver glue (kernel) or the oracle (host) */
extern u32 t32_tx_core_sd;		/* vendor f3310 */
void t32_tx_os_misc_deregister(u32 misc);
void t32_tx_os_release_clks(u32 sd);
void t32_tx_os_kfree(u32 p);
void t32_tx_os_iounmap(u32 a);
void t32_tx_os_release_mem_region(u32 start, u32 n);
void t32_tx_os_vic_ctrl(u32 which, u32 val);
int t32_tx_os_ispcore_sensor_ioctl(u32 sd, u32 cmd, void *arg);

/* tx_isp_subdev_deinit */
static inline void t32_tx_subdev_deinit(u32 sd)
{
	u32 i, res;

	if (t32_tx_ld(sd, T32_SD_MISC_ON))
		t32_tx_os_misc_deregister(sd + T32_SD_MISC);
	t32_tx_os_release_clks(sd);
	for (i = 0; i < 2; i++)
		if (t32_tx_ld(sd, T32_SD_BUF(i)))
			t32_tx_os_kfree(t32_tx_ld(sd, T32_SD_BUF(i)));
	for (i = 0; i < 3; i++)
		if (t32_tx_ld(sd, T32_SD_MAP(i)))
			t32_tx_os_iounmap(t32_tx_ld(sd, T32_SD_MAP(i)));
	for (i = 0; i < 3; i++) {
		res = t32_tx_ld(sd, T32_SD_RES(i));
		if (res) {
			t32_tx_os_release_mem_region(t32_tx_ld(res, 0), t32_tx_ld(res, 4) + 1 - t32_tx_ld(res, 0));
			t32_tx_st(sd, T32_SD_RES(i), 0);
		}
	}
	t32_tx_isp_free_irq(sd + T32_SD_IRQ);
	t32_tx_isp_module_deinit(sd);
	t32_tx_st(sd, T32_SD_OPS, 0);
}

/*
 * tx_isp_core_shvflip(input): VIC flip control on both VICs, the sensor's
 * flip (sensor ioctl 0x2000010 with { input, &msg }), then the core ioctl
 * 0x1000007 with { max(msg[1], 1), 0x1000009 }.  Vendor: the f3310 NULL
 * test comes after the first use.
 */
static inline int t32_tx_core_shvflip(u32 input)
{
	u32 sd = t32_tx_core_sd, in = t32_tx_ld(sd, T32_SD_PRIV_CORE) + input * T32_CORE_IN_BYTES;
	u32 msg[3], arg[2], cmd[2], core, fn;
	int v;

	msg[0] = t32_tx_ld(in, T32_CORE_FLIP);
	arg[0] = input;
	arg[1] = T32_TX_A(msg);
	t32_tx_os_vic_ctrl(0, T32_VIC_CTRL_FLIP);
	t32_tx_os_vic_ctrl(1, T32_VIC_CTRL_FLIP);
	t32_tx_os_ispcore_sensor_ioctl(t32_tx_core_sd, T32_SENSOR_FLIP, arg);
	v = (int)msg[1];
	if (v <= 0)
		v = 1;
	msg[1] = (u32)v;
	cmd[0] = (u32)v;
	cmd[1] = T32_CORE_FLIP_ARG;
	t32_tx_st(in, T32_CORE_FLIP_PEND, 0);
	sd = t32_tx_core_sd;
	if (!sd)
		return -(int)T32_ENODEV;
	core = t32_tx_ld(t32_tx_ld(sd, T32_SD_OPS), T32_OPS_CORE);
	if (!core || !(fn = t32_tx_ld(core, T32_CORE_IOCTL)))
		return -(int)T32_ENOIOCTLCMD;
	return ((t32_tx_cmd_fn)T32_TX_P(fn))(sd, T32_CORE_FLIP_CMD, cmd);
}

#endif
