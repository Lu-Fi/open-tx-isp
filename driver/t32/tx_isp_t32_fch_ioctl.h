/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame channel: ioctl dispatcher from the vendor module (Matt Davis'
 * recovered C, PR #15): frame_channel_unlocked_ioctl (0x13c18).
 *
 * The first argument is a kernel channel index (< 256: channel
 * index * 816 in the channel array of the ISP core *d9950 +0x120, the
 * kernel-caller flag +0x328 set) or a file (private data +0x78, flag
 * cleared).  Commands: 0xc0045483 set_fmt, 0xc0045484 get_fmt, 0xc0145485
 * reqbufs, 0xc0605486 querybuf (inline), 0xc0605487 qbuf, 0xc0605488
 * dqbuf, 0xc0045489 streamon, 0xc004548a streamoff (the handlers are
 * hooks), 0xc004548b set the frame-done source (word to event 0x3000009
 * of the channel's pad +0x2cc; with d9008 and all three module tables set
 * for the channel index +0x2d0 the second call per channel, counted in
 * d9ae4, is skipped), 0xc004548c wait for a frame (completion +0x2e4, or
 * with d9008 set and d9004 clear down_timeout(+0x2f0, 50)) and report the
 * frame count; others: -515.
 *
 * Querybuf: the v4l2_buffer is copied from user space even for kernel
 * callers; index checked against the buffer count +0x214, buffer
 * +0x114 + 4 * index filled by __fill_tisp_buffer.
 *
 * Vendor quirks kept: down_timeout reports -1 as the frame count, a failed
 * completion wait reports its error; the result of a successful wait is
 * the completion's count + 1.  Vendor UB named, not fixed: a file without
 * private data gets the flag word written at address 0x328; 0xc004548c
 * does not check the channel or the argument.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_fchio_oracle).
 */
#ifndef TX_ISP_T32_FCH_IOCTL_H
#define TX_ISP_T32_FCH_IOCTL_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_FIO_FILE_PRIV	0x78U
#define T32_FIO_CORE_CHANS	0x120U
#define T32_FIO_CH_BYTES	816U
#define T32_FIO_TYPE		0x2cU
#define T32_FIO_VBUFS		0x114U
#define T32_FIO_NBUFS		0x214U
#define T32_FIO_PAD		0x2ccU
#define T32_FIO_INDEX		0x2d0U
#define T32_FIO_DONE		0x2e4U
#define T32_FIO_SEM		0x2f0U
#define T32_FIO_KERNEL		0x328U
#define T32_FIO_BUF_BYTES	96U
#define T32_FIO_EV_SRC		0x3000009U

#define T32_FIO_S_FMT		0xc0045483U
#define T32_FIO_G_FMT		0xc0045484U
#define T32_FIO_REQBUFS		0xc0145485U
#define T32_FIO_QUERYBUF	0xc0605486U
#define T32_FIO_QBUF		0xc0605487U
#define T32_FIO_DQBUF		0xc0605488U
#define T32_FIO_STREAMON	0xc0045489U
#define T32_FIO_STREAMOFF	0xc004548aU
#define T32_FIO_SET_SRC		0xc004548bU
#define T32_FIO_WAIT		0xc004548cU

typedef int (*t32_fio_handler)(u8 *ch, u32 arg);

struct t32_fio_os {
	t32_fio_handler set_fmt, get_fmt, reqbufs, qbuf, dqbuf, streamon, streamoff;
	u32 (*copy_from_user)(u8 *dst, u32 user, u32 n);
	u32 (*copy_to_user)(u32 user, const u8 *src, u32 n);
	void (*fill)(u32 vb, u8 *buf);			/* __fill_tisp_buffer */
	int (*send_event)(u32 pad, u32 cmd, u8 *data);	/* tx_isp_send_event_to_remote */
	int (*down_timeout)(u8 *sem, u32 jiffies);
	int (*wait_completion)(u8 *c);			/* private_wait_for_completion_interruptible */
	const u32 *d9008, *d9004, *d9950;
	const u32 *f37d4, *f37a4, *f3774;		/* module tables per channel index */
	u32 *d9ae4;					/* set-source call counter */
};

static inline int t32_fio_valid(const u8 *p)
{
	return p && (uintptr_t)p < (uintptr_t)(u32)-4095;
}

static inline int t32_fio_copy_out(const struct t32_fio_os *os, u8 *ch, u32 arg, const u8 *src, u32 n, int ret)
{
	if (t32_g_rd32(ch, T32_FIO_KERNEL)) {
		memcpy(T32_PTR(arg), src, n);
		return ret;
	}
	if (os->copy_to_user(arg, src, n)) {
		isp_printf(2, "Failed to copy to user\n");
		return -12;
	}
	return ret;
}

