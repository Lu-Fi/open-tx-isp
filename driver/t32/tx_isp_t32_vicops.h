/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC subdev glue from the vendor module (Matt Davis' recovered C, PR #15):
 * vic_core_ops_init (0x23d4), vic_sensor_ops_ioctl (0x2774),
 * vic_sensor_ops_sync_sensor_attr (0x2960) with its error path _part_2
 * (0x2930) and vic_mdma_irq_function (0x3ed8).  (The VIC register values
 * themselves are in tx_isp_t32_vic.h.)
 *
 * VIC device (the private data of the VIC subdev, +0x118; vendor offsets):
 * register base +0xf4, per sensor a struct tx_isp_video_in copy at +0x120 +
 * idx * 104 (width +0, height +4, word +8, attribute pointer +0x34, info
 * pointer +0x38 -> u16 sensor index at +92), per sensor word +0x2e0 + idx * 4
 * (the VIC index used for the stop check, read as an index again), per
 * sensor "attr synced" flag +0x2c0 + idx * 4, per sensor state +0x300 +
 * idx * 4 (2 off, 3 on), +0x334 (VIC running: stop/reset on 0x2000013), a
 * 108 byte config copy +0x448, per VIC index k two byte pairs at +0x99c +
 * 2k / +0x9a0 + 2k and two 16 byte blocks at +0x9a4 + 32k / +0x9b4 + 32k.
 * MDMA globals: frames left (t32_data_d9310), frame index (d9318), DMA base
 * (d9304), "no completion" flag (d930c); the frame-done completion is at
 * VIC +0x370.  The tisp VIC library and tx_isp_vic_start / index_cal are hooks.
 *
 * Vendor quirks / UB kept: core_ops_init does not check the private data
 * pointer; its stop path checks the state of the sensor whose index is the
 * word +0x2e0 + idx * 4 (double indirection); sensor_ops_ioctl 0x200000c
 * starts the VIC with the mode argument % 2 (signed) and 0x200000f starts it
 * with 0 and then 1, returning tx_isp_vic_start's result; 0x200001d reads
 * its request from address 0 instead of the argument (an oops; kept as the
 * vendor does it, never exercised by the oracle); the error printk carries
 * no line.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_vicops_oracle).
 */
#ifndef TX_ISP_T32_VICOPS_H
#define TX_ISP_T32_VICOPS_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_VICD_SD_PRIV	0x118U
#define T32_VICD_BASE		0x0f4U
#define T32_VICD_VIN(i)		(0x120U + (i) * 104U)
#define T32_VICD_VIN_BYTES	104U
#define T32_VICD_VIN_ATTR	0x34U
#define T32_VICD_VIN_INFO	0x38U
#define T32_VICD_INFO_INDEX	92U
#define T32_VICD_WIDTH		0x120U	/* vin copy of the channel the MDMA uses (sensor 0) */
#define T32_VICD_HEIGHT		0x124U
#define T32_VICD_LINK(i)	(0x2e0U + (i) * 4U)
#define T32_VICD_LINK_SET(k)	(0x2d0U + (k) * 4U)	/* written by 0x200001d */
#define T32_VICD_SYNCED(i)	(0x2c0U + (i) * 4U)
#define T32_VICD_STATE(i)	(0x300U + (i) * 4U)
#define T32_VICD_RUNNING	0x334U
#define T32_VICD_COMPLETE	0x370U
#define T32_VICD_CFG		0x448U
#define T32_VICD_CFG_BYTES	108U
#define T32_VICD_PAIR_A(k)	(0x99cU + (k) * 2U)
#define T32_VICD_PAIR_B(k)	(0x9a0U + (k) * 2U)
#define T32_VICD_BLK_A(k)	(0x9a4U + (k) * 32U)
#define T32_VICD_BLK_B(k)	(0x9b4U + (k) * 32U)
#define T32_VICD_ST_OFF		2U
#define T32_VICD_ST_ON		3U
#define T32_VIC_REG_MDMA_ADDR	0x320U
#define T32_VIC_REG_CTRL	0x10U

