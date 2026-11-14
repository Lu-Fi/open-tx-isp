/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) D-PHY settle time (openimp #99), binary oracle block "mipi"
 * (tests/t33_binoracle/mipi.conf): get_settle_time, the vendor's table from
 * the sensor's MIPI clock (Mbps, sensor attribute +0x1c) that
 * mipi_phy_stream_on writes to the D-PHY lane words 0x160 + n * 0x80.  The
 * T32 module computes 115 * clk / 4000 - 3 instead (tx_isp_t32_vic.h).
 */
#ifndef TX_ISP_T33_MIPI_H
#define TX_ISP_T33_MIPI_H

/* { clock band end (Mbps, exclusive), settle in the band }; 2400 and above: 0x46 */
static const short t33_settle_tab[][2] = {
	{ 110, 2 }, { 150, 3 }, { 300, 6 }, { 400, 8 }, { 500, 0xb }, { 600, 0xe }, { 700, 0x10 },
	{ 800, 0x12 }, { 1000, 0x16 }, { 1200, 0x1e }, { 1400, 0x23 }, { 1600, 0x2d }, { 1800, 0x32 },
	{ 2000, 0x37 }, { 2200, 0x3c }, { 2400, 0x41 },
};

/* get_settle_time (signed clock: negatives give 2) */
static inline unsigned int t33_settle_time(int clk)
{
	unsigned int i;

	for (i = 0; i < sizeof(t33_settle_tab) / sizeof(t33_settle_tab[0]); i++)
		if (clk < t33_settle_tab[i][0])
			return t33_settle_tab[i][1];
	return 0x46;
}

/*
 * mipi_phy_stream_on (openimp #99), binary oracle block "mipiphy" (tests/t33_binoracle/mipiphy.conf).
 *
 * The CSI device keeps the register blocks of its two CSI hosts, the D-PHY block, the sensor attributes (bus type,
 * MIPI clock, CSI index, lanes), a state per sensor and the camera mode (1 single, 2 dual).  Other than the T32
 * module (tx_isp_t32_mipi.h) the T33 one
 *  - pulses host 0 (0x0c and 0x10 low, 1 ms, high) at every stream-on, not only at the first,
 *  - writes no host 0x08 / 0x28 / 0x2c / 0x80 in single mode, no D-PHY lane words 0x128 + n * 0x80, and the D-PHY
 *    word 0x80 (0xdf) only when host 1 is used: for a sensor on host 0 they keep their reset values,
 *  - takes the settle time from get_settle_time's table.
 * Register writes go through struct t33_mipi_ops in the vendor's order; ctl is the CSI host (0 / 1).
 * Returns 0, or -1 (after the vendor's message) for more lanes than a host has.
 */
struct t33_mipi_sensor {
	unsigned int dbus;	/* +0x14: 1 MIPI, 2 DVP */
	int clk;		/* +0x1c: MIPI clock, Mbps */
	unsigned char index;	/* +0x28: CSI host */
	unsigned char lanes;	/* +0x29 */
};

#define T33_MIPI_LOG_DBUS	0	/* The sensor dbus_type is %d */
#define T33_MIPI_LOG_LANES1	1	/* The number of lanes in MIPI CSI [1] cannot exceed 2. */
#define T33_MIPI_LOG_DUAL0	2	/* In the dual-camera mode, the maximum number of lanes for MIPI CSI[0] is 2. */
#define T33_MIPI_LOG_DUAL1	3	/* In the dual-camera mode, the maximum number of lanes for MIPI CSI[1] is 2. */
#define T33_MIPI_LOG_SETTLE0	4	/* dual mode: the settle time of CSI 0 */
#define T33_MIPI_LOG_SETTLE1	5	/* dual mode: the settle time of CSI 1 */

struct t33_mipi_ops {
	void (*host)(unsigned int ctl, unsigned int reg, unsigned int val);
	void (*phy)(unsigned int reg, unsigned int val);
	void (*sleep_1ms)(void);	/* private_msleep(1); private_mdelay(1) on the fast-boot kernels */
	void (*log)(int what, unsigned int val);
};

static inline void t33_mipi_host0_pulse(const struct t33_mipi_ops *k)
{
	k->host(0, 0x0c, 0);
	k->host(0, 0x10, 0);
	k->sleep_1ms();
	k->host(0, 0x0c, 1);
	k->host(0, 0x10, 1);
}

/* state: the sensor's CSI state (streams from 2 on); idx: the sensor (0 / 1); s: the sensor attributes [idx] */
static inline int t33_mipi_phy_stream_on(const struct t33_mipi_ops *k, unsigned int mode, int state, unsigned int idx,
					 const struct t33_mipi_sensor *s)
{
	unsigned int lanes, t;

	if (state < 2)
		return 0;
	if (mode == 1) {
		s += idx;
		if (s->dbus != 1) {
			if (s->dbus != 2)
				k->log(T33_MIPI_LOG_DBUS, s->dbus);
			return 0;
		}
		lanes = s->lanes;
		if (s->index == 0) {
			t33_mipi_host0_pulse(k);
			k->host(0, 0x04, lanes - 1);
			k->host(0, 0x100, lanes == 4 ? 0xf : 3);
			k->phy(0, 0x7d);
			t = t33_settle_time(s->clk);
			k->phy(0x160, t);
			k->phy(0x1e0, t);
			k->phy(0x260, t);
		} else if (s->index == 1) {
			t33_mipi_host0_pulse(k);
			k->host(1, 0x10, 1);
			if (lanes > 2) {
				k->log(T33_MIPI_LOG_LANES1, lanes);
				return -1;
			}
			k->host(1, 0x04, lanes - 1);
			k->host(1, 0x100, 3);
			k->phy(0, 0x7d);
			k->phy(0x80, 0xdf);
			t = t33_settle_time(s->clk);
			k->phy(0x3e0, t);
		} else {
			return 0;
		}
		k->phy(0x2e0, t);
		k->phy(0x360, t);
		return 0;
	}
	if (mode != 2)
		return 0;
	if (idx == 0) {
		t33_mipi_host0_pulse(k);
		k->host(1, 0x10, 1);
		k->host(0, 0x80, 1);
		lanes = s[0].lanes;
		if (lanes > 2) {
			k->log(T33_MIPI_LOG_DUAL0, lanes);
			return -1;
		}
		k->host(0, 0x04, lanes - 1);
		k->host(0, 0x100, 3);
		k->phy(0, 0x7d);
		k->phy(0x80, 0xdf);
		t = t33_settle_time(s[0].clk);
		k->log(T33_MIPI_LOG_SETTLE0, t);
		k->phy(0x160, t);
		k->phy(0x1e0, t);
		k->phy(0x260, t);
		return 0;
	}
	lanes = s[1].lanes;
	if (lanes > 2) {
		k->log(T33_MIPI_LOG_DUAL1, lanes);
		return -1;
	}
	k->host(1, 0x04, lanes - 1);
	k->host(1, 0x100, 3);
	t = t33_settle_time(s[1].clk);
	k->log(T33_MIPI_LOG_SETTLE1, t);
	k->phy(0x3e0, t);
	k->phy(0x2e0, t);
	k->phy(0x360, t);
	return 0;
}

#endif /* TX_ISP_T33_MIPI_H */
