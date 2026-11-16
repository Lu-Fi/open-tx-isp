/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ISP core tuning device (vendor tx-isp-t33.ko): isp_core_tuning_init, isp_core_tuning_event,
 * isp_core_tunning_open, isp_core_tunning_release, isp_core_tunning_default_ioctl,
 * isp_core_tunning_unlocked_ioctl.
 *
 * Tuning block (168 bytes): +0 owner, +16 three 32 byte channel entries (+16 of an entry: day/night mode),
 * +132 lock, +144 state (1 idle, 2 closed, 3 open), +148 file ops, +152 completion, +164 event callback.
 * The file reaches it through file +112 -> +264 -> +1700.  Kernel calls go through struct t33_tun_ops.
 */
#ifndef TX_ISP_T33_R12A_H
#define TX_ISP_T33_R12A_H

#include "tx_isp_t33_blk.h"

#define T33_TUN_SIZE 168
#define T33_TUN_GFP 208
#define T33_TUN_ENT 16
#define T33_TUN_DN 32
#define T33_TUN_LOCK 132
#define T33_TUN_STATE 144
#define T33_TUN_FOPS 148
#define T33_TUN_DONE 152
#define T33_TUN_EVENT_CB 164
#define T33_TUN_ST_IDLE 1
#define T33_TUN_ST_CLOSED 2
#define T33_TUN_ST_OPEN 3
#define T33_FILE_DEV 112
#define T33_DEV_PRIV 264
#define T33_DEV_TUN 1700
#define T33_CORE_PRIV 276
#define T33_EV_OPEN 0x4000000
#define T33_EV_IDLE 0x4000001
#define T33_EV_FRAME 0x4000002
#define T33_EV_DAYNIGHT 0x4000003
#define T33_TUN_CMD_GET 0xc0085465U
#define T33_TUN_CMD_PUT 0xc0085466U
#define T33_EFAULT (-14)
#define T33_ENOIOCTLCMD (-515)

struct t33_tun_ops {
	u32 (*copy_from_user)(void *dst, u32 src, u32 n);
	u32 (*copy_to_user)(u32 dst, const void *src, u32 n);
	int (*s_ctrl)(u32 id, void *tun, void *val);		/* tx_isp_core_ops_s_ctrl */
	int (*g_ctrl)(u32 id, void *tun, void *val);		/* tx_isp_core_ops_g_ctrl */
	void (*enable_tuning)(void);
	void (*disable_tuning)(void);
	void (*log)(u32 level, const char *fmt, const char *func, u32 line);
	void (*day_or_night)(u32 ch, u32 mode, u32 mode8);
	void (*complete)(void *c);
	void (*frame_done)(void *data);
	void *(*kmalloc)(u32 size, u32 gfp);
	void (*spin_lock_init)(void *l);
	void (*mutex_init)(void *m, const char *name, void *key);
	void (*init_completion)(void *c);
};

static inline u8 *t33_tun_ptr(const u8 *p, u32 off)
{
	return (u8 *)(unsigned long)t33_le32(p + off);
}

static inline void t33_tun_set(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
	p[off + 2] = (u8)(v >> 16);
	p[off + 3] = (u8)(v >> 24);
}

static inline u8 *t33_tun_of_file(const u8 *file)
{
	return t33_tun_ptr(t33_tun_ptr(t33_tun_ptr(file, T33_FILE_DEV), T33_DEV_PRIV), T33_DEV_TUN);
}

/* isp_core_tuning_init (vendor: private_kmalloc(168, 208)) */
static inline u8 *t33_tun_init(const struct t33_tun_ops *ops, u32 owner, u32 fops, u32 event_cb)
{
	u8 *t = (u8 *)ops->kmalloc(T33_TUN_SIZE, T33_TUN_GFP);
	u32 i;

	if (!t) {
		ops->log(2, "tuning: out of memory\n", "isp_core_tuning_init", 0);
		return 0;
	}
	for (i = 0; i < T33_TUN_SIZE; i++)
		t[i] = 0;
	t33_tun_set(t, 0, owner);
	ops->spin_lock_init(t + T33_TUN_LOCK);
	ops->mutex_init(t + T33_TUN_LOCK, "tuning", 0);
	ops->init_completion(t + T33_TUN_DONE);
	t33_tun_set(t, T33_TUN_STATE, T33_TUN_ST_IDLE);
	t33_tun_set(t, T33_TUN_FOPS, fops);
	t33_tun_set(t, T33_TUN_EVENT_CB, event_cb);
	return t;
}