#define T32_VIC_IOC_START	0x200000cU	/* index, config, start(mode % 2) */
#define T32_VIC_IOC_IRQ		0x200000eU	/* register 0x10 = 0x10 */
#define T32_VIC_IOC_RESTART	0x200000fU	/* start(0), start(1) */
#define T32_VIC_IOC_STOP	0x2000013U
#define T32_VIC_IOC_CFG		0x200001cU
#define T32_VIC_IOC_LINK	0x200001dU	/* vendor: NULL dereference */

struct t32_vicops_os {
	u32 (*index_cal)(u8 *vic, u32 idx);			/* tx_isp_vic_index_cal */
	void (*lib_init)(u32 k, u32 attr, const u32 *cfg);	/* lib_tisp_vic_init, cfg { w, h, word, cfg word 0 } */
	void (*lib_deinit)(void);
	void (*lib_cfg)(u32 k, u32 attr);			/* lib_tisp_vic_cfg */
	void (*lib_stop)(void);
	void (*lib_reset)(void);
	int (*vic_start)(u8 *vic, u32 mode);			/* tx_isp_vic_start */
	void (*complete)(u8 *c);				/* private_complete */
};

struct t32_vic_mdma {
	u32 frames_left;	/* t32_data_d9310 */
	u32 frame;		/* t32_data_d9318 */
	u32 dma_base;		/* t32_data_d9304 */
	u32 no_complete;	/* t32_data_d930c */
};

static inline int t32_vicd_valid(const u8 *p)
{
	return p && (uintptr_t)p < (uintptr_t)(u32)-4095;
}

static inline u8 *t32_vicd_priv(const u8 *sd)
{
	return T32_PTR(t32_g_rd32(sd, T32_VICD_SD_PRIV));
}

static inline int t32_vic_bad_subdev(void)	/* vic_sensor_ops_sync_sensor_attr_part_2 */
{
	isp_printf(2, "vic: invalid subdev\n", 0, 0);
	return -22;
}

static inline int t32_vic_core_ops_init(const struct t32_vicops_os *os, u8 *sd, const u32 *req)
{
	u8 *vic, *e;
	u32 idx, k, cfg[4];

	if (!t32_vicd_valid(sd))
		return t32_vic_bad_subdev();
	vic = t32_vicd_priv(sd);
	idx = req[1];
	if (req[0]) {
		if (t32_g_rd32(vic, T32_VICD_STATE(idx)) == T32_VICD_ST_ON)
			return 0;
		k = os->index_cal(vic, idx);
		e = vic + T32_VICD_VIN(idx);
		cfg[0] = t32_g_rd32(e, 0);
		cfg[1] = t32_g_rd32(e, 4);
		cfg[2] = t32_g_rd32(e, 8);
		memcpy(&cfg[3], vic + T32_VICD_CFG, 4);
		os->lib_init(k & 0xff, t32_g_rd32(e, T32_VICD_VIN_ATTR), cfg);
		t32_g_wr32(vic, T32_VICD_STATE(req[1]), T32_VICD_ST_ON);
		return 0;
	}
	k = os->index_cal(vic, idx);
	if (t32_g_rd32(vic, T32_VICD_STATE(t32_g_rd32(vic, T32_VICD_LINK(req[1])))) == T32_VICD_ST_OFF)
		return 0;
	os->lib_deinit();
	vic[T32_VICD_PAIR_A(k)] = 0;
	vic[T32_VICD_PAIR_B(k)] = 0;
	memset(vic + T32_VICD_BLK_A(k), 0, 16);
	vic[T32_VICD_PAIR_A(k) + 1] = 0;
	vic[T32_VICD_PAIR_B(k) + 1] = 0;
	memset(vic + T32_VICD_BLK_B(k), 0, 16);
	t32_g_wr32(vic, T32_VICD_STATE(req[1]), T32_VICD_ST_OFF);
	return 0;
}

