/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame channel: buffer queue from the vendor module (Matt Davis'
 * recovered C, PR #15): frame_channel_vb2_qbuf (0x1112c).
 *
 * Channel (vendor offsets): queue +0x2c (buffer type), mutex +0x30, memory
 * type +0x44, image size +0x60, buffer array +0x114, buffer count +0x214,
 * queued list +0x218 (prev +0x21c, count +0x220), state +0x244 (bit 0
 * streaming), channel index +0x2d0, queued lock +0x2d4, kernel caller
 * +0x328.  v4l2_buffer (96 bytes): index +0, type +4, flags +12, timestamp
 * +16..+24, memory +48, userptr +52, length +56, the vendor's plane block
 * +64..+92 (word +72 must be 1: "uv_addr" version).  Buffer: flags +12,
 * timestamp +16, userptr +52, length +56, plane block +64..+78, extra
 * +80..+92, state +100, byte +104, queued node +124, own list +140,
 * plane addresses / sizes +148 / +152 / +180 / +184.
 *
 * Flow: with d9008 a user caller whose channel index has all three module
 * tables set skips (returns 0) while its counter d9ba4 is below f3774; in
 * the frame-skip modes d9014 1..4 the listed channel indexes count d9d04
 * per slot and skip while the count exceeds d9d14 (the reqbufs count);
 * then copy in, checks (version, type, index, buffer, memory, length,
 * state), copy of the user fields, cache write-back of both planes
 * through the DMA ops +36 (for a negative userptr only in mode 0), queued
 * list append, __enqueue_in_driver while streaming, fill and copy out.
 *
 * Vendor quirks kept: the "already in use" message prints the channel
 * index and the buffer's old userptr; the byte +104 is 1 / 2 / 30 along
 * the way; the second plane's write-back length is half the first's.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_fchqb_oracle).
 */
#ifndef TX_ISP_T32_FCH_QBUF_H
#define TX_ISP_T32_FCH_QBUF_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_QB_QUEUE		0x2cU
#define T32_QB_MUTEX		0x30U
#define T32_QB_MEMORY		0x44U
#define T32_QB_SIZEIMAGE	0x60U
#define T32_QB_VBUFS		0x114U
#define T32_QB_NBUFS		0x214U
#define T32_QB_QLIST		0x218U
#define T32_QB_QCOUNT		0x220U
#define T32_QB_STATE		0x244U
#define T32_QB_INDEX		0x2d0U
#define T32_QB_QLOCK		0x2d4U
#define T32_QB_KERNEL		0x328U
#define T32_QB_BUF_BYTES	96U
#define T32_QB_BUF_VERSION	72U
#define T32_QB_FLAGS_MASK	0xffff1bb8U
#define T32_QB_DMA_SYNC		36U	/* sync_single_for_device in the DMA ops */
#define T32_QB_DMA_TO_DEVICE	2U
#define T32_QB_VB_STATE		100U
#define T32_QB_VB_MARK		104U
#define T32_QB_VB_QNODE		124U
#define T32_QB_VB_LIST		140U

typedef void (*t32_qb_dma_sync)(u32 dev, u32 addr, u32 size, u32 dir);

struct t32_fchqb_os {
	u32 (*copy_from_user)(u8 *dst, u32 user, u32 n);
	u32 (*copy_to_user)(u32 user, const u8 *src, u32 n);
	void (*mutex_lock)(u8 *m);
	void (*mutex_unlock)(u8 *m);
	void (*lock)(u8 *l, u32 *flags);		/* __private_spin_lock_irqsave */
	void (*unlock)(u8 *l, u32 flags);		/* private_spin_unlock_irqrestore */
	void (*enqueue)(u8 *vb);			/* __enqueue_in_driver */
	void (*fill)(u8 *vb, u8 *buf);			/* __fill_tisp_buffer */
	const u32 *dma_ops;				/* mips_dma_map_ops */
	const u32 *d9008, *d9014;
	const u32 *f37d4, *f37a4, *f3774;		/* module tables per channel index */
	u32 *d9ba4;					/* qbuf skip counter */
	u32 *d9d04;					/* qbuf count per frame-skip slot */
	const u32 *d9d14;				/* reqbufs count per frame-skip slot */
};

