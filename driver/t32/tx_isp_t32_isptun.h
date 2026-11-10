/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 2: the tuning device (/dev/isp-m0 style misc device of the core).
 * Readable C of the vendor functions
 *   isp_core_tuning_init (0xc704), isp_core_tuning_event (0xc414),
 *   isp_core_tunning_open (0x89c8), isp_core_tunning_release (0x8950),
 *   isp_core_tunning_default_ioctl (0xc4ec), isp_core_tunning_unlocked_ioctl (0xc5ac),
 *   isp_core_tunning_unlocked_ioctl_fast (0xc6b8),
 *   isp_tunning_poll (0x1e5d0), isp_tunning_read (0x1e72c), isp_tunning_write (0x1e850),
 *   isp_frame_done_wakeup (0xc364), isp_frame_done_wait (0xb1dc).
 *
 * Tuning block (168 B, kmalloc'ed by isp_core_tuning_init): +0 owner, +16 three 32 B channel
 * entries (bytes 0..3 = 0x80, +8 = 0x190001, +12 = 0 on open, +16 day/night mode), +132 lock,
 * +144 state (1 = created / closed by event, 2 = released, 3 = open), +148 file ops, +152
 * completion, +164 event callback.  The device reaches it through file->private_data (+120)
 * -> +268 -> +1704, the fast path through the core subdev's private block (+280) -> +1704.
 * Frame-done: per channel a 64-bit counter (t32_data_d9698, 3 channels), a flag and a wait queue
 * (channel 0: d9694 / cf134, others: d9690 / cf12c).  Read side: a flag byte (dad90) and a
 * wait queue (dad94).  All kernel calls are hooks (struct t32_tun_ops).
 *
 * Vendor behaviour kept: cmd 0xc0085465 of the unlocked ioctl returns the caller's s0
 * (uninitialised in the vendor; 0 here, as in the oracle's reference run); tuning_event 3
 * indexes the entries with the unchecked channel word; isp_tunning_read copies len bytes from
 * the one-byte flag; the wakeup indexes the counters with the unchecked channel word;
 * frame_done_wait with timeout 0 and a set flag reads counter 0 for channel 0 (it never sees a
 * set flag: it clears it first).
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_isptun_oracle).
 */
#ifndef TX_ISP_T32_ISPTUN_H
#define TX_ISP_T32_ISPTUN_H

#include "tx_isp_t32_ispcore.h"

#define T32_TUN_SIZE		168
#define T32_TUN_GFP		0x024000c0U
#define T32_TUN_OWNER		0
#define T32_TUN_ENT		16	/* 3 entries of 32 B */
#define T32_TUN_ENT_DN		16	/* entry field: day/night mode */
#define T32_TUN_LOCK		132
#define T32_TUN_STATE		144
#define T32_TUN_FOPS		148
#define T32_TUN_DONE		152
#define T32_TUN_EVENT_CB	164
#define T32_TUN_ST_IDLE		1
#define T32_TUN_ST_CLOSED	2
#define T32_TUN_ST_OPEN		3
#define T32_FILE_PRIV		120
#define T32_FILE_FLAGS		28
#define T32_O_NONBLOCK		0x80
#define T32_MISC_DEV		268
#define T32_DEV_TUNING		1704
#define T32_EV_TUN_OPEN		0x04000000U
#define T32_EV_TUN_IDLE		0x04000001U
#define T32_EV_FRAME_DONE	0x04000002U
#define T32_EV_DAY_NIGHT	0x04000003U
#define T32_TUN_CMD_GET		0xc0085465U
#define T32_TUN_CMD_PUT		0xc0085466U
#define T32_TUN_CMD_CTRL	0xc0105467U
#define T32_EFAULT		(-14)
#define T32_EAGAIN		(-11)
#define T32_ERESTARTSYS		(-512)
#define T32_ETIMEDOUT		(-145)
#define T32_TASK_INTERRUPTIBLE	1
#define T32_TASK_NORMAL		3

struct t32_tun_ops {
	int (*call)(u32 fn, u32 a0, u32 a1, u32 a2);		/* poll_table qproc */
	u32 (*copy_from_user)(void *dst, u32 src, u32 n);	/* returns the bytes not copied */
	u32 (*copy_to_user)(u32 dst, const void *src, u32 n);
	u32 (*copy_user)(u32 dst, u32 src, u32 n);		/* __copy_user */
	u32 (*fs_seg)(void);					/* current_thread_info()->addr_limit */
	int (*s_ctrl)(u32 id, u32 tun, void *val);		/* tx_isp_core_ops_s_ctrl */
	int (*g_ctrl)(u32 id, u32 tun, void *val);		/* tx_isp_core_ops_g_ctrl */
	void (*enable_tuning)(void);
	void (*disable_tuning)(void);
	void (*log)(u32 level, u32 line);			/* isp_printf / printk (line 0: none) */
	void (*day_or_night)(u32 ch, u32 mode, u32 mode8);	/* tisp_day_or_night_s_ctrl */
	void (*complete)(u32 c);
	void (*init_completion)(u32 c);
	void *(*kmalloc)(u32 size, u32 gfp);
	void (*spin_lock_init)(u32 l);
	void (*mutex_init)(u32 m);
	void (*wake_up)(u32 wq, u32 mode, u32 nr, u32 key);
	int (*prepare_to_wait_event)(u32 wq, u32 *entry, u32 state);
	void (*finish_wait)(u32 wq, u32 *entry);
	int (*schedule_timeout)(int timeout);
};

/* the globals of the tuning device (vendor .bss / .data objects) */
struct t32_tun_glob {
	u8 *rd_flag;		/* t32_data_dad90: a frame for the reader */
	u32 rd_wq;		/* t32_data_dad94 */
	u32 *fd_flag;		/* t32_data_d9690: frame done, channels != 0 */
	u32 *fd_flag0;		/* t32_data_d9694: frame done, channel 0 */
	u32 *fd_cnt;		/* t32_data_d9698: 3 x u64 frame-done counters (lo, hi) */
	u32 fd_wq, fd_wq0;	/* t32_data_cf12c, t32_data_cf134 */
	u32 core;		/* t32_data_f3310: the core subdev */
};

/* isp_core_tuning_init(owner): fops / event_cb are the vendor objects cf0c0 / isp_core_tuning_event */
static inline u32 t32_isp_core_tuning_init(const struct t32_tun_ops *ops, u32 owner, u32 fops, u32 event_cb)
{
	u8 *t = ops->kmalloc(T32_TUN_SIZE, T32_TUN_GFP);
	u32 a = T32_IC_A(t);

	if (!t) {
		ops->log(2, 0);
		return 0;
	}
	memset(t, 0, T32_TUN_SIZE);
	t32_ic_w32(a, T32_TUN_OWNER, owner);
	ops->spin_lock_init(a + T32_TUN_LOCK);
	ops->mutex_init(a + T32_TUN_LOCK);	/* the vendor inits the mutex on the spinlock's address */
	ops->init_completion(a + T32_TUN_DONE);
	t32_ic_w32(a, T32_TUN_STATE, T32_TUN_ST_IDLE);
	t32_ic_w32(a, T32_TUN_FOPS, fops);
	t32_ic_w32(a, T32_TUN_EVENT_CB, event_cb);
	return a;
}

/* isp_frame_done_wakeup(data): data word 1 = channel; count the frame, raise the flag, wake */
static inline void t32_isp_frame_done_wakeup(const struct t32_tun_ops *ops, const struct t32_tun_glob *g, u32 data)
{
	u32 ch = t32_ic_r32(data, 4), *c = g->fd_cnt + 2 * ch;

	c[0]++;
	if (!c[0])
		c[1]++;
	if (ch) {
		*g->fd_flag = 1;
		ops->wake_up(g->fd_wq, T32_TASK_NORMAL, 1, 0);
	} else {
		*g->fd_flag0 = 1;
		ops->wake_up(g->fd_wq0, T32_TASK_NORMAL, 1, 0);
	}
}

/* isp_core_tuning_event(tun, event, data) */
static inline int t32_isp_core_tuning_event(const struct t32_tun_ops *ops, const struct t32_tun_glob *g,
					    u32 tun, u32 ev, u32 data)
{
	u32 ch, e, mode;

	switch (ev) {
	case T32_EV_TUN_OPEN:
		t32_ic_w32(tun, T32_TUN_STATE, T32_TUN_ST_CLOSED);
		break;
	case T32_EV_TUN_IDLE:
		t32_ic_w32(tun, T32_TUN_STATE, T32_TUN_ST_IDLE);
		break;
	case T32_EV_FRAME_DONE:
		t32_isp_frame_done_wakeup(ops, g, data);
		break;
	case T32_EV_DAY_NIGHT:
		ch = t32_ic_r32(data, 4);
		e = tun + T32_TUN_ENT + (ch << 5);
		mode = t32_ic_r32(e, T32_TUN_ENT_DN);
		ops->day_or_night(ch & 0xff, mode, mode & 0xff);
		t32_ic_w32(e, T32_TUN_ENT_DN, mode);
		ops->complete(tun + T32_TUN_DONE);
		break;
	}
	return 0;
}

static inline u32 t32_tun_of_file(u32 file)
{
	return t32_ic_r32(t32_ic_r32(t32_ic_r32(file, T32_FILE_PRIV), T32_MISC_DEV), T32_DEV_TUNING);
}

/* isp_core_tunning_open(inode, file) */
static inline int t32_isp_core_tunning_open(const struct t32_tun_ops *ops, u32 file)
{
	u32 t = t32_tun_of_file(file), e;

	if (t32_ic_r32(t, T32_TUN_STATE) != T32_TUN_ST_CLOSED)
		return -1;
	for (e = t + T32_TUN_ENT; e != t + T32_TUN_ENT + 3 * 32; e += 32) {
		memset(T32_IC_P(e), 0x80, 4);
		t32_ic_w32(e, 8, 0x190001);
		t32_ic_w32(e, 12, 0);
	}
	ops->enable_tuning();
	t32_ic_w32(t, T32_TUN_STATE, T32_TUN_ST_OPEN);
	return 0;
}

/* isp_core_tunning_release(inode, file) */
static inline int t32_isp_core_tunning_release(const struct t32_tun_ops *ops, u32 file)
{
	u32 t = t32_tun_of_file(file);

	ops->log(0, 2197);
	if (t32_ic_r32(t, T32_TUN_STATE) != T32_TUN_ST_CLOSED) {
		ops->disable_tuning();
		t32_ic_w32(t, T32_TUN_STATE, T32_TUN_ST_CLOSED);
	}
	return 0;
}

/* isp_core_tunning_default_ioctl(tun, cmd, arg): arg = { id, get, value[2] } */
static inline int t32_isp_core_tunning_default_ioctl(const struct t32_tun_ops *ops, u32 tun, u32 cmd, u32 arg)
{
	u32 ctl[4] = { 0 };
	int ret;

	(void)cmd;
	if (ops->copy_from_user(ctl, arg, sizeof(ctl)))
		return T32_EFAULT;
	if (!ctl[1])
		return ops->s_ctrl(ctl[0], tun, &ctl[2]);
	ret = ops->g_ctrl(ctl[0], tun, &ctl[2]);
	if (ret && ret != T32_ENOIOCTLCMD)
		return ret;
	if (ops->copy_to_user(arg, ctl, sizeof(ctl)))
		return T32_EFAULT;
	return ret;
}

/* isp_core_tunning_unlocked_ioctl(file, cmd, arg); file NULL = the fast path's core device */
static inline int t32_isp_core_tunning_unlocked_ioctl(const struct t32_tun_ops *ops, const struct t32_tun_glob *g,
						      u32 file, u32 cmd, u32 arg)
{
	u32 dev = file ? t32_ic_r32(t32_ic_r32(file, T32_FILE_PRIV), T32_MISC_DEV) :
			 t32_ic_r32(g->core, T32_SD_PRIV);
	u32 t = t32_ic_r32(dev, T32_DEV_TUNING), buf[2] = { 0 };
	int ret;

	if (t32_ic_r32(t, T32_TUN_STATE) != T32_TUN_ST_OPEN)
		return -1;
	if (cmd == T32_TUN_CMD_GET)
		return ops->copy_from_user(buf, arg, 8) ? T32_EFAULT : 0;
	if (cmd == T32_TUN_CMD_PUT) {
		if (ops->copy_from_user(buf, arg, 8))
			return T32_EFAULT;
		return ops->copy_to_user(arg, buf, 8) ? T32_EFAULT : 0;
	}
	ret = t32_isp_core_tunning_default_ioctl(ops, t, cmd, arg);
	return ret == T32_ENOIOCTLCMD ? 0 : ret;
}

/* isp_core_tunning_unlocked_ioctl_fast(id, get, v0, v1): a kernel-side control call */
static inline int t32_isp_core_tunning_unlocked_ioctl_fast(const struct t32_tun_ops *ops, const struct t32_tun_glob *g,
							   const u32 *ctl)
{
	u32 t = t32_ic_r32(t32_ic_r32(g->core, T32_SD_PRIV), T32_DEV_TUNING);

	return t32_isp_core_tunning_default_ioctl(ops, t, T32_TUN_CMD_CTRL, T32_IC_A(ctl));
}

/* isp_tunning_poll(file, wait) */
static inline u32 t32_isp_tunning_poll(const struct t32_tun_ops *ops, const struct t32_tun_glob *g, u32 file, u32 wait)
{
	u32 qproc = wait ? t32_ic_r32(wait, 0) : 0;

	if (qproc)
		ops->call(qproc, file, g->rd_wq, wait);
	return *g->rd_flag != 0;
}

/* isp_tunning_read(file, buf, len, ppos) */
static inline int t32_isp_tunning_read(const struct t32_tun_ops *ops, const struct t32_tun_glob *g,
				       u32 file, u32 buf, u32 len)
{
	u32 left = len;

	if (t32_ic_r32(file, T32_FILE_FLAGS) & T32_O_NONBLOCK)
		return T32_EAGAIN;
	if (!*g->rd_flag)
		return T32_EAGAIN;
	if (!(((buf + len) | buf | len) & ops->fs_seg()))
		left = ops->copy_user(buf, T32_IC_A(g->rd_flag), len);
	if (left)
		return T32_EFAULT;
	*g->rd_flag = 0;
	return len;
}

/* isp_tunning_write(file, buf, len, ppos): only traces */
static inline int t32_isp_tunning_write(const struct t32_tun_ops *ops, u32 len)
{
	ops->log(0, 690);
	return len;
}

/* isp_frame_done_wait(ch, timeout, out): wait_event_interruptible_timeout on the channel's flag,
 * out = the channel's frame counter */
static inline int t32_isp_frame_done_wait(const struct t32_tun_ops *ops, const struct t32_tun_glob *g,
					  u32 ch, int timeout, u32 *out)
{
	u32 *flag = ch ? g->fd_flag : g->fd_flag0, wq = ch ? g->fd_wq : g->fd_wq0;
	u32 entry[5] = { 0 }, *c;
	int ret = timeout;

	*flag = 0;
	if (*flag == 1 && !timeout) {
		c = g->fd_cnt + (ch ? 2 * ch : 0);
		out[0] = c[0];
		out[1] = c[1];
		return 0;
	}
	if (*flag != 1 && timeout) {
		entry[3] = entry[4] = T32_IC_A(&entry[3]);
		for (;;) {
			int r = ops->prepare_to_wait_event(wq, entry, T32_TASK_INTERRUPTIBLE);

			if (*flag == 1) {
				if (!ret)
					ret = 1;
				break;
			}
			if (!ret)
				break;
			if (r) {
				ret = r;
				break;
			}
			ret = ops->schedule_timeout(ret);
		}
		ops->finish_wait(wq, entry);
	}
	c = g->fd_cnt + 2 * ch;
	out[0] = c[0];
	out[1] = c[1];
	if (ret == T32_ERESTARTSYS)
		return T32_ERESTARTSYS;
	return ret ? 0 : T32_ETIMEDOUT;
}

#endif /* TX_ISP_T32_ISPTUN_H */
