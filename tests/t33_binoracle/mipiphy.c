/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "mipiphy": mipi_phy_stream_on (driver/t32/tx_isp_t33_mipi.h) with the vendor calling
 * convention and the vendor's CSI device layout: sd + 0x114 the CSI device; there + 0xf0 / + 0xf4 the register
 * blocks of the two hosts, + 0x150 + 0x68 * i the sensor attribute pointer (+ 0x14 bus type, + 0x1c clock, + 0x28
 * host, + 0x29 lanes), + 0x2bc + 4 * i the state, + 0x2dc the D-PHY block, + 0x358 the mode; attr[1] the sensor.
 */
#include <stdint.h>
#include "tx_isp_t33_mipi.h"

typedef uint8_t u8;
typedef uint32_t u32;

int zeratul_open, atlas_open;
extern void private_msleep(u32 ms);
extern void private_mdelay(u32 ms);
extern int isp_printf(int level, const char *fmt, u32 val);

static u8 *bo_csi;

static void bo_host(unsigned int ctl, unsigned int reg, unsigned int val)
{
	*(u32 *)(*(u8 **)(bo_csi + 0xf0 + 4 * ctl) + reg) = val;
}

static void bo_phy(unsigned int reg, unsigned int val)
{
	*(u32 *)(*(u8 **)(bo_csi + 0x2dc) + reg) = val;
}

static void bo_sleep(void)
{
	if (!zeratul_open && !atlas_open)
		private_msleep(1);
	else
		private_mdelay(1);
}

static void bo_log(int what, unsigned int val)
{
	isp_printf(what == T33_MIPI_LOG_DBUS ? 1 : what >= T33_MIPI_LOG_SETTLE0 ? 0 : 2, "", val);
}

static const struct t33_mipi_ops bo_ops = { bo_host, bo_phy, bo_sleep, bo_log };

int bo_mipi_phy_stream_on(u8 *sd, const u32 *attr)
{
	struct t33_mipi_sensor s[2];
	u32 i, idx = attr[1];

	if (!sd || (u32)(uintptr_t)sd >= 0xfffff001u)
		return -22;
	bo_csi = *(u8 **)(sd + 0x114);
	if (!bo_csi || (u32)(uintptr_t)bo_csi >= 0xfffff001u)
		return -22;
	for (i = 0; i < 2; i++) {
		const u8 *a = *(u8 **)(bo_csi + 0x150 + 0x68 * i);

		s[i].dbus = *(u32 *)(a + 0x14);
		s[i].clk = *(int *)(a + 0x1c);
		s[i].index = a[0x28];
		s[i].lanes = a[0x29];
	}
	return t33_mipi_phy_stream_on(&bo_ops, *(u32 *)(bo_csi + 0x358), *(int *)(bo_csi + 0x2bc + 4 * idx), idx, s);
}