static inline int t32_fio_querybuf(const struct t32_fio_os *os, u8 *ch, u32 arg)
{
	u8 buf[T32_FIO_BUF_BYTES];
	u32 index;

	if (!t32_fio_valid(ch))
		return -22;
	if (!t32_fio_valid(T32_PTR(arg))) {
		isp_printf(2, "The parameter from user is invalid!\n");
		return -22;
	}
	if (os->copy_from_user(buf, arg, T32_FIO_BUF_BYTES)) {
		isp_printf(2, "Failed to copy from user\n");
		return -12;
	}
	if (t32_g_rd32(buf, 4) != t32_g_rd32(ch, T32_FIO_TYPE)) {
		isp_printf(2, "dqbuf: invalid buffer type\n");
		return -22;
	}
	index = t32_g_rd32(buf, 0);
	if (index >= t32_g_rd32(ch, T32_FIO_NBUFS)) {
		isp_printf(2, "querybuf: buffer index out of range\n");
		return -22;
	}
	os->fill(t32_g_rd32(ch, T32_FIO_VBUFS + index * 4), buf);
	if (os->copy_to_user(arg, buf, T32_FIO_BUF_BYTES)) {
		isp_printf(2, "Failed to copy to user\n");
		return -12;
	}
	return 0;
}

static inline int t32_fio_set_src(const struct t32_fio_os *os, u8 *ch, u32 arg)
{
	u8 v[4] = { 0 };
	u32 i;
	int ret;

	if (!t32_fio_valid(ch))
		return -22;
	if (!t32_fio_valid(T32_PTR(arg))) {
		isp_printf(2, "The parameter from user is invalid!\n");
		return -22;
	}
	i = t32_g_rd32(ch, T32_FIO_INDEX);
	if (*os->d9008 && os->f37d4[i] && os->f37a4[i] && os->f3774[i] && ++os->d9ae4[i] == 2)
		return 0;
	if (t32_g_rd32(ch, T32_FIO_KERNEL)) {
		memcpy(v, T32_PTR(arg), 4);
	} else if (os->copy_from_user(v, arg, 4)) {
		isp_printf(2, "Failed to copy from user\n");
		return -12;
	}
	ret = os->send_event(t32_g_rd32(ch, T32_FIO_PAD), T32_FIO_EV_SRC, v);
	if (ret == 0 || ret == -515)
		return 0;
	isp_printf(2, "streamon: driver refused to start streaming\n");
	return ret;
}

static inline int t32_fio_wait(const struct t32_fio_os *os, u8 *ch, u32 arg)
{
	u8 v[4];
	int ret;

	if (*os->d9008 && !*os->d9004) {
		ret = os->down_timeout(ch + T32_FIO_SEM, 50);
		t32_g_wr32(v, 0, (u32)-1);
	} else {
		ret = os->wait_completion(ch + T32_FIO_DONE);
		t32_g_wr32(v, 0, ret < 0 ? (u32)ret : t32_g_rd32(ch, T32_FIO_DONE) + 1);
	}
	return t32_fio_copy_out(os, ch, arg, v, 4, ret);
}

static inline int t32_frame_channel_unlocked_ioctl(const struct t32_fio_os *os, u32 file, u32 cmd, u32 arg)
{
	static const char fn[] = "frame_channel_unlocked_ioctl";
	u8 *ch;

	if (file < 256) {
		ch = T32_PTR(file * T32_FIO_CH_BYTES + t32_g_rd32(T32_PTR(*os->d9950), T32_FIO_CORE_CHANS));
		t32_g_wr32(ch, T32_FIO_KERNEL, 1);
	} else {
		ch = T32_PTR(t32_g_rd32(T32_PTR(file), T32_FIO_FILE_PRIV));
		if (!t32_fio_valid(ch))
			ch = 0;
		t32_g_wr32(ch, T32_FIO_KERNEL, 0);	/* vendor: also for ch == NULL */
	}
	switch (cmd) {
	case T32_FIO_S_FMT:
		return os->set_fmt(ch, arg);
	case T32_FIO_G_FMT:
		return os->get_fmt(ch, arg);
	case T32_FIO_REQBUFS:
		return os->reqbufs(ch, arg);
	case T32_FIO_QUERYBUF:
		return t32_fio_querybuf(os, ch, arg);
	case T32_FIO_QBUF:
		return os->qbuf(ch, arg);
	case T32_FIO_DQBUF:
		return os->dqbuf(ch, arg);
	case T32_FIO_STREAMON:
		return os->streamon(ch, arg);
	case T32_FIO_STREAMOFF:
		return os->streamoff(ch, arg);
	case T32_FIO_SET_SRC:
		return t32_fio_set_src(os, ch, arg);
	case T32_FIO_WAIT:
		return t32_fio_wait(os, ch, arg);
	}
	isp_printf(2, "[ %s:%d ] the frame ioctl cmd(%d) couldn't be handled!\n", fn, 1523, cmd);
	return -515;
}

#endif
