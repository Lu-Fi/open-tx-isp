/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame-channel buffer queue helpers of the vendor module (its own
 * vb2-like queue) and the media pad lookup: check_state, find_new_buffer,
 * __enqueue_in_driver, __fill_tisp_buffer, __vb2_queue_free,
 * __vb2_queue_cancel, __frame_channel_vb2_streamoff, find_subdev_link_pad.
 *
 * The vendor objects are 32-bit kernel structs; they are handled here as
 * byte images with the vendor offsets (pointers are 32-bit words), so that
 * the same code runs in the kernel and in the host oracle.
 *   queue (frame channel + 44):  +232 buffer pointers, +488 buffer count,
 *     +492 queued list, +500 / +512 words, +504 done list, +512 spinlock,
 *     +516 wait queue, +536 bit 0 streaming, +672 / +676 channel words
 *   buffer:  +0..+95 user buffer (v4l2_buffer layout: +12 flags, +88 user
 *     pointer), +92 memory type, +96 channel, +100 state, +104 state byte,
 *     +116 word for the user, +140 event payload
 * Kernel calls go through struct t32_vbuf_ops.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK):
 * __vb2_queue_cancel zeroes the word at +512 (the spinlock) while it holds
 * that lock, before the unlock.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_vbuf_oracle).
 */
#ifndef TX_ISP_T32_VBUF_H
#define TX_ISP_T32_VBUF_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_EVENT_QBUF		0x3000005
#define T32_EVENT_STREAMOFF	0x3000004
#define T32_EVENT_FREE		0x3000008
#define T32_ENOIOCTLCMD		515
#define T32_EINVAL		22
#define T32_VB_STATE_ACTIVE	3

struct t32_vbuf_ops {
	int (*send_event)(u32 sd, u32 event, void *arg);	/* tx_isp_send_event_to_remote */
	int (*isp_printf)(int level, const char *fmt, ...);
	void (*kfree)(void *p);
	void (*lock)(void *lock, u32 *flags);		/* __private_spin_lock_irqsave */
	void (*unlock)(void *lock, u32 flags);		/* private_spin_unlock_irqrestore */
	void (*wake_up_all)(void *wq);
	unsigned long (*copy_to_user)(void *to, const void *from, unsigned long n);
	void (*queue_cancel)(u8 *q);			/* __vb2_queue_cancel, for streamoff */
};

static inline u32 *t32_vb_w(u8 *p, u32 off)
{
	return (u32 *)(p + off);
}

static inline u32 t32_vb_addr(const void *p)
{
	return (u32)(uintptr_t)p;
}

static inline void *t32_vb_ptr(u32 v)
{
	return (void *)(uintptr_t)v;
}

/* INIT_LIST_HEAD on a 32-bit list_head */
static inline void t32_vb_list_init(u8 *q, u32 off)
{
	*t32_vb_w(q, off) = t32_vb_addr(q + off);
	*t32_vb_w(q, off + 4) = t32_vb_addr(q + off);
}

/* check_state: 1 = buffers done (list +504 not empty) or not streaming */
static inline int t32_check_state(u8 *q)
{
	if (!q)
		return 0;
	if (*t32_vb_w(q, 504) != t32_vb_addr(q + 504))
		return 1;
	return (*t32_vb_w(q, 536) & 1) ^ 1;
}

/* find_new_buffer: the first free one of 20 slots of 20 bytes (byte 1 = in use), zeroed and taken */
static inline u8 *t32_find_new_buffer(u8 *slots)
{
	int i;

	for (i = 0; i < 20; i++) {
		u8 *s = slots + 20 * i;

		if (!s[1]) {
			memset(s, 0, 20);
			s[1] = 1;
			return s;
		}
	}
	return NULL;
}

/* __enqueue_in_driver */
static inline int t32_enqueue_in_driver(const struct t32_vbuf_ops *k, u8 *vb)
{
	u8 *ch = t32_vb_ptr(*t32_vb_w(vb, 96));
	int ret;

	*t32_vb_w(vb, 100) = T32_VB_STATE_ACTIVE;
	vb[104] = T32_VB_STATE_ACTIVE;
	ret = k->send_event(*t32_vb_w(ch, 672), T32_EVENT_QBUF, vb + 140);
	if (ret && ret != -T32_ENOIOCTLCMD)
		k->isp_printf(2, "Failed to qbuf to driver; chan%d!\n", *t32_vb_w(ch, 676));
	return ret;
}

/* __fill_tisp_buffer: the user's copy of buffer vb */
static inline void t32_fill_tisp_buffer(const struct t32_vbuf_ops *k, u8 *vb, u8 *out)
{
	u8 *ch = t32_vb_ptr(*t32_vb_w(vb, 96));
	u32 *flags = t32_vb_w(out, 12);

	memcpy(out, vb, 96);
	if (*t32_vb_w(vb, 92) == 4 && k->copy_to_user(t32_vb_ptr(*t32_vb_w(out, 88)), vb + 116, 4))
		k->isp_printf(2, "[ %s:%d ] copy error!!!\n", "__fill_tisp_buffer", 377);
	*flags &= 0xffff1bb8;
	*flags |= *t32_vb_w(ch, 20);
	switch (*t32_vb_w(vb, 100)) {
	case 1:
	case 3:
		*flags |= 0x2;
		break;
	case 4:
		*flags |= 0x4;
		break;
	case 5:
		*flags |= 0x40;
		*flags |= 0x4;
		break;
	default:
		break;
	}
}

