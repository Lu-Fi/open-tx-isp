/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp video links and device release: vendor
 * tx_isp_video_link_destroy (.isra) and tx_isp_release.
 *
 * Link sets (vendor table bed7c, data.c:1922): 12 { descriptor array,
 * count } pairs (3, 3, 3, 3, 4, 4, 4, 4, 6, 6, 6, 6 links of 20 bytes:
 * source pad at +0, sink pad at +8).  The descriptor arrays name pads by
 * subdevice; the table is provided by the driver glue (t32_tx_link_sets).
 * Set index of an input: input (+8 when the input has a sensor at +308),
 * or input + 4 in the multi-sensor modes (vendor d9014: mode 1 input 0,
 * mode 2 inputs < 2, mode 3 < 3, mode 4 < 4).
 *
 * The isra link destroy takes the ISP device - 12: +308 + 4 i sensor of
 * input i, +324 + 4 s link count of set s.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txlink_oracle).
 */
#ifndef TX_ISP_T32_TXLINK_H
#define TX_ISP_T32_TXLINK_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txcore.h"

#define T32_LINK_SETS		12U
#define T32_LINK_BYTES		20U
#define T32_LINK_SINK		8U
#define T32_PAD_LINK		8U
#define T32_LK_SENSOR(i)	(308U + 4U * (i))
#define T32_LK_COUNT(s)		(324U + 4U * (s))

#define T32_DEV_LINKED(i)	(312U + 4U * (i))	/* ISP device: input i linked */
#define T32_DEV_SENSOR_OF(i)	(296U + 4U * (i))
#define T32_DEV_BUFS		360U			/* 16 { ptr, 8 bytes } */
#define T32_DEV_BUFS_END	552U
#define T32_DEV_BUF_BYTES	12U
#define T32_PM_RELEASE		4U

struct t32_tx_link_set {
	u32 links, n;
};

/* provided by the driver glue (kernel) or the oracle (host) */
extern struct t32_tx_link_set t32_tx_link_sets[T32_LINK_SETS];
extern u32 t32_tx_link_mode;		/* vendor d9014 */
u32 t32_tx_os_find_subdev_link_pad(u32 isp, u32 desc);
int t32_tx_os_subdev_video_destroy_link(u32 link);
void t32_tx_os_kfree(u32 p);
void t32_tx_os_memset(u32 a, u32 c, u32 n);

/* tx_isp_video_link_destroy (.isra)(isp - 12, input) */
static inline int t32_tx_video_link_destroy(u32 lk, u32 input)
{
	u32 set = input, m = t32_tx_link_mode, k, d;

	if (t32_tx_ld(lk, T32_LK_SENSOR(input)))
		set = input + 8;
	if ((m == 1 && input == 0) || (m == 2 && input < 2) || (m == 3 && input < 3) || (m == 4 && input < 4))
		set = input + 4;
	if ((int)t32_tx_ld(lk, T32_LK_COUNT(set)) <= 0)
		return 0;
	d = t32_tx_link_sets[set].links;
	for (k = 0; (int)k < (int)t32_tx_link_sets[set].n; k++, d += T32_LINK_BYTES) {
		u32 src = t32_tx_os_find_subdev_link_pad(lk, d), snk = t32_tx_os_find_subdev_link_pad(lk, d + T32_LINK_SINK);
		int r;

		if (!src || !snk)
			continue;
		t32_tx_os_subdev_video_destroy_link(src + T32_PAD_LINK);
		r = t32_tx_os_subdev_video_destroy_link(snk + T32_PAD_LINK);
		if (r && r != -(int)T32_ENOIOCTLCMD)
			return r;
	}
	t32_tx_st(lk, T32_LK_COUNT(set), 0);
	return 0;
}

/* tx_isp_release(inode, file): the last release runs every subdevice's release, drops links and buffers */
static inline int t32_tx_isp_release(u32 inode, u32 file)
{
	u32 dev = t32_tx_ld(file, 120), k, a;
	int ret = 0;

	(void)inode;
	if (t32_tx_ld(dev, T32_DEV_OPENS)) {
		t32_tx_st(dev, T32_DEV_OPENS, t32_tx_ld(dev, T32_DEV_OPENS) - 1);
		return 0;
	}
	for (k = 0; k < T32_DEV_SD_N; k++) {
		u32 sd = t32_tx_ld(dev, T32_DEV_OPEN_SD + 4 * k), fn;
		int r;

		if (!sd)
			continue;
		fn = t32_tx_sd_pm(sd, T32_PM_RELEASE);
		if (!fn) {
			ret = -(int)T32_ENOIOCTLCMD;
			continue;
		}
		r = ((t32_tx_open_fn)T32_TX_P(fn))(sd, file);
		if (r && r != -(int)T32_ENOIOCTLCMD) {
			ret = r;
			goto bufs;
		}
		ret = r;
	}
	for (k = 0; k < 4; k++) {
		if (t32_tx_ld(dev, T32_DEV_LINKED(k)))
			t32_tx_video_link_destroy(dev - 12, k);
		t32_tx_st(dev, T32_DEV_SENSOR_OF(k), 0);
	}
	ret = 0;
bufs:
	for (a = dev + T32_DEV_BUFS; a != dev + T32_DEV_BUFS_END; a += T32_DEV_BUF_BYTES)
		if (t32_tx_ld(a, 0)) {
			t32_tx_os_kfree(t32_tx_ld(a, 0));
			t32_tx_os_memset(a, 0, T32_DEV_BUF_BYTES);
		}
	return ret;
}

#endif