static inline int t32_vic_sync_sensor_attr(u8 *sd, const u8 *vin)
{
	u8 *vic, *info;

	if (!t32_vicd_valid(sd))
		return t32_vic_bad_subdev();
	vic = t32_vicd_priv(sd);
	if (!t32_vicd_valid(vic))
		return t32_vic_bad_subdev();
	if (!vin)
		return 0;
	info = T32_PTR(t32_g_rd32(vin, T32_VICD_VIN_INFO));	/* the vendor keeps a stack copy of *vin */
	memcpy(vic + T32_VICD_VIN(t32_g_rd16(info, T32_VICD_INFO_INDEX)), vin, T32_VICD_VIN_BYTES);
	t32_g_wr32(vic, T32_VICD_SYNCED(t32_g_rd16(info, T32_VICD_INFO_INDEX)), 1);
	return 0;
}

static inline int t32_vic_sensor_ops_ioctl(const struct t32_vicops_os *os, u8 *sd, u32 cmd, const u32 *arg)
{
	u8 *vic;
	u32 k;

	if (!t32_vicd_valid(sd))
		return 0;
	vic = t32_vicd_priv(sd);
	if (!t32_vicd_valid(vic))
		return 0;
	switch (cmd) {
	case T32_VIC_IOC_START:
		k = os->index_cal(vic, arg[0]);
		os->lib_cfg(k & 0xff, t32_g_rd32(vic, T32_VICD_VIN(0) + T32_VICD_VIN_ATTR));
		return os->vic_start(vic, (u32)((int)arg[0] % 2));
	case T32_VIC_IOC_IRQ:
		*(volatile u32 *)(T32_PTR(t32_g_rd32(vic, T32_VICD_BASE)) + T32_VIC_REG_CTRL) = 0x10;
		return 0;
	case T32_VIC_IOC_RESTART:
		os->vic_start(vic, 0);
		return os->vic_start(vic, 1);
	case T32_VIC_IOC_STOP:
		if (t32_g_rd32(vic, T32_VICD_RUNNING)) {
			os->lib_stop();
			os->lib_reset();
		}
		return 0;
	case T32_VIC_IOC_CFG:
		memcpy(vic + T32_VICD_CFG, arg, T32_VICD_CFG_BYTES);
		isp_printf(0, "%s[%d]: vic cfg %d\n", "vic_sensor_ops_ioctl", 266, t32_g_rd32(vic, T32_VICD_CFG));
		return 0;
	case T32_VIC_IOC_LINK: {
		/* vendor UB kept: the request is read from address 0 instead of arg (an oops) */
		volatile const u32 *nul = (volatile const u32 *)(uintptr_t)0;

		k = os->index_cal(vic, nul[1]);
		t32_g_wr32(vic, T32_VICD_LINK_SET(k), nul[0]);
		return 0;
	}
	default:
		return 0;
	}
}

/* MDMA frame interrupt: the next frame address, or the completion after the last frame */
static inline void t32_vic_mdma_irq_function(const struct t32_vicops_os *os, struct t32_vic_mdma *m,
					     u8 *vic, u32 done)
{
	u32 size;

	if (!done && m->frames_left) {
		m->frame++;
		m->frames_left--;
		if (m->frames_left) {
			size = t32_g_rd32(vic, T32_VICD_WIDTH) * t32_g_rd32(vic, T32_VICD_HEIGHT) * 2;
			*(volatile u32 *)(T32_PTR(t32_g_rd32(vic, T32_VICD_BASE)) + T32_VIC_REG_MDMA_ADDR) =
				size * m->frame + m->dma_base;
			return;
		}
	} else if (done && m->frames_left) {
		return;
	}
	if (!m->no_complete)
		os->complete(vic + T32_VICD_COMPLETE);
}

#endif
