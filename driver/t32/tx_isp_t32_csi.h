/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MIPI CSI subdev glue from the vendor module (Matt Davis' recovered C,
 * PR #15): csi_core_ops_init (0x7668), csi_sensor_ops_sync_sensor_attr
 * (0x7868) with its error path _part_4 (0x7838), csi_phy_start (0x8024),
 * csi_video_s_stream (0x80dc) and csi_sensor_ops_ioctl (0x81c8).
 *
 * CSI device (vendor offsets): register bases +0xf4 (host) and +0xf8 (second
 * block), per sensor a struct tx_isp_video_in copy (104 bytes) at +0x120 +
 * idx * 104 (its attribute pointer at +0x34), per sensor state +0x2c0 + idx * 4
 * (2 idle, 3 init/stopped, 4 streaming), spinlock +0x2dc, a 108 byte config
 * copy at +0x2ec (word 0: lanes - 1 style count, < 2 -> PHY mode 1 else 2),
 * the number of started sensors +0x358 and the PHY mode +0x35c.  The D-PHY
 * start itself (mipi_phy_stream_on, constprop) and the spinlock are hooks.
 * Requests: { on/enable, sensor index }.
 *
 * Vendor quirks kept: core_ops_init with on != 0 only sets the state 3, the
 * stop (on == 0, count != 0) clears the host (and for PHY mode 2 the second
 * block) registers 0x08 / 0x0c / 0x10, sets the state 2 and decrements the
 * count; s_stream only acts on MIPI sensors (attribute dbus type 1);
 * sync_sensor_attr reads the sensor index as a u16 at +92 of the structure
 * the copy's word +0x38 points to; ioctl answers 0 for everything (also for
 * an invalid subdev); the invalid-subdev printk carries no line.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_csi_oracle).
 */
#ifndef TX_ISP_T32_CSI_H
#define TX_ISP_T32_CSI_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_CSI_BASE		0x0f4U
#define T32_CSI_BASE2		0x0f8U
#define T32_CSI_VIN(i)		(0x120U + (i) * 104U)
#define T32_CSI_VIN_BYTES	104U
#define T32_CSI_VIN_ATTR	0x34U
#define T32_CSI_VIN_INFO	0x38U	/* -> u16 sensor index at +92 */
#define T32_CSI_INFO_INDEX	92U
#define T32_CSI_STATE(i)	(0x2c0U + (i) * 4U)
#define T32_CSI_LOCK		0x2dcU
#define T32_CSI_CFG		0x2ecU
#define T32_CSI_CFG_BYTES	108U
#define T32_CSI_COUNT		0x358U
#define T32_CSI_PHY_MODE	0x35cU
#define T32_CSI_ST_IDLE		2U
#define T32_CSI_ST_INIT		3U
#define T32_CSI_ST_ON		4U
#define T32_CSI_ATTR_DBUS	0x14U	/* 1 = MIPI */
#define T32_CSI_IOC_PHY_START	0x200000cU
#define T32_CSI_IOC_CFG		0x200001cU

struct t32_csi_os {
	void (*lock)(u8 *lock, u32 *flags);	/* __private_spin_lock_irqsave */
	void (*unlock)(u8 *lock, u32 flags);
	void (*phy_stream_on)(u8 *csi, u32 *arg);	/* mipi_phy_stream_on, arg { 1, idx } */
};

static inline int t32_csi_valid(const u8 *p)
{
	return p && (uintptr_t)p < (uintptr_t)(u32)-4095;
}

static inline int t32_csi_bad_subdev(void)	/* csi_sensor_ops_sync_sensor_attr_part_4 */
{
	isp_printf(2, "csi: invalid subdev\n", 0, 0);
	return -22;
}

static inline void t32_csi_set_state(const struct t32_csi_os *os, u8 *csi, u32 idx, u32 st)
{
	u32 flags = 0;

	os->lock(csi + T32_CSI_LOCK, &flags);
	t32_g_wr32(csi, T32_CSI_STATE(idx), st);
	os->unlock(csi + T32_CSI_LOCK, flags);
}

static inline int t32_csi_core_ops_init(const struct t32_csi_os *os, u8 *csi, const u32 *req)
{
	u32 flags = 0, mode, r;

	if (!t32_csi_valid(csi))
		return t32_csi_bad_subdev();
	os->lock(csi + T32_CSI_LOCK, &flags);
	if (req[0]) {
		t32_g_wr32(csi, T32_CSI_STATE(req[1]), T32_CSI_ST_INIT);
	} else if (t32_g_rd32(csi, T32_CSI_COUNT)) {
		mode = t32_g_rd32(csi, T32_CSI_PHY_MODE);
		if (mode == 1 || mode == 2) {
			for (r = 0x08; r <= 0x10; r += 4) {
				*(volatile u32 *)(T32_PTR(t32_g_rd32(csi, T32_CSI_BASE)) + r) = 0;
				if (mode == 2)
					*(volatile u32 *)(T32_PTR(t32_g_rd32(csi, T32_CSI_BASE2)) + r) = 0;
			}
		}
		t32_g_wr32(csi, T32_CSI_STATE(req[1]), T32_CSI_ST_IDLE);
		t32_g_wr32(csi, T32_CSI_COUNT, t32_g_rd32(csi, T32_CSI_COUNT) - 1);
	}
	os->unlock(csi + T32_CSI_LOCK, flags);
	return 0;
}

static inline int t32_csi_sync_sensor_attr(u8 *csi, const u8 *vin)
{
	u32 idx;

	if (!t32_csi_valid(csi))
		return t32_csi_bad_subdev();
	if (!vin)
		return 0;
	idx = t32_g_rd16(T32_PTR(t32_g_rd32(vin, T32_CSI_VIN_INFO)), T32_CSI_INFO_INDEX);
	memcpy(csi + T32_CSI_VIN(idx), vin, T32_CSI_VIN_BYTES);
	return 0;
}

static inline int t32_csi_phy_start(const struct t32_csi_os *os, u8 *csi, u32 idx)
{
	u32 arg[2];

	t32_g_wr32(csi, T32_CSI_PHY_MODE, t32_g_rd32(csi, T32_CSI_CFG) < 2 ? 1 : 2);
	arg[0] = 1;
	arg[1] = idx;
	os->phy_stream_on(csi, arg);
	t32_csi_set_state(os, csi, idx, T32_CSI_ST_ON);
	return 0;
}

static inline int t32_csi_video_s_stream(const struct t32_csi_os *os, u8 *csi, const u32 *req)
{
	u32 idx;
	u8 *attr;

	if (!t32_csi_valid(csi))
		return t32_csi_bad_subdev();
	idx = req[1];
	attr = T32_PTR(t32_g_rd32(csi, T32_CSI_VIN(idx) + T32_CSI_VIN_ATTR));
	if (t32_g_rd32(attr, T32_CSI_ATTR_DBUS) != 1)
		return 0;
	if (req[0])
		return t32_csi_phy_start(os, csi, idx);
	t32_csi_set_state(os, csi, idx, T32_CSI_ST_INIT);
	return 0;
}

static inline int t32_csi_sensor_ops_ioctl(const struct t32_csi_os *os, u8 *csi, u32 cmd, const u32 *arg)
{
	if (!t32_csi_valid(csi))
		return 0;
	if (cmd == T32_CSI_IOC_PHY_START)
		t32_csi_phy_start(os, csi, arg[0]);
	else if (cmd == T32_CSI_IOC_CFG)
		memcpy(csi + T32_CSI_CFG, arg, T32_CSI_CFG_BYTES);
	return 0;
}

#endif
