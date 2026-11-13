/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MIPI CSI PHY stream-on of the vendor module:
 * mipi_phy_stream_on.constprop.5.
 *
 * The CSI device (subdev +280) keeps: +244 / +248 the register blocks of
 * the two CSI controllers, +340 + 104 * i the sensor info pointer of
 * sensor i (+20 bus type, +28 MIPI rate, +40 controller, +41 lanes),
 * +704 + 4 * i the sensor state, +732 a spinlock, +736 the PHY register
 * block, +856 the stream-on count, +860 the camera mode (1 single, 2 dual).
 * Register writes are plain stores into the blocks, in the vendor's order.
 * attr: +0 enable, +4 sensor index.
 *
 * Settle time = 115 * rate / 4000 - 3 (signed), 255 when out of 0..255.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK): in
 * single mode the lane / reset writes go to the controller of the
 * sensor's +40 byte, but +128 always to controller 0; a sensor bus type
 * other than 1 (MIPI) is skipped, and printed unless it is 2 (DVP).
 *
 * Calls (delay, lock, print) go through struct t32_mipi_ops.
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_mipi_oracle).
 */
#ifndef TX_ISP_T32_MIPI_H
#define TX_ISP_T32_MIPI_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#ifndef T32_HOST_S64
#define T32_HOST_S64
typedef int64_t s64;
typedef int32_t s32;
#endif
#endif

struct t32_mipi_ops {
	void (*udelay)(u32 us);
	void (*msleep)(u32 ms);
	void (*lock)(void *lock, u32 *flags);
	void (*unlock)(void *lock, u32 flags);
	int (*isp_printf)(int level, const char *fmt, ...);
};

static inline u32 *t32_mipi_w(u8 *p, u32 off)
{
	return (u32 *)(p + off);
}

static inline u8 *t32_mipi_p(u8 *p, u32 off)	/* a 32-bit pointer field */
{
	return (u8 *)(uintptr_t)*t32_mipi_w(p, off);
}

static inline u32 t32_mipi_settle(u32 rate)
{
	u32 t = (u32)((s32)(115 * rate) / 4000) - 3;

	return t < 256 ? t : 255;
}

/* controller reset pulse: +12 low, 1 ms (udelay when fast_delay), high */
static inline void t32_mipi_ctrl_pulse(const struct t32_mipi_ops *k, u8 *csi, u32 fast_delay)
{
	*t32_mipi_w(t32_mipi_p(csi, 244), 12) = 0;
	if (fast_delay)
		k->udelay(1000);
	else
		k->msleep(1);
	*t32_mipi_w(t32_mipi_p(csi, 244), 12) = 1;
}

static inline void t32_mipi_count_up(const struct t32_mipi_ops *k, u8 *csi)
{
	u32 flags = 0;

	k->lock(csi + 732, &flags);
	++*t32_mipi_w(csi, 856);
	k->unlock(csi + 732, flags);
}