static inline int t32_qb_valid(uintptr_t p)
{
	return p && p < (uintptr_t)(u32)-4095;
}

static inline int t32_qb_noted(u32 mode, u32 index)
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

static inline void t32_qb_sync(const struct t32_fchqb_os *os, u32 addr, u32 size)
{
	u32 fn = t32_g_rd32(T32_PTR(*os->dma_ops), T32_QB_DMA_SYNC);

	if (fn)
		((t32_qb_dma_sync)T32_PTR(fn))(0, addr, size, T32_QB_DMA_TO_DEVICE);
}

static inline int t32_qb_check(u8 *ch, const u8 *buf, u8 **vbp)
{
	static const char fn[] = "frame_channel_vb2_qbuf";
	u32 index = t32_g_rd32(buf, 0);
	u8 *vb;

	if (t32_g_rd32(buf, T32_QB_BUF_VERSION) != 1) {
		isp_printf(2, "[%s,%d] failed: Please update the version to pass the uv_addr.\n", fn, 799);
		return -22;
	}
	if (t32_g_rd32(buf, 4) != t32_g_rd32(ch, T32_QB_QUEUE)) {
		isp_printf(2, "qbuf: invalid buffer type\n");
		return -22;
	}
	if (index >= t32_g_rd32(ch, T32_QB_NBUFS)) {
		isp_printf(2, "qbuf: buffer index(%d) out of range(%d) \n", index, t32_g_rd32(ch, T32_QB_NBUFS));
		return -22;
	}
	vb = T32_PTR(t32_g_rd32(ch, T32_QB_VBUFS + index * 4));
	if (!vb) {
		isp_printf(2, "qbuf: buffer is NULL\n");
		return -22;
	}
	vb[T32_QB_VB_MARK] = 1;
	if (t32_g_rd32(buf, 48) != t32_g_rd32(ch, T32_QB_MEMORY)) {
		isp_printf(2, "qbuf: invalid memory type\n");
		return -22;
	}
	if (t32_g_rd32(buf, 56) != t32_g_rd32(ch, T32_QB_SIZEIMAGE)) {
		isp_printf(2, "qbuf: invalid memory size, length = %d sizeimage = %d\n",
			   t32_g_rd32(buf, 56), t32_g_rd32(ch, T32_QB_SIZEIMAGE));
		return -22;
	}
	if (t32_g_rd32(vb, T32_QB_VB_STATE)) {
		isp_printf(2, "qbuf: buffer already in use [chan index %d] [0x%lx]\n",
			   t32_g_rd32(ch, T32_QB_INDEX), t32_g_rd32(vb, 52));
		return -22;
	}
	*vbp = vb;
	return 0;
}

