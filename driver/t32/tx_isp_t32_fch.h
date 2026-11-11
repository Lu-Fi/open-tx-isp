/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame channel (/dev/framechanN) file operations from the vendor module
 * (Matt Davis' recovered C, PR #15): frame_channel_vb2_streamoff (0x13b44),
 * frame_channel_vidioc_get_fmt (0x108dc) and frame_channel_open_fast
 * (0x1444c), frame_channel_release (0x12190).
 *
 * Channel (816 bytes, see tx_isp_t32_fs_pm.h): vb2 queue +0x2c, queue memory
 * +0x214, current format +0x248 (116 bytes), remote subdev +0x2cc, channel
 * index +0x2d0, mutex +0x2d4, state +0x2e0 (< 2 slaked, 3 opened), completion
 * +0x2e4 (done +0x2f0, wait list +0x2f4/+0x2f8), frame counters +0x2fc/+0x300,
 * "argument is a kernel pointer" flag +0x328.  User copies, the remote event,
 * the vb2 calls, mutex and completion are hooks of struct t32_fch_os.
 *
 * Vendor quirks kept: get_fmt prints "Failed to set fmt" when the remote
 * get-format event fails, accepts -515 (no handler) as success, and always
 * reports 4 / 8 in the words +16 / +28 of the format; open_fast indexes the
 * channel table with the minor unchecked (an out-of-range read for a bad
 * minor) and initialises the completion inline when d9008 is set and d9004
 * is clear; release indexes its module tables (f37d4, f37a4, f3774, d9954)
 * with the channel index unchecked, and its second stream-off (the channel
 * read again from the file, i.e. the same channel, already out of state 4)
 * can never run.  Ours zeroes the format buffer before the event (the vendor
 * leaves the bytes the event does not fill as stack contents).
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_fch_oracle).
 */
#ifndef TX_ISP_T32_FCH_H
#define TX_ISP_T32_FCH_H

#include "tx_isp_t32_fs_pm.h"

#define T32_FCH_FMT		0x248U
#define T32_FCH_FMT_BYTES	116U
#define T32_FCH_REMOTE		0x2ccU
#define T32_FCH_INDEX		0x2d0U
#define T32_FCH_MUTEX		0x2d4U
#define T32_FCH_DONE		0x2e4U
#define T32_FCH_DONE_CNT	0x2f0U
#define T32_FCH_DONE_WAIT	0x2f4U
#define T32_FCH_FRM_A		0x2fcU
#define T32_FCH_FRM_B		0x300U
#define T32_FCH_KARG		0x328U
#define T32_FCH_ST_OPEN		3U
#define T32_EV_GET_FMT		0x3000001U
#define T32_ENOIOCTLCMD		(-515)

struct t32_fch_os {
	u32 (*copy_from_user)(u8 *dst, u32 user, u32 n);	/* bytes left */
	u32 (*copy_to_user)(u32 user, const u8 *src, u32 n);
	int (*send_event)(u32 sd, u32 cmd, u8 *data);		/* tx_isp_send_event_to_remote */
	int (*streamoff)(u8 *ch, u32 type);			/* __frame_channel_vb2_streamoff */
	void (*mutex_lock)(u8 *m);
	void (*mutex_unlock)(u8 *m);
	void (*init_completion)(u8 *c);
	void (*queue_free)(u8 *q, u32 mem);			/* __vb2_queue_free */
	const u32 *d9008, *d9004;				/* module flags of the completion init */
	void (*kfree)(u32 p);					/* private_kfree */
};

static inline int t32_frame_channel_vb2_streamoff(const struct t32_fch_os *os, u8 *ch, u32 arg)
{
	u32 type;

	if (!t32_fs_valid(ch))
		return -22;
	if (!t32_fs_valid(T32_PTR(arg))) {
		isp_printf(2, "The parameter from user is invalid!\n");
		return -22;
	}
	if (t32_g_rd32(ch, T32_FCH_KARG)) {
		memcpy(&type, T32_PTR(arg), 4);
	} else if (os->copy_from_user((u8 *)&type, arg, 4)) {
		isp_printf(2, "Failed to copy from user\n");
		return -12;
	}
	return os->streamoff(ch, type);
}

