/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame channel: buffer dequeue from the vendor module (Matt Davis'
 * recovered C, PR #15): frame_channel_vb2_dqbuf (0x116b0).
 *
 * Channel (vendor offsets): buffer type +0x2c, queued list +0x218 (prev
 * +0x21c, count +0x220), done list +0x224, done lock / count +0x22c, wait
 * queue +0x230, state +0x244 (bit 0 streaming), frame index +0x2d0, queued
 * lock +0x2d4, kernel caller +0x328 (the buffer pointer is a kernel
 * address: memcpy instead of the user copies).  Buffer: state +0x64, extra
 * flags +0x78, queued node +0x7c, done node +0x84.  ISP core (*d9950): lock
 * / state word +0x128.  v4l2_buffer (96 bytes): type +4, flags +0x34.
 *
 * Flow: copy in the v4l2_buffer, check the type, wait (interruptible, the
 * vendor's check_state condition) until the done list has a buffer while
 * streaming, unlink it, fill the v4l2_buffer (__fill_tisp_buffer), mark it
 * dequeued; in the frame-skip modes d9014 1..4 the buffers of the listed
 * frame indices (mode 1: 0; 2: 0, 3; 3: 0, 3, 6; 4: 0, 3, 6, 9) go straight
 * back to the queued list (state 3, extra flags moved into the v4l2 flags
 * when the core word is 1); then copy out.
 *
 * Vendor quirks kept: the done-list lock word +0x22c is also the counter
 * that is decremented; the core lock word +0x128 is read as a state; a
 * wait that returns 0 or -512 waits again.  Vendor UB named, not fixed: a
 * positive wait result fills from a NULL buffer (kernel waits never return
 * one, the oracle does not either).
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_fchdq_oracle).
 */
#ifndef TX_ISP_T32_FCH_DQ_H
#define TX_ISP_T32_FCH_DQ_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_DQ_TYPE		0x2cU
#define T32_DQ_QLIST		0x218U
#define T32_DQ_QCOUNT		0x220U
#define T32_DQ_DONE		0x224U
#define T32_DQ_DLOCK		0x22cU
#define T32_DQ_WAITQ		0x230U
#define T32_DQ_STATE		0x244U
#define T32_DQ_FRAME		0x2d0U
#define T32_DQ_QLOCK		0x2d4U
#define T32_DQ_KERNEL		0x328U
#define T32_DQ_VB_STATE		0x64U
#define T32_DQ_VB_XFLAGS	0x78U
#define T32_DQ_VB_QNODE		0x7cU
#define T32_DQ_VB_DNODE		0x84U
#define T32_DQ_CORE_LOCK	0x128U
#define T32_DQ_BUF_TYPE		4U
#define T32_DQ_BUF_FLAGS	0x34U
#define T32_DQ_BUF_BYTES	96U
#define T32_DQ_STATE_QUEUED	3U

struct t32_fchdq_os {
	u32 (*copy_from_user)(u8 *dst, u32 user, u32 n);	/* bytes left */
	u32 (*copy_to_user)(u32 user, const u8 *src, u32 n);
	int (*wait)(u8 *wq, u8 *ch_type);		/* wait_event_interruptible(check_state) */
	void (*lock)(u8 *l, u32 *flags);		/* __private_spin_lock_irqsave */
	void (*unlock)(u8 *l, u32 flags);		/* private_spin_unlock_irqrestore */
	void (*fill)(u8 *vb, u8 *buf);			/* __fill_tisp_buffer */
	const u32 *d9014;				/* frame-skip mode */
	const u32 *d9950;				/* ISP core */
};

static inline int t32_dq_requeue(u32 mode, u32 frame)
{
	switch (mode) {
	case 1:
		return frame == 0;
	case 2:
		return frame == 0 || frame == 3;
	case 3:
		return frame < 7 && (73U >> frame & 1);
	case 4:
		return frame < 10 && (585U >> frame & 1);
	}
	return 0;
}

static inline int t32_frame_channel_vb2_dqbuf(const struct t32_fchdq_os *os, u8 *ch, u32 arg)
{
	u8 buf[T32_DQ_BUF_BYTES], *vb, *node, *core;
	u32 flags = 0, f2 = 0, prev;
	int ret;

	if (!ch || (uintptr_t)ch >= (uintptr_t)(u32)-4095)
		return -22;
	if (!arg || arg >= (u32)-4095) {
		isp_printf(2, "The parameter from user is invalid!\n");
		return -22;
	}
	if (t32_g_rd32(ch, T32_DQ_KERNEL)) {
		memcpy(buf, T32_PTR(arg), T32_DQ_BUF_BYTES);
	} else if (os->copy_from_user(buf, arg, T32_DQ_BUF_BYTES)) {
		isp_printf(2, "Failed to copy from user\n");
		return -12;
	}
	if (t32_g_rd32(buf, T32_DQ_BUF_TYPE) != t32_g_rd32(ch, T32_DQ_TYPE)) {
		isp_printf(2, "dqbuf: invalid buffer type\n");
		return -22;
	}
	for (;;) {
		if (!(t32_g_rd32(ch, T32_DQ_STATE) & 1)) {
			isp_printf(2, "Streaming off, will not wait for buffers\n");
			return -22;
		}
		if (t32_g_rd32(ch, T32_DQ_DONE) != T32_WORD(ch + T32_DQ_DONE))
			break;
		ret = os->wait(ch + T32_DQ_WAITQ, ch + T32_DQ_TYPE);
		if (ret == 0 || ret == -512)
			continue;
		isp_printf(2, "Sleep was interrupted\n");
		return ret;	/* vendor: a positive result goes on with a NULL buffer */
	}
	os->lock(ch + T32_DQ_DLOCK, &f2);
	node = T32_PTR(t32_g_rd32(ch, T32_DQ_DONE));
	t32_g_wr32(T32_PTR(t32_g_rd32(node, 0)), 4, t32_g_rd32(node, 4));
	t32_g_wr32(T32_PTR(t32_g_rd32(node, 4)), 0, t32_g_rd32(node, 0));
	t32_g_wr32(node, 0, 0x100);
	t32_g_wr32(node, 4, 0x200);
	t32_g_wr32(ch, T32_DQ_DLOCK, t32_g_rd32(ch, T32_DQ_DLOCK) - 1);
	os->unlock(ch + T32_DQ_DLOCK, f2);
	vb = node - T32_DQ_VB_DNODE;
	os->fill(vb, buf);
	t32_g_wr32(vb, T32_DQ_VB_STATE, 0);
	if (t32_dq_requeue(*os->d9014, t32_g_rd32(ch, T32_DQ_FRAME))) {
		core = T32_PTR(*os->d9950);
		os->lock(core + T32_DQ_CORE_LOCK, &flags);
		core = T32_PTR(*os->d9950);
		if (t32_g_rd32(core, T32_DQ_CORE_LOCK) == 1 && t32_g_rd32(vb, T32_DQ_VB_XFLAGS)) {
			t32_g_wr32(buf, T32_DQ_BUF_FLAGS, t32_g_rd32(buf, T32_DQ_BUF_FLAGS) | t32_g_rd32(vb, T32_DQ_VB_XFLAGS));
			t32_g_wr32(vb, T32_DQ_VB_XFLAGS, 0);
		}
		os->unlock(core + T32_DQ_CORE_LOCK, flags);
		t32_g_wr32(vb, T32_DQ_VB_STATE, T32_DQ_STATE_QUEUED);
		os->lock(ch + T32_DQ_QLOCK, &flags);
		prev = t32_g_rd32(ch, T32_DQ_QLIST + 4);
		t32_g_wr32(ch, T32_DQ_QLIST + 4, T32_WORD(vb + T32_DQ_VB_QNODE));
		t32_g_wr32(vb, T32_DQ_VB_QNODE, T32_WORD(ch + T32_DQ_QLIST));
		t32_g_wr32(vb, T32_DQ_VB_QNODE + 4, prev);
		t32_g_wr32(T32_PTR(prev), 0, T32_WORD(vb + T32_DQ_VB_QNODE));
		t32_g_wr32(ch, T32_DQ_QCOUNT, t32_g_rd32(ch, T32_DQ_QCOUNT) + 1);
		os->unlock(ch + T32_DQ_QLOCK, flags);
	}
	if (t32_g_rd32(ch, T32_DQ_KERNEL)) {
		memcpy(T32_PTR(arg), buf, T32_DQ_BUF_BYTES);
		return 0;
	}
	if (os->copy_to_user(arg, buf, T32_DQ_BUF_BYTES)) {
		isp_printf(2, "Failed to copy to user\n");
		return -12;
	}
	return 0;
}

#endif
