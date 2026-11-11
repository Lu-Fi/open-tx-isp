/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp core glue from the vendor module (Matt Davis' recovered C,
 * PR #15): the sensor event senders sensor_event_prepare_process (0xb6eb8),
 * _suspend_process (0xb6e3c), _resume_process (0xb6dc0), _complete_process
 * (0xb6d40), _streamoff_process (0xb6c74), and the media link teardown
 * subdev_video_destroy_link (0x16f20).
 *
 * The event senders hand a 3 word event { arg, type, value } to the notify
 * callback of the sensor subdev (+0x98) with the command 0x200001b; a NULL
 * subdev or callback, or an error of the callback, prints (level 2, vendor
 * line) and returns -22.  streamoff sends { i, 5, 100 } for i = 0 .. n - 1
 * and checks the subdev per event (n = 0 returns 0 for any subdev).
 * Only NULL is tested, not ERR_PTR (vendor).
 *
 * destroy_link: a link { source pad, sink pad, reverse link, flags }; with
 * flags != 0 the link (and its reverse) is cleared and both pads get the
 * state 2 (the source pad unchecked, as the vendor).
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_txcore6_oracle).
 */
#ifndef TX_ISP_T32_TXCORE6_H
#define TX_ISP_T32_TXCORE6_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_NOTIFY_SENSOR_EVENT	0x200001bU
#define T32_SEV_PREPARE		0U
#define T32_SEV_SUSPEND		1U
#define T32_SEV_RESUME		2U
#define T32_SEV_COMPLETE	3U
#define T32_SEV_STREAMOFF	5U

typedef int (*t32_txc_notify_fn)(u8 *sd, u32 cmd, void *arg);

static inline int t32_sensor_event_send(u8 *sd, u32 arg, u32 type, u32 value, u32 line)
{
	u32 ev[3] = { arg, type, value };
	u32 fn = sd ? t32_g_rd32(sd, T32_G_SD_NOTIFY) : 0;

	if (!fn || ((t32_txc_notify_fn)T32_PTR(fn))(sd, T32_NOTIFY_SENSOR_EVENT, ev)) {
		isp_printf(2, "%s[%d]: sensor event failed\n", "sensor_event", line);
		return -22;
	}
	return 0;
}

static inline int t32_sensor_event_prepare_process(u32 arg, u8 *sd)
{
	return t32_sensor_event_send(sd, arg, T32_SEV_PREPARE, 0, 4338);
}

static inline int t32_sensor_event_suspend_process(u32 arg, u8 *sd)
{
	return t32_sensor_event_send(sd, arg, T32_SEV_SUSPEND, 0, 4359);
}

static inline int t32_sensor_event_resume_process(u32 arg, u8 *sd)
{
	return t32_sensor_event_send(sd, arg, T32_SEV_RESUME, 0, 4380);
}

static inline int t32_sensor_event_complete_process(u32 arg, u8 *sd)
{
	return t32_sensor_event_send(sd, arg, T32_SEV_COMPLETE, 100, 4401);
}

static inline int t32_sensor_event_streamoff_process(u32 unused, u8 *sd, u32 unused2, u32 n)
{
	u32 i;
	int ret;

	(void)unused;
	(void)unused2;
	for (i = 0; i < n; i++) {
		ret = t32_sensor_event_send(sd, i, T32_SEV_STREAMOFF, 100, 4301);
		if (ret)
			return ret;
	}
	return 0;
}

/* struct tx_isp_link (16 bytes) and the pad state byte */
#define T32_LINK_SOURCE		0U
#define T32_LINK_SINK		4U
#define T32_LINK_REVERSE	8U
#define T32_LINK_FLAGS		12U
#define T32_PAD_STATE		7U
#define T32_PAD_STATE_FREE	2U

static inline int t32_subdev_video_destroy_link(u8 *link)
{
	u8 *src, *sink, *rev;

	if (!t32_g_rd32(link, T32_LINK_FLAGS))
		return 0;
	src = T32_PTR(t32_g_rd32(link, T32_LINK_SOURCE));
	sink = T32_PTR(t32_g_rd32(link, T32_LINK_SINK));
	rev = T32_PTR(t32_g_rd32(link, T32_LINK_REVERSE));
	memset(link, 0, 16);
	src[T32_PAD_STATE] = T32_PAD_STATE_FREE;
	if (rev)
		memset(rev, 0, 16);
	if (sink)
		sink[T32_PAD_STATE] = T32_PAD_STATE_FREE;
	return 0;
}

#endif
