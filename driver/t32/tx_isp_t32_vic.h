/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC input configuration and MIPI CSI / D-PHY start values, derived
 * from the sensor attributes (struct tx_isp_sensor_attribute, offsets in
 * tx_isp_t32_abi.h).
 *
 * How the values follow from the attributes was read from the T32 vendor
 * module's VIC configuration and MIPI PHY start routines (RE reference, our
 * own code) and checked against the vendor stack's QEMU register trace for
 * the cv8001 (S0 run r10: 2-lane RAW10 MIPI, 3840x2160, 1000 Mbps):
 *
 * VIC (base 0x80000 + index * 0x1000), written in this order:
 *   0x1a4  0x000a000a (MIPI "other" mode; Sony mode: 0x20 = 0x20000 and
 *          0x1a0 = 0x100010 instead)
 *   0x100  line length in 32-bit words, ceil(bits per pixel * twidth / 32)
 *   0x01c  2 (MIPI input)
 *   0x024  csi_fmt (0 RAW8, 1 RAW10, 2 RAW12, 7 YUV422)
 *   0x014  w << 16 | h (sensor output size)
 *   0x10c  the mipi_sc control word (bit layout as T41: hcrop_diff_en 25,
 *          vcomp 24, hcomp 23, line_sync 22, work_start 20, dt_en 18,
 *          dt_value 12, del_start 8, frame_mode 4, fid 2, sensor_mode 0)
 *   0x110  twidth << 16 | crop0x, 0x114 crop1x, 0x118 crop2x, 0x11c crop3x
 *   linear: 0x200 0x80000011, 0x204 0, 0x1ac 0x20000, 0x1a8 0x4440
 *   (the single-sensor online mode the vendor uses; WDR 2/3 frames:
 *   0x1ac = 0x1a8 = 0x4410 / 0x4420)
 *   0x1b0  0x10
 *   0x1a0  frame_mode << 4 | sensor_mode
 *   0x104  crop1y << 16 | crop0y, 0x108 crop3y << 16 | crop2y
 *
 * CSI host (0x10054000) and D-PHY (0x10022000), single-CSI path, index 0:
 *   host 0x0c 0, 1 (once per load), 0x04 lanes - 1,
 *   0x100 lanes == 4 ? 0xf : 3, 0x08 0, 0x10 0 then 1, 0x28/0x2c ~0,
 *   phy 0x00 0x7d, host 0x80 0, phy 0x80 0x1f,
 *   phy 0x128 + n * 0x80 (n 0..5) 0x3f,
 *   phy 0x160 + n * 0x80 (n 0..4) settle = 115 * clk / 4000 - 3 (255 if
 *   out of 0..255): 0x19 at 1000 Mbps.
 * CSI index 1 (second host, 0x5f / 0x0f values) and DVP sensors are not
 * supported here (no reference; refused with -EOPNOTSUPP).
 *
 * Pure helpers, shared with the host test tests/tx_isp_t32_vic_test.c.
 */
#ifndef TX_ISP_T32_VIC_H
#define TX_ISP_T32_VIC_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#include <errno.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
typedef uint16_t u16;
typedef uint8_t u8;
#endif

#include "tx_isp_t32_abi_attr.h"

#define T32_VIC_MAX_WRITES		32

struct t32_reg_write {
	u32 off;
	u32 val;
};

struct t32_reg_seq {
	unsigned int n;
	struct t32_reg_write w[T32_VIC_MAX_WRITES];
};

static inline void t32_seq_add(struct t32_reg_seq *s, u32 off, u32 val)
{
	if (s->n < T32_VIC_MAX_WRITES) {
		s->w[s->n].off = off;
		s->w[s->n].val = val;
		s->n++;
	}
}

static inline u32 t32_attr_u32(const void *attr, u32 off)
{
	u32 v;

	memcpy(&v, (const u8 *)attr + off, sizeof(v));
	return v;
}

static inline u32 t32_attr_u16(const void *attr, u32 off)
{
	u16 v;

	memcpy(&v, (const u8 *)attr + off, sizeof(v));
	return v;
}

static inline u32 t32_attr_u8(const void *attr, u32 off)
{
	return ((const u8 *)attr)[off];
}

/* bits per pixel of a csi_fmt, 0 for an unknown one */
static inline u32 t32_csi_fmt_bpp(u32 fmt)
{
	switch (fmt) {
	case 0: return 8;
	case 1: return 10;
	case 2: return 12;
	case 7: return 16;
	}
	return 0;
}

/*
 * VIC input configuration (offsets relative to the VIC base) for a sensor
 * putting out w x h.  Returns 0 or -EOPNOTSUPP / -EINVAL.
 */
static inline int t32_vic_input_seq(const void *attr, u32 w, u32 h,
				    struct t32_reg_seq *s)
{
	u32 fmt, bpp, tw, fm, word;

	s->n = 0;
	if (t32_attr_u32(attr, T32_ATTR_DBUS_TYPE) != T32_DBUS_MIPI)
		return -EOPNOTSUPP;
	fmt = t32_attr_u32(attr, T32_ATTR_SC_CSI_FMT);
	bpp = t32_csi_fmt_bpp(fmt);
	tw = t32_attr_u32(attr, T32_ATTR_MIPI_TWIDTH);
	fm = t32_attr_u32(attr, T32_ATTR_SC_FRAME_MODE);
	if (!bpp || !w || !h || fm > 2)
		return -EINVAL;
	if (!tw)
		tw = w;