/* sd: the CSI subdev (+280 the CSI device); fast_delay: d9008 */
static inline int t32_mipi_phy_stream_on(const struct t32_mipi_ops *k, u8 *sd, const u32 *attr, u32 fast_delay)
{
	u8 *csi = NULL, *info, *r, *r1, *phy;
	u32 idx, ctl, mode, t0, t1, v;
	int i;

	if (sd && (u32)(uintptr_t)sd < (u32)-4095)
		csi = t32_mipi_p(sd, 280);
	if (!csi || (u32)(uintptr_t)csi >= (u32)-4095)
		return -22;
	idx = attr[1];
	if ((s32)*t32_mipi_w(csi, 704 + 4 * idx) < 2)
		return 0;
	if (!attr[0])
		return 0;
	mode = *t32_mipi_w(csi, 860);
	phy = t32_mipi_p(csi, 736);
	if (mode == 1) {
		info = t32_mipi_p(csi, 340 + 104 * idx);
		if (*t32_mipi_w(info, 20) != 1) {	/* not MIPI */
			if (*t32_mipi_w(info, 20) != 2)
				k->isp_printf(1, "The sensor dbus_type is %d\n", *t32_mipi_w(info, 20));
			return 0;
		}
		ctl = info[40];
		if (!*t32_mipi_w(csi, 856)) {
			t32_mipi_ctrl_pulse(k, csi, fast_delay);
			t32_mipi_count_up(k, csi);
		}
		r = t32_mipi_p(csi, 244 + 4 * ctl);
		*t32_mipi_w(r, 4) = (u32)info[41] - 1;
		*t32_mipi_w(r, 256) = info[41] == 4 ? 15 : 3;
		*t32_mipi_w(r, 8) = 0;
		*t32_mipi_w(r, 16) = 0;
		*t32_mipi_w(r, 16) = 1;
		*t32_mipi_w(r, 40) = 0xffffffffu;
		*t32_mipi_w(r, 44) = 0xffffffffu;
		*t32_mipi_w(phy, 0) = 125;
		*t32_mipi_w(t32_mipi_p(csi, 244), 128) = ctl == 1 ? ctl : 0;
		*t32_mipi_w(phy, 128) = ctl == 1 ? 95 : 31;
		v = info[40] ? 15 : 63;
		for (i = 0; i < 6; i++)
			*t32_mipi_w(phy, 296 + 128 * i) = v;
		t0 = t32_mipi_settle(*t32_mipi_w(info, 28));
		k->isp_printf(0, "csi settle_time[0] = 0x%x\n", t0);
		for (i = 0; i < 5; i++)
			*t32_mipi_w(phy, 352 + 128 * i) = t0;
		return 0;
	}
	if (mode != 2 || *t32_mipi_w(csi, 856))
		return 0;
	t32_mipi_ctrl_pulse(k, csi, fast_delay);
	r = t32_mipi_p(csi, 244);
	r1 = t32_mipi_p(csi, 248);
	info = t32_mipi_p(csi, 340);
	*t32_mipi_w(r, 4) = (u32)info[41] - 1;
	*t32_mipi_w(r1, 4) = (u32)t32_mipi_p(csi, 444)[41] - 1;
	*t32_mipi_w(r, 256) = 3;
	*t32_mipi_w(r1, 256) = 3;
	*t32_mipi_w(r, 8) = 0;
	*t32_mipi_w(r1, 8) = 0;
	*t32_mipi_w(r, 16) = 0;
	*t32_mipi_w(r1, 16) = 0;
	*t32_mipi_w(r, 16) = 1;
	*t32_mipi_w(r1, 16) = 1;
	*t32_mipi_w(r, 40) = 0xffffffffu;
	*t32_mipi_w(r1, 40) = 0xffffffffu;
	*t32_mipi_w(r, 44) = 0xffffffffu;
	*t32_mipi_w(r1, 44) = 0xffffffffu;
	*t32_mipi_w(phy, 0) = 125;
	*t32_mipi_w(r, 128) = 1;
	*t32_mipi_w(phy, 128) = 95;
	for (i = 0; i < 6; i++)
		*t32_mipi_w(phy, 296 + 128 * i) = 15;
	t0 = t32_mipi_settle(*t32_mipi_w(t32_mipi_p(csi, 340), 28));
	t1 = t32_mipi_settle(*t32_mipi_w(t32_mipi_p(csi, 444), 28));
	k->isp_printf(0, "csi settle_time[0] = 0x%x\n", t0);
	k->isp_printf(0, "csi settle_time[1] = 0x%x\n", t1);
	for (i = 0; i < 3; i++)
		*t32_mipi_w(phy, 352 + 128 * i) = t0;
	*t32_mipi_w(phy, 736) = t1;
	*t32_mipi_w(phy, 864) = t1;
	t32_mipi_count_up(k, csi);
	return 0;
}

#endif /* TX_ISP_T32_MIPI_H */
