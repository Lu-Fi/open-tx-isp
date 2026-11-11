/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame channel: buffer request from the vendor module (Matt Davis'
 * recovered C, PR #15): frame_channel_reqbufs (0x10bf4).
 *
 * Channel (vendor offsets): queue +0x2c (buffer type), buffer size +0x3c,
 * memory type +0x44, format copy +0x48 (word) / +0x4c (48 bytes), buffer
 * array +0x114, buffer count +0x214, state +0x244 (bit 0 streaming),
 * current format +0x248 / +0x24c, pad +0x2cc, channel index +0x2d0, kernel
 * caller +0x328.  Request (v4l2_requestbuffers, 20 bytes): count +0,
 * memory +8.  Buffer (kmalloc'ed, zeroed): index +0, type +4, memory +0x30,
 * queue +0x60, state +0x64.
 *
 * Flow: copy in (memcpy for kernel callers); with d9008 and all three
 * module tables set for the channel index the second call, counted in
 * d9b44, returns 0 at once; streaming: -16; count 0 or existing buffers or
 * a new memory type: __vb2_queue_free first (count 0 returns 0); then the
 * memory type must match; up to 64 buffers are allocated and appended;
 * in the frame-skip modes d9014 1..4 the allocated count is noted in
 * d9d14[index / 3] for the listed channel indexes; fewer than asked: freed
 * again, -12; then the count is published, the events 0x3000009 and
 * 0x3000007 (with the count) go to the pad, the request is copied out and
 * the current format is copied to the format copy.
 *
 * Vendor quirks kept: queue_free is told the old buffer count; the new
 * buffers are numbered from the old count although the queue was freed
 * (if the free left the count), and with old count + new buffers above 64
 * the slot writes run into the count word +0x214 (vendor UB, not fixed;
 * the oracle keeps below it); -515 from either event counts as success.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_fchrq_oracle).
 */
#ifndef TX_ISP_T32_FCH_REQ_H
#define TX_ISP_T32_FCH_REQ_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_RQ_QUEUE		0x2cU
#define T32_RQ_BUFSIZE		0x3cU
#define T32_RQ_MEMORY		0x44U
#define T32_RQ_FMT_COPY		0x48U
#define T32_RQ_VBUFS		0x114U
#define T32_RQ_NBUFS		0x214U
#define T32_RQ_STATE		0x244U
#define T32_RQ_FMT		0x248U
#define T32_RQ_FMT_BYTES	48U
#define T32_RQ_PAD		0x2ccU
#define T32_RQ_INDEX		0x2d0U
#define T32_RQ_KERNEL		0x328U
#define T32_RQ_BYTES		20U
#define T32_RQ_MAX		64U
#define T32_RQ_GFP		0x24000c0U
#define T32_RQ_VB_TYPE		4U
#define T32_RQ_VB_MEMORY	0x30U
#define T32_RQ_VB_QUEUE		0x60U
#define T32_RQ_VB_STATE		0x64U
#define T32_RQ_EV_SRC		0x3000009U
#define T32_RQ_EV_BUFS		0x3000007U

struct t32_fchrq_os {
	u32 (*copy_from_user)(u8 *dst, u32 user, u32 n);
	u32 (*copy_to_user)(u32 user, const u8 *src, u32 n);
	void (*queue_free)(u8 *q, u32 n);		/* __vb2_queue_free */
	u32 (*kmalloc)(u32 size, u32 flags);		/* private_kmalloc */
	int (*send_event)(u32 pad, u32 cmd, u8 *data);	/* tx_isp_send_event_to_remote */
	const u32 *d9008, *d9014;
	const u32 *f37d4, *f37a4, *f3774;		/* module tables per channel index */
	u32 *d9b44;					/* reqbufs call counter */
	u32 *d9d14;					/* allocated count per frame-skip slot */
};

static inline int t32_rq_valid(uintptr_t p)
{
	return p && p < (uintptr_t)(u32)-4095;
}

/* the channel indexes whose count the frame-skip mode notes */
static inline int t32_rq_noted(u32 mode, u32 index)
{
	switch (mode) {
	case 1:
		return index == 0;
	case 2:
		return index == 0 || index == 3;
	case 3:
		return index < 7 && (73U >> index & 1);
	case 4:
		return index < 10 && (585U >> index & 1);
	}
	return 0;
}

static inline int t32_rq_event(const struct t32_fchrq_os *os, u8 *ch, u32 cmd, u8 *data, const char *err)
{
	int ret = os->send_event(t32_g_rd32(ch, T32_RQ_PAD), cmd, data);

	if (ret == 0 || ret == -515)
		return 0;
	isp_printf(2, err);
	return ret;
}

static inline int t32_frame_channel_reqbufs(const struct t32_fchrq_os *os, u8 *ch, u32 arg)
{
	u8 req[T32_RQ_BYTES], cnt[4];
	u32 n, got, base, idx;
	int ret;

	if (!t32_rq_valid((uintptr_t)ch))
		return -22;
	if (!t32_rq_valid(arg)) {
		isp_printf(2, "The parameter from user is invalid!\n");
		return -22;
	}
	idx = t32_g_rd32(ch, T32_RQ_INDEX);
	if (*os->d9008 && os->f37d4[idx] && os->f37a4[idx] && os->f3774[idx] && ++os->d9b44[idx] == 2)
		return 0;
	if (t32_g_rd32(ch, T32_RQ_KERNEL)) {
		memcpy(req, T32_PTR(arg), T32_RQ_BYTES);
	} else if (os->copy_from_user(req, arg, T32_RQ_BYTES)) {
		isp_printf(2, "Failed to copy from user\n");
		return -12;
	}
	if (t32_g_rd32(ch, T32_RQ_STATE) & 1) {
		isp_printf(2, "reqbufs: streaming active\n");
		return -16;
	}
	if (!t32_g_rd32(req, 0) || t32_g_rd32(ch, T32_RQ_NBUFS) ||
	    t32_g_rd32(ch, T32_RQ_MEMORY) != t32_g_rd32(req, 8)) {
		os->queue_free(ch + T32_RQ_QUEUE, t32_g_rd32(ch, T32_RQ_NBUFS));
		if (!t32_g_rd32(req, 0))
			return 0;
	}
	if (t32_g_rd32(ch, T32_RQ_MEMORY) != t32_g_rd32(req, 8)) {
		isp_printf(2, "reqbufs: the memory type isn't userptr!\n");
		return -22;
	}
	n = t32_g_rd32(req, 0) < T32_RQ_MAX + 1 ? t32_g_rd32(req, 0) : T32_RQ_MAX;
	for (got = 0; got < n; got++) {
		u8 *vb = T32_PTR(os->kmalloc(t32_g_rd32(ch, T32_RQ_BUFSIZE), T32_RQ_GFP));

		if (!vb) {
			isp_printf(2, "Memory alloc for buffer struct failed\n");
			break;
		}
		memset(vb, 0, t32_g_rd32(ch, T32_RQ_BUFSIZE));
		t32_g_wr32(vb, T32_RQ_VB_STATE, 0);
		t32_g_wr32(vb, T32_RQ_VB_QUEUE, T32_WORD(ch + T32_RQ_QUEUE));
		base = t32_g_rd32(ch, T32_RQ_NBUFS);
		t32_g_wr32(vb, 0, base + got);
		t32_g_wr32(vb, T32_RQ_VB_TYPE, t32_g_rd32(ch, T32_RQ_QUEUE));
		t32_g_wr32(vb, T32_RQ_VB_MEMORY, t32_g_rd32(ch, T32_RQ_MEMORY));
		t32_g_wr32(ch, T32_RQ_VBUFS + (got + t32_g_rd32(ch, T32_RQ_NBUFS)) * 4, T32_WORD(vb));
	}
	isp_printf(1, "Allocated %d buffers!\n", got);
	if (!got) {
		isp_printf(2, "Memory allocation failed\n");
		return -12;
	}
	t32_g_wr32(cnt, 0, got);
	if (t32_rq_noted(*os->d9014, idx = t32_g_rd32(ch, T32_RQ_INDEX)))
		os->d9d14[(int)idx / 3] = got;
	if (got < n) {
		os->queue_free(ch + T32_RQ_QUEUE, got);
		isp_printf(2, "Can't allocation %d buffers!\n", n);
		return -12;
	}
	t32_g_wr32(ch, T32_RQ_NBUFS, got);
	t32_g_wr32(req, 0, got);
	ret = t32_rq_event(os, ch, T32_RQ_EV_SRC, cnt, "streamon: driver refused to start streaming\n");
	if (ret)
		return ret;
	ret = t32_rq_event(os, ch, T32_RQ_EV_BUFS, cnt, "requested buffer Failed\n");
	if (ret)
		return ret;
	if (t32_g_rd32(ch, T32_RQ_KERNEL)) {
		memcpy(T32_PTR(arg), req, T32_RQ_BYTES);
	} else if (os->copy_to_user(arg, req, T32_RQ_BYTES)) {
		isp_printf(2, "Failed to copy to user\n");
		return -12;
	}
	if (t32_rq_valid((uintptr_t)(ch + T32_RQ_FMT)) && t32_rq_valid((uintptr_t)(ch + T32_RQ_FMT_COPY))) {
		t32_g_wr32(ch, T32_RQ_FMT_COPY, t32_g_rd32(ch, T32_RQ_FMT));
		memcpy(ch + T32_RQ_FMT_COPY + 4, ch + T32_RQ_FMT + 4, T32_RQ_FMT_BYTES);
	}
	return 0;
}

#endif