/* __vb2_queue_free: free the last n buffers */
static inline int t32_vb2_queue_free(const struct t32_vbuf_ops *k, u8 *q, u32 n)
{
	u32 i, flags = 0;

	for (i = *t32_vb_w(q, 488) - n; i < *t32_vb_w(q, 488); i++) {
		k->kfree(t32_vb_ptr(*t32_vb_w(q, 232 + 4 * i)));
		*t32_vb_w(q, 232 + 4 * i) = 0;
	}
	*t32_vb_w(q, 488) -= n;
	t32_vb_list_init(q, 492);
	k->lock(q + 512, &flags);
	t32_vb_list_init(q, 504);
	k->unlock(q + 512, flags);
	return k->send_event(*t32_vb_w(q, 672), T32_EVENT_FREE, NULL);
}

/* __vb2_queue_cancel */
static inline void t32_vb2_queue_cancel(const struct t32_vbuf_ops *k, u8 *q)
{
	u32 i, flags = 0;

	if (!q || t32_vb_addr(q) >= (u32)-4095)
		return;
	if (*t32_vb_w(q, 536) & 1)
		k->send_event(*t32_vb_w(q, 672), T32_EVENT_STREAMOFF, NULL);
	q[536] &= ~1;
	t32_vb_list_init(q, 492);
	k->lock(q + 512, &flags);
	t32_vb_list_init(q, 504);
	*t32_vb_w(q, 500) = 0;
	*t32_vb_w(q, 512) = 0;
	k->unlock(q + 512, flags);
	k->send_event(*t32_vb_w(q, 672), T32_EVENT_FREE, NULL);
	k->wake_up_all(q + 516);
	for (i = 0; i < *t32_vb_w(q, 488); i++)
		*t32_vb_w(t32_vb_ptr(*t32_vb_w(q, 232 + 4 * i)), 100) = 0;
}

/*
 * __frame_channel_vb2_streamoff (ch: the frame channel, queue at +44):
 * clears the channel's slot word slots[idx / 3] when its index +720 is a
 * first-of-three index valid for the direct mode (1: 0; 2: 0, 3; 3: 0, 3, 6;
 * 4: 0, 3, 6, 9), cancels the queue, state +736 = 3.
 */
static inline int t32_frame_channel_streamoff(const struct t32_vbuf_ops *k, u8 *ch, u32 type, u32 direct_mode,
					      u32 *slots)
{
	u32 idx = *t32_vb_w(ch, 720);
	static const u16 valid[5] = { 0, 0x1, 0x9, 0x49, 0x249 };	/* bit idx */

	if (*t32_vb_w(ch, 44) != type) {
		k->isp_printf(2, "streamon: invalid stream type\n");
		return -T32_EINVAL;
	}
	if (!(*t32_vb_w(ch, 580) & 1)) {
		k->isp_printf(1, "streamoff: not streaming\n");
		return -T32_EINVAL;
	}
	if (direct_mode >= 1 && direct_mode <= 4 && idx < 10 && (valid[direct_mode] >> idx & 1))
		slots[idx / 3] = 0;
	k->queue_cancel(ch + 44);
	*t32_vb_w(ch, 736) = 3;
	return 0;
}

/*
 * find_subdev_link_pad (dev +84: 16 subdev pointers; subdev +8 name, +268 /
 * +270 output / input pad counts, +272 / +276 pad arrays of 36 bytes;
 * desc: +0 name pointer, +4 type 1 input / 2 output, +5 index)
 */
static inline u8 *t32_find_subdev_link_pad(const struct t32_vbuf_ops *k, u8 *dev, u8 *desc)
{
	u8 *sd = NULL;
	u32 idx = desc[5];
	int i;

	for (i = 0; i < 16; i++) {
		u8 *s = t32_vb_ptr(*t32_vb_w(dev, 84 + 4 * i));

		if (s && !strcmp(t32_vb_ptr(*t32_vb_w(s, 8)), t32_vb_ptr(*t32_vb_w(desc, 0)))) {
			sd = s;
			break;
		}
	}
	if (!sd)
		return NULL;
	if (desc[4] == 1 && idx < *(u16 *)(sd + 270))
		return (u8 *)t32_vb_ptr(*t32_vb_w(sd, 276)) + 36 * idx;
	if (desc[4] == 2 && idx < *(u16 *)(sd + 268))
		return (u8 *)t32_vb_ptr(*t32_vb_w(sd, 272)) + 36 * idx;
	k->isp_printf(2, "[ %s:%d ] Can't find the matched pad!\n", "find_subdev_link_pad", 463);
	return NULL;
}

#endif /* TX_ISP_T32_VBUF_H */