	if (t32_attr_u32(attr, T32_ATTR_MIPI_MODE) == 1) {
		t32_seq_add(s, 0x020, 0x20000);
		t32_seq_add(s, 0x1a0, 0x100010);
	} else {
		t32_seq_add(s, 0x1a4, 0x000a000a);
	}
	t32_seq_add(s, 0x100, (bpp * tw + 31) / 32);
	t32_seq_add(s, 0x01c, 2);
	t32_seq_add(s, 0x024, fmt);
	t32_seq_add(s, 0x014, (w << 16) | h);
	word = t32_attr_u32(attr, T32_ATTR_SC_HCROP_DIFF_EN) << 25 |
	       t32_attr_u32(attr, T32_ATTR_SC_VCOMP_EN) << 24 |
	       t32_attr_u32(attr, T32_ATTR_SC_HCOMP_EN) << 23 |
	       t32_attr_u32(attr, T32_ATTR_SC_LINE_SYNC) << 22 |
	       t32_attr_u32(attr, T32_ATTR_SC_WORK_START) << 20 |
	       t32_attr_u32(attr, T32_ATTR_SC_DT_EN) << 18 |
	       t32_attr_u32(attr, T32_ATTR_SC_DT_VALUE) << 12 |
	       t32_attr_u32(attr, T32_ATTR_SC_DEL_START) << 8 |
	       fm << 4 |
	       t32_attr_u32(attr, T32_ATTR_SC_FID_MODE) << 2 |
	       t32_attr_u32(attr, T32_ATTR_SC_SENSOR_MODE);
	t32_seq_add(s, 0x10c, word);
	t32_seq_add(s, 0x110, (tw << 16) | t32_attr_u16(attr, T32_ATTR_SC_CROP0X));
	t32_seq_add(s, 0x114, t32_attr_u16(attr, T32_ATTR_SC_CROP0X + 4));
	t32_seq_add(s, 0x118, t32_attr_u16(attr, T32_ATTR_SC_CROP0X + 8));
	t32_seq_add(s, 0x11c, t32_attr_u16(attr, T32_ATTR_SC_CROP0X + 12));
	switch (fm) {
	case 0:
		t32_seq_add(s, 0x200, 0x80000011);
		t32_seq_add(s, 0x204, 0);
		t32_seq_add(s, 0x1ac, 0x20000);
		t32_seq_add(s, 0x1a8, 0x4440);
		break;
	case 1:
		t32_seq_add(s, 0x1ac, 0x4410);
		t32_seq_add(s, 0x1a8, 0x4410);
		break;
	case 2:
		t32_seq_add(s, 0x1ac, 0x4420);
		t32_seq_add(s, 0x1a8, 0x4420);
		break;
	}
	t32_seq_add(s, 0x1b0, 0x10);
	t32_seq_add(s, 0x1a0, fm << 4 | t32_attr_u32(attr, T32_ATTR_SC_SENSOR_MODE));
	t32_seq_add(s, 0x104, t32_attr_u16(attr, T32_ATTR_SC_CROP0X + 6) << 16 |
			      t32_attr_u16(attr, T32_ATTR_SC_CROP0X + 2));
	t32_seq_add(s, 0x108, t32_attr_u16(attr, T32_ATTR_SC_CROP0X + 14) << 16 |
			      t32_attr_u16(attr, T32_ATTR_SC_CROP0X + 10));
	return 0;
}

/* MIPI CSI start values of the single-CSI path */
struct t32_csi_cfg {
	u32 lanes_m1;		/* host 0x04 */
	u32 host_100;		/* host 0x100 */
	u32 phy_80;		/* phy 0x80 */
	u32 phy_lane;		/* phy 0x128 + n * 0x80 */
	u32 settle;		/* phy 0x160 + n * 0x80 */
};

static inline u32 t32_csi_settle(u32 clk_mbps)
{
	int v = (int)((115 * clk_mbps) / 4000) - 3;

	return (v < 0 || v > 255) ? 255 : (u32)v;
}

static inline int t32_csi_cfg_from_attr(const void *attr, struct t32_csi_cfg *c)
{
	u32 lanes = t32_attr_u8(attr, T32_ATTR_MIPI_LANES);

	if (t32_attr_u32(attr, T32_ATTR_DBUS_TYPE) != T32_DBUS_MIPI ||
	    t32_attr_u8(attr, T32_ATTR_MIPI_INDEX) != 0)
		return -EOPNOTSUPP;
	if (lanes < 1 || lanes > 4)
		return -EINVAL;
	c->lanes_m1 = lanes - 1;
	c->host_100 = lanes == 4 ? 0xf : 3;
	c->phy_80 = 0x1f;
	c->phy_lane = 0x3f;
	c->settle = t32_csi_settle(t32_attr_u32(attr, T32_ATTR_MIPI_CLK));
	return 0;
}

#endif /* TX_ISP_T32_VIC_H */