static inline int t32_frame_channel_vidioc_get_fmt(const struct t32_fch_os *os, u8 *ch, u32 arg)
{
	u8 fmt[T32_FCH_FMT_BYTES];
	int r;

	if (!t32_fs_valid(ch))
		return -22;
	memset(fmt, 0, sizeof(fmt));
	r = os->send_event(t32_g_rd32(ch, T32_FCH_REMOTE), T32_EV_GET_FMT, fmt);
	if (r && r != T32_ENOIOCTLCMD) {
		isp_printf(2, "Failed to set fmt to frame chan%d!\n", t32_g_rd32(ch, T32_FCH_INDEX));
		return r;
	}
	t32_g_wr32(fmt, 28, 8);
	t32_g_wr32(fmt, 16, 4);
	t32_g_wr32(fmt, 0, 1);
	if (os->copy_to_user(arg, fmt, T32_FCH_FMT_BYTES)) {
		isp_printf(2, "Failed to copy to user\n");
		return -12;
	}
	memcpy(ch + T32_FCH_FMT, fmt, T32_FCH_FMT_BYTES);
	return 0;
}

static inline int t32_frame_channel_open_fast(const struct t32_fch_os *os, const u32 *chans, u32 minor)
{
	u8 *ch = T32_PTR(chans[minor]);

	if (!t32_fs_valid(ch))
		return -22;
	os->mutex_lock(ch + T32_FCH_MUTEX);
	if ((int)t32_g_rd32(ch, T32_FCH_STATE) < 2) {
		os->mutex_unlock(ch + T32_FCH_MUTEX);
		isp_printf(2, "Frame channel%d is slake now, Please activate it firstly!\n",
			   t32_g_rd32(ch, T32_FCH_INDEX));
		return -1;
	}
	os->mutex_unlock(ch + T32_FCH_MUTEX);
	memset(ch + T32_FCH_FMT, 0, T32_FCH_FMT_BYTES);
	t32_g_wr32(ch, T32_FCH_FRM_A, 0);
	t32_g_wr32(ch, T32_FCH_FRM_B, 0);
	if (*os->d9008 && !*os->d9004) {
		t32_g_wr32(ch, T32_FCH_DONE_CNT, 0);
		t32_g_wr32(ch, T32_FCH_DONE_WAIT, T32_WORD(ch + T32_FCH_DONE_WAIT));
		t32_g_wr32(ch, T32_FCH_DONE_WAIT + 4, T32_WORD(ch + T32_FCH_DONE_WAIT));
	} else {
		os->init_completion(ch + T32_FCH_DONE);
	}
	os->queue_free(ch + T32_FCH_VBQ, t32_g_rd32(ch, T32_FCH_QMEM));
	t32_g_wr32(ch, T32_FCH_STATE, T32_FCH_ST_OPEN);
	return 0;
}

/* module tables of frame_channel_release, indexed by the channel index */
struct t32_fch_tabs {
	const u32 *f37d4, *f37a4, *f3774;	/* all three set: the channel stream is stopped again */
	u32 *d9954;				/* per-channel buffer freed when d9004 is set */
};

static inline void t32_fch_stop_stream(const struct t32_fch_os *os, u8 *ch)
{
	if (t32_g_rd32(ch, T32_FCH_STATE) != T32_FCH_ST_STREAM)
		return;
	os->streamoff(ch, t32_g_rd32(ch, T32_FCH_VBQ));
	os->queue_free(ch + T32_FCH_VBQ, t32_g_rd32(ch, T32_FCH_QMEM));
	t32_g_wr32(ch, T32_FCH_STATE, T32_FS_ST_ACTIVE);
}

static inline int t32_frame_channel_release(const struct t32_fch_os *os, const struct t32_fch_tabs *tb, u8 *file)
{
	u8 *ch = T32_PTR(t32_g_rd32(file, 120));	/* file->private_data */
	u32 idx;

	if (!t32_fs_valid(ch))
		return -22;
	t32_fch_stop_stream(os, ch);
	idx = t32_g_rd32(ch, T32_FCH_INDEX);
	if (*os->d9008 && tb->f37d4[idx] && tb->f37a4[idx] && tb->f3774[idx])
		t32_fch_stop_stream(os, ch);
	if (*os->d9004 && tb->d9954[idx]) {
		os->kfree(tb->d9954[idx]);
		tb->d9954[idx] = 0;
	}
	return 0;
}

#endif