/* isp_core_tuning_event(tun, event, data) */
static inline int t33_tun_event(const struct t33_tun_ops *ops, u8 *tun, u32 ev, u8 *data)
{
	u32 ch, mode;
	u8 *e;

	switch (ev) {
	case T33_EV_OPEN:
		t33_tun_set(tun, T33_TUN_STATE, T33_TUN_ST_CLOSED);
		break;
	case T33_EV_IDLE:
		t33_tun_set(tun, T33_TUN_STATE, T33_TUN_ST_IDLE);
		break;
	case T33_EV_FRAME:
		ops->frame_done(data);
		break;
	case T33_EV_DAYNIGHT:
		ch = t33_le32(data + 4);
		e = tun + T33_TUN_DN + (ch << 5);
		mode = t33_le32(e);
		ops->day_or_night(ch & 0xff, mode, mode & 0xff);
		t33_tun_set(e, 0, mode);
		ops->complete(tun + T33_TUN_DONE);
		break;
	default:
		break;
	}
	return 0;
}

/* isp_core_tunning_open(inode, file) */
static inline int t33_tun_open(const struct t33_tun_ops *ops, const u8 *file)
{
	u8 *tun = t33_tun_of_file(file), *e;
	u32 i, k;

	if (t33_le32(tun + T33_TUN_STATE) != T33_TUN_ST_CLOSED)
		return -1;
	for (i = 0; i < 3; i++) {
		e = tun + T33_TUN_ENT + 32 * i;
		for (k = 0; k < 4; k++)
			e[k] = 0x80;
		t33_tun_set(e, 8, 0x190001);
		t33_tun_set(e, 12, 0);
	}
	ops->enable_tuning();
	t33_tun_set(tun, T33_TUN_STATE, T33_TUN_ST_OPEN);
	return 0;
}

/* isp_core_tunning_release(inode, file) */
static inline int t33_tun_release(const struct t33_tun_ops *ops, const u8 *file)
{
	u8 *tun = t33_tun_of_file(file);

	ops->log(0, "tuning release\n", "isp_core_tunning_release", 2162);
	if (t33_le32(tun + T33_TUN_STATE) != T33_TUN_ST_CLOSED) {
		ops->disable_tuning();
		t33_tun_set(tun, T33_TUN_STATE, T33_TUN_ST_CLOSED);
	}
	return 0;
}

/* isp_core_tunning_default_ioctl(tun, cmd, arg): arg = { id, get, value[2] } */
static inline int t33_tun_default_ioctl(const struct t33_tun_ops *ops, u8 *tun, u32 arg)
{
	u32 ctl[4] = { 0 };
	int ret;

	if (ops->copy_from_user(ctl, arg, 16))
		return T33_EFAULT;
	if (!ctl[1])
		return ops->s_ctrl(ctl[0], tun, &ctl[2]);
	ret = ops->g_ctrl(ctl[0], tun, &ctl[2]);
	if (ret && ret != T33_ENOIOCTLCMD)
		return ret;
	if (ops->copy_to_user(arg, ctl, 16))
		return T33_EFAULT;
	return ret;
}

/* isp_core_tunning_unlocked_ioctl(file, cmd, arg); file NULL: the core subdev (ispcore_sd) */
static inline int t33_tun_unlocked_ioctl(const struct t33_tun_ops *ops, const u8 *core, const u8 *file,
					 u32 cmd, u32 arg)
{
	const u8 *dev = file ? t33_tun_ptr(t33_tun_ptr(file, T33_FILE_DEV), T33_DEV_PRIV) :
			       t33_tun_ptr(core, T33_CORE_PRIV);
	u8 *tun = t33_tun_ptr(dev, T33_DEV_TUN);
	u32 buf[4] = { 0 };
	int ret;

	if (t33_le32(tun + T33_TUN_STATE) != T33_TUN_ST_OPEN)
		return -1;
	if (cmd == T33_TUN_CMD_GET)
		return ops->copy_from_user(buf, arg, 8) ? T33_EFAULT : 0;
	if (cmd == T33_TUN_CMD_PUT) {
		if (ops->copy_from_user(buf, arg, 8))
			return T33_EFAULT;
		return ops->copy_to_user(arg, buf, 8) ? T33_EFAULT : 0;
	}
	ret = t33_tun_default_ioctl(ops, tun, arg);
	return ret == T33_ENOIOCTLCMD ? 0 : ret;
}

#endif /* TX_ISP_T33_R12A_H */