static inline int t32_frame_channel_vb2_qbuf(const struct t32_fchqb_os *os, u8 *ch, u32 arg)
{
	u8 buf[T32_QB_BUF_BYTES], *vb = 0;
	u32 flags = 0, idx, k, prev, uptr, plane;
	int ret;

	if (!t32_qb_valid((uintptr_t)ch))
		return -22;
	if (!t32_qb_valid(arg)) {
		isp_printf(2, "The parameter from user is invalid!\n");
		return -22;
	}
	idx = t32_g_rd32(ch, T32_QB_INDEX);
	if (*os->d9008 && !t32_g_rd32(ch, T32_QB_KERNEL) && os->f37d4[idx] && os->f37a4[idx] && os->f3774[idx] &&
	    (int)os->d9ba4[idx]++ < (int)os->f3774[idx])
		return 0;
	idx = t32_g_rd32(ch, T32_QB_INDEX);
	if (t32_qb_noted(*os->d9014, idx)) {
		k = (u32)((int)idx / 3);
		if ((int)os->d9d14[k] < (int)++os->d9d04[k])
			return 0;
	}
	if (t32_g_rd32(ch, T32_QB_KERNEL)) {
		memcpy(buf, T32_PTR(arg), T32_QB_BUF_BYTES);
	} else if (os->copy_from_user(buf, arg, T32_QB_BUF_BYTES)) {
		isp_printf(2, "Failed to copy from user\n");
		return -12;
	}
	ret = t32_qb_check(ch, buf, &vb);
	if (ret)
		return ret;
	uptr = t32_g_rd32(buf, 52);
	t32_g_wr32(vb, 52, uptr);
	t32_g_wr32(vb, 56, t32_g_rd32(buf, 56));
	memcpy(vb + 16, buf + 16, 12);
	t32_g_wr32(vb, 12, t32_g_rd32(buf, 12) & T32_QB_FLAGS_MASK);
	t32_g_wr32(vb, 92, t32_g_rd32(buf, 92));
	if (t32_g_rd32(buf, 92))
		t32_g_wr32(vb, 88, t32_g_rd32(buf, 88));
	t32_g_wr32(vb, 84, t32_g_rd32(buf, 84));
	if (t32_g_rd32(buf, 84))
		t32_g_wr32(vb, 80, t32_g_rd32(buf, 80));
	memcpy(vb + 64, buf + 64, 16);	/* version word checked as 1 above */
	plane = t32_g_rd32(vb, 64);
	if ((int)uptr >= 0 || !*os->d9014) {
		t32_qb_sync(os, uptr, t32_g_rd16(vb, 78) * t32_g_rd32(vb, 68));
		t32_qb_sync(os, plane, t32_g_rd16(vb, 78) * t32_g_rd32(vb, 68) >> 1);
	}
	t32_g_wr32(vb, 148, uptr);
	t32_g_wr32(vb, 180, t32_g_rd32(vb, 68));
	t32_g_wr32(vb, 152, plane);
	t32_g_wr32(vb, 184, t32_g_rd32(vb, 72));
	t32_g_wr32(vb, T32_QB_VB_LIST, T32_WORD(vb + T32_QB_VB_LIST));
	t32_g_wr32(vb, T32_QB_VB_LIST + 4, T32_WORD(vb + T32_QB_VB_LIST));
	t32_g_wr32(vb, T32_QB_VB_STATE, 2);
	vb[T32_QB_VB_MARK] = 2;
	os->mutex_lock(ch + T32_QB_MUTEX);
	os->lock(ch + T32_QB_QLOCK, &flags);
	prev = t32_g_rd32(ch, T32_QB_QLIST + 4);
	t32_g_wr32(ch, T32_QB_QLIST + 4, T32_WORD(vb + T32_QB_VB_QNODE));
	t32_g_wr32(vb, T32_QB_VB_QNODE, T32_WORD(ch + T32_QB_QLIST));
	t32_g_wr32(vb, T32_QB_VB_QNODE + 4, prev);
	t32_g_wr32(T32_PTR(prev), 0, T32_WORD(vb + T32_QB_VB_QNODE));
	t32_g_wr32(vb, T32_QB_VB_STATE, 1);
	t32_g_wr32(ch, T32_QB_QCOUNT, t32_g_rd32(ch, T32_QB_QCOUNT) + 1);
	os->unlock(ch + T32_QB_QLOCK, flags);
	if (t32_g_rd32(ch, T32_QB_STATE) & 1)
		os->enqueue(vb);
	vb[T32_QB_VB_MARK] = 30;
	os->fill(vb, buf);
	os->mutex_unlock(ch + T32_QB_MUTEX);
	if (t32_g_rd32(ch, T32_QB_KERNEL)) {
		memcpy(T32_PTR(arg), buf, T32_QB_BUF_BYTES);
		return 0;
	}
	if (os->copy_to_user(arg, buf, T32_QB_BUF_BYTES)) {
		isp_printf(2, "Failed to copy to user\n");
		return -12;
	}
	return 0;
}

#endif
