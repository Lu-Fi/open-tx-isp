/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 25: ispcore_core_ops_ioctl (0xbbb80), the core subdev's
 * ioctl(sd, cmd, arg).  cmd - 0x1000000 = idx; idx 0..30 go through the vendor jump table
 * c31e0 (c325c when sd is NULL), every command then ends in the subdev broadcast below.
 *
 *  broadcast (vendor label bbbe0 / bbbe4, table c319c for idx 0..16): for each of the 16
 *    subdev words sd + 84 .. + 144 that is not NULL, by idx:
 *      0      ops[0] (sd + 264 -> + 0), its + 4: fn(sub, arg)
 *      1      ops[3] (+ 12), its + 4:        fn(sub, arg)
 *      3, 7, 8, 10, 12..16  ops[0] + 32:     fn(sub, cmd, arg)
 *      other  no call
 *    A missing ops block / function gives -515 (-ENOIOCTLCMD).  After each subdev a result
 *    other than 0 / -515 is returned at once (also a preset one with no call); at the end
 *    -515 becomes 0.
 *  idx 0 / 1: first the core's own ops[0] / ops[3] + 4 (sd, arg); a result other than 0 /
 *    -515 is printed ("ioctl", line 2820) and returned, else the broadcast starts with it.
 *  sd NULL, idx 0 / 1: printed, -19.
 *
 *  idx 9: bayer order per marked pipe (t32_io_bayer); 16: oneshot tick(sd), like idx 0 / 1;
 *    21: tisp_ae_algo_deinit(arg[1]); 24 / 25: tisp_awb_algo_init(arg[1], 1, arg + 8) /
 *    (arg[1], 0, 0); 23 / 27: AE / AWB user block (t32_io_algo; -14 copy fault, magic
 *    0x13501f4 else -1 (AE) / -22 (AWB), both printed twice; AWB handled only when word 2 = 1).
 *  idx 3: stream on (t32_io_stream_on; no private block: -22; any pipe state < 2: nothing);
 *    5: down_timeout(priv + 4032, 1000) (-145 printed, other errors like idx 0);
 *    6: arg 0 = u8 priv + 4288 = 1, 2 = tisp_core_reset(priv, 1), else reset(priv, 0) + tick;
 *    14 / 15: all format table users off / on under the lock priv + 288.
 *  idx 20: AE init (t32_io_ae_init); 22 / 26: AE / AWB info (magic else -1, copy fault -14;
 *    a failed waitdone is printed with level 1 (vendor a2 / a3 are leftovers, ours 0) and
 *    handled like idx 0; AWB: copy-to faults are only printed, the result stays 0).
 *
 *  Broadcast only: idx 2, 4, 7, 8, 10, 12, 13, 17..19, 28, 29 and every cmd outside
 *  0x1000000..0x100001e.
 *
 * Phased port: every command except idx 11 and 30 is ported so far.  Those two and sd NULL
 * with idx >= 2 (vendor: table c325c entries, then a NULL dereference on most paths) return
 * T32_IO_UNPORTED; an error pointer sd (vendor: dereferenced) is not
 * handled either.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispioctl_oracle).
 */
#ifndef TX_ISP_T32_ISPIOCTL_H
#define TX_ISP_T32_ISPIOCTL_H

#include "tx_isp_t32_ispinit.h"

#define T32_IO_CMD0		0x01000000U
#define T32_IO_NOCMD		0xfffffdfdU	/* -515, -ENOIOCTLCMD */
#define T32_IO_NODEV		0xffffffedU	/* -19, -ENODEV */
#define T32_IO_UNPORTED		0x7fff0000U	/* not a vendor value: command group not ported yet */
#define T32_IO_SD_SUBDEV	84		/* 16 subdev words */
#define T32_IO_NSUBDEV		16
#define T32_IO_SD_OPS		264		/* ops[0] core, ops[3] video */
#define T32_IO_LINE		2820
#define T32_IO_MAGIC		0x013501f4U	/* first word of the AE / AWB user blocks */
#define T32_IO_FAULT		0xfffffff2U	/* -14, -EFAULT */
#define T32_IO_INVAL		0xffffffeaU	/* -22, -EINVAL */
#define T32_IO_A(p)		((u32)(uintptr_t)(p))
#define T32_CI_PIPE_ON		372		/* + 104 * pipe: 1 = bayer order to be written */
#define T32_CI_STREAMING	4012
#define T32_CI_SEM		4032
#define T32_CI_USERS_ON		4284
#define T32_CI_RESET_REQ	4288		/* u8 */
#define T32_CI_USER_ON		276		/* in a 284 B user entry */
#define T32_IO_TIMEDOUT		0xffffff6fU	/* -145, -ETIMEDOUT */
#define T32_CI_SENSOR		376		/* + 104 * pipe: sensor attribute block */
#define T32_CI_PIPE_EXP		384		/* + 104 * pipe: + 384 / + 388 / + 392 */
#define T32_IO_AEINFO		1972		/* AE info: bus, sensor u16 + 276 / + 278, pipe + 392 / 384 / 388 */
#define T32_IO_AEINFO_SIZE	1996
#define T32_IO_AWBINFO_SIZE	12288

struct t32_io_ops {
	void (*print)(u32 level, u32 fmt, u32 func, u32 line);		/* isp_printf */
	u32 (*call)(u32 fn, u32 a0, u32 a1, u32 a2);			/* subdev op */
	u32 fmt_err, func;						/* cd71c, c34a0 */
	/* phase 2: AE / AWB / bayer / tick */
	u32 (*copy_from_user)(u32 to, u32 from, u32 n);			/* private_copy_from_user */
	void (*bayer)(u32 pipe, u32 code);				/* mbus_to_bayer_write */
	u32 (*tick)(u32 sd);						/* tx_isp_core_oneshot_tick_isra_18 */
	void (*ae_deinit)(u32 h);					/* tisp_ae_algo_deinit */
	void (*ae_handle)(u32 h, u32 buf);				/* tisp_ae_algo_handle */
	void (*awb_init)(u32 h, u32 on, u32 p);				/* tisp_awb_algo_init */
	void (*awb_handle)(u32 h, u32 buf);				/* tisp_awb_algo_handle */
	u32 fmt_copy, fmt_magic, fmt_inval, func_ae, func_awb;		/* cd550 cd570 cd604, c342c c33ec */
	/* phase 3: stream on, wait, reset, user enable */
	void (*lock)(u32 lock, u32 *flags);				/* __private_spin_lock_irqsave */
	void (*unlock)(u32 lock, u32 flags);				/* private_spin_unlock_irqrestore */
	void (*aov_wake_up)(u32 pipe);					/* lib_tisp_aov_wake_up */
	void (*stream_on)(void);					/* lib_tisp_stream_on */
	u32 (*down_timeout)(u32 sem, u32 ms);
	void (*core_reset)(u32 priv, u32 on);				/* tisp_core_reset */
	u32 *npipe;							/* f3354 */
	u32 fmt_nodev, fmt_on, fmt_timeout, func_on, func_wait;		/* cd4ec cd504 cd524, c3488 c346c */
	/* phase 4: AE init, AE / AWB info */
	void (*ae_init)(u32 pipe, u32 on, u32 p);			/* tisp_ae_algo_init */
	u32 (*exp2)(u32 v, u32 in_q, u32 out_q);			/* tisp_math_exp2 */
	u32 (*ae_waitdone)(u32 pipe);					/* tisp_ae_algo_waitdone */
	u32 (*ae_getinfo)(u32 pipe);					/* tisp_ae_algo_getinfo */
	u32 (*awb_waitdone)(u32 h);					/* tisp_awb_algo_waitdone */
	u32 (*awb_getinfo)(u32 buf);					/* tisp_awb_algo_getinfo */
	u32 (*copy_to_user)(u32 to, u32 from, u32 n);			/* private_copy_to_user */
	u32 fmt_ae_wait, fmt_awb_wait, fmt_copy_to, func_aeinfo, func_awbinfo;	/* cd5a4 cd5e4 cd5c4, c344c c340c */
};

/* fn of the subdev ops block ops[blk], word w; 0 when the block or the function is missing */
static inline u32 t32_io_fn(u32 s, u32 blk, u32 w)
{
	u32 o = t32_ic_r32(t32_ic_r32(s, T32_IO_SD_OPS), 4 * blk);

	return o ? t32_ic_r32(o, w) : 0;
}

/* table c319c: 1 = ops[0] + 4, 2 = ops[3] + 4, 3 = ops[0] + 32 (with cmd), 0 = no call */
static const u8 t32_io_bc_kind[17] = { 1, 2, 0, 3, 0, 0, 0, 3, 3, 0, 3, 0, 3, 3, 3, 3, 3 };

static inline u32 t32_io_bcast(const struct t32_io_ops *ops, u32 sd, u32 cmd, u32 arg, u32 ret)
{
	u32 idx = cmd - T32_IO_CMD0, i, s, fn, k;

	for (i = 0; i < T32_IO_NSUBDEV; i++) {
		s = t32_ic_r32(sd, T32_IO_SD_SUBDEV + 4 * i);
		if (!s)
			continue;
		k = idx < 17 ? t32_io_bc_kind[idx] : 0;
		if (k == 1 || k == 2) {
			fn = t32_io_fn(s, k == 1 ? 0 : 3, 4);
			if (!fn) {
				ret = T32_IO_NOCMD;
				continue;
			}
			ret = ops->call(fn, s, arg, 0);
		} else if (k == 3) {
			fn = t32_io_fn(s, 0, 32);
			if (!fn) {
				ret = T32_IO_NOCMD;
				continue;
			}
			ret = ops->call(fn, s, cmd, arg);
		}
		if (ret != 0 && ret != T32_IO_NOCMD)
			return ret;
	}
	return ret == T32_IO_NOCMD ? 0 : ret;
}

/* the vendor error tail (label bcc0c): the general "ioctl" print, the result */
static inline u32 t32_io_err(const struct t32_io_ops *ops, u32 ret)
{
	ops->print(2, ops->fmt_err, ops->func, T32_IO_LINE);
	return ret;
}

/* idx 9: write the bayer order of every pipe marked 1 (priv + 372 + 104 * pipe), clear the mark */
static inline void t32_io_bayer(const struct t32_io_ops *ops, u32 sd)
{
	u32 priv = t32_ic_r32(sd, T32_IS_PRIV), i, e;

	for (i = 0; i < t32_ic_r32(priv, T32_CI_SATTR); i++) {
		e = priv + T32_CI_PIPE_ON + T32_CI_PIPE_SIZE * i;
		if (t32_ic_r32(e, 0) != 1)
			continue;
		ops->bayer(i, t32_ic_r32(e, -40));
		t32_ic_w32(e, 0, 0);
	}
}

/* idx 23 / 27: AE (64 B) / AWB (24 B) user block { magic, handle, ... }; 0 or an error to return */
static inline u32 t32_io_algo(const struct t32_io_ops *ops, u32 arg, int awb)
{
	u32 buf[16], func = awb ? ops->func_awb : ops->func_ae;

	if (ops->copy_from_user(T32_IO_A(buf), arg, awb ? 24 : 64)) {
		ops->print(2, ops->fmt_copy, func, awb ? 2721 : 2449);
		return t32_io_err(ops, T32_IO_FAULT);
	}
	if (buf[0] != T32_IO_MAGIC) {
		if (awb) {
			ops->print(2, ops->fmt_inval, func, 2726);
			return t32_io_err(ops, T32_IO_INVAL);
		}
		ops->print(2, ops->fmt_magic, func, 2454);
		return t32_io_err(ops, 0xffffffffU);
	}
	if (!awb)
		ops->ae_handle(buf[1], T32_IO_A(buf));
	else if (buf[2] == 1)
		ops->awb_handle(buf[1], T32_IO_A(buf));
	return 0;
}

/* a usable core private block: sd + 280 not NULL and no error pointer */
static inline u32 t32_io_priv(u32 sd)
{
	u32 priv = t32_ic_r32(sd, T32_IS_PRIV);

	return priv && !T32_CI_ERR(priv) ? priv : 0;
}

/* idx 3: stream on once every pipe state is >= 2; 1 = go on with the broadcast, else the error */
static inline u32 t32_io_stream_on(const struct t32_io_ops *ops, u32 sd)
{
	u32 priv = t32_io_priv(sd), i;

	if (!priv) {
		ops->print(2, ops->fmt_nodev, ops->func_on, 2081);
		return t32_io_err(ops, T32_IO_INVAL);
	}
	for (i = 0; i < 4; i++)
		if ((int)t32_ic_r32(priv, T32_CI_STATE + 4 * i) < 2)
			return 0;
	for (i = 0; i < t32_ic_r32(priv, T32_CI_SATTR); i++)
		ops->aov_wake_up(i & 0xff);
	if (!t32_ic_r32(priv, T32_CI_STREAMING)) {
		*ops->npipe = 0;
		ops->stream_on();
		t32_ic_w32(priv, T32_CI_STREAMING, 1);
	}
	ops->print(1, ops->fmt_on, ops->func_on, 2100);
	return 0;
}

/* idx 5: wait (1 s) for the core semaphore priv + 4032 */
static inline u32 t32_io_wait(const struct t32_io_ops *ops, u32 sd, u32 *ret)
{
	u32 priv = t32_io_priv(sd);

	if (!priv) {
		ops->print(2, ops->fmt_nodev, ops->func_wait, 2171);
		return t32_io_err(ops, T32_IO_INVAL);
	}
	*ret = ops->down_timeout(priv + T32_CI_SEM, 1000);
	if (*ret == T32_IO_TIMEDOUT) {
		ops->print(2, ops->fmt_timeout, ops->func_wait, 2181);
		return t32_io_err(ops, *ret);
	}
	if (*ret != 0 && *ret != T32_IO_NOCMD)
		return t32_io_err(ops, *ret);
	return 0;
}

/* idx 14 / 15: all format table users off / on (priv + 4284 = current state), under priv + 288 */
static inline void t32_io_users(const struct t32_io_ops *ops, u32 sd, u32 on)
{
	u32 priv = t32_ic_r32(sd, T32_IS_PRIV), flags = 0, i;

	ops->lock(priv + T32_CI_LOCK, &flags);
	if (on ? t32_ic_r32(priv, T32_CI_USERS_ON) != 1 : t32_ic_r32(priv, T32_CI_USERS_ON) != 0) {
		t32_ic_w32(priv, T32_CI_USERS_ON, on);
		for (i = 0; i < t32_ic_r32(priv, T32_CI_NUSERS); i++)
			t32_ic_w32(t32_ic_r32(priv, T32_CI_USERS) + T32_CI_USER_SIZE * i, T32_CI_USER_ON, on);
	}
	ops->unlock(priv + T32_CI_LOCK, flags);
}

/* idx 20: AE init for pipe arg[1] with the sensor limits (exp2 of the Q16 logs to Q10) */
static inline void t32_io_ae_init(const struct t32_io_ops *ops, u32 sd, u32 arg)
{
	u32 priv = t32_ic_r32(sd, T32_IS_PRIV), pipe = t32_ic_r32(arg, 4);
	u32 pe = priv + T32_CI_PIPE_SIZE * pipe, sen = t32_ic_r32(pe, T32_CI_SENSOR);

	t32_ic_w32(arg, 24, 0);
	t32_ic_w32(arg, 72, t32_ic_r32(priv, T32_CI_BUS + 4 * pipe) ? 0 : 1024);
	ops->ae_init(pipe, 1, arg);
	t32_ic_w32(arg, 108, t32_ic_r32(pe, T32_CI_PIPE_EXP + 8));
	t32_ic_w32(arg, 12, t32_ic_r32(sen, 216));
	t32_ic_w32(arg, 16, ops->exp2(t32_ic_r32(sen, 152), 16, 10));
	t32_ic_w32(arg, 20, ops->exp2(t32_ic_r32(sen, 184), 16, 10));
	t32_ic_w32(arg, 60, t32_ic_r32(sen, 232));
	t32_ic_w32(arg, 64, ops->exp2(t32_ic_r32(sen, 168), 16, 10));
	t32_ic_w32(arg, 68, ops->exp2(t32_ic_r32(sen, 200), 16, 10));
}

/* idx 22 / 26: copy the user block header { magic, handle, .. } (n B); 0 or the error to return */
static inline u32 t32_io_hdr(const struct t32_io_ops *ops, u32 *buf, u32 arg, u32 n, u32 func, u32 line)
{
	if (ops->copy_from_user(T32_IO_A(buf), arg, n)) {
		ops->print(2, ops->fmt_copy, func, line);
		return t32_io_err(ops, T32_IO_FAULT);
	}
	if (buf[0] != T32_IO_MAGIC) {
		ops->print(2, ops->fmt_magic, func, line + 5);
		return t32_io_err(ops, 0xffffffffU);
	}
	return 0;
}

/* a failed waitdone: printed (level 1), then like idx 0: -515 goes on to the broadcast */
static inline u32 t32_io_wait_err(const struct t32_io_ops *ops, u32 fmt, u32 ret)
{
	ops->print(1, fmt, 0, 0);
	return ret == T32_IO_NOCMD ? 0 : t32_io_err(ops, ret);
}

/* idx 22: AE info of pipe buf[1] completed with the bus / sensor / pipe words, copied to arg + 8 */
static inline u32 t32_io_ae_info(const struct t32_io_ops *ops, u32 sd, u32 arg, u32 *ret)
{
	u32 priv = t32_ic_r32(sd, T32_IS_PRIV), buf[2], pipe, info, pe, e;

	e = t32_io_hdr(ops, buf, arg, 8, ops->func_aeinfo, 2410);
	if (e)
		return e;
	pipe = buf[1];
	*ret = ops->ae_waitdone(pipe);
	if (*ret)
		return t32_io_wait_err(ops, ops->fmt_ae_wait, *ret);
	info = ops->ae_getinfo(pipe);
	pe = priv + T32_CI_PIPE_SIZE * pipe;
	t32_ic_w32(info, T32_IO_AEINFO, t32_ic_r32(priv, T32_CI_BUS + 4 * pipe));
	t32_ic_w32(info, T32_IO_AEINFO + 4, *(u16 *)T32_IC_P(t32_ic_r32(pe, T32_CI_SENSOR) + 276));
	t32_ic_w32(info, T32_IO_AEINFO + 8, *(u16 *)T32_IC_P(t32_ic_r32(pe, T32_CI_SENSOR) + 278));
	t32_ic_w32(info, T32_IO_AEINFO + 12, t32_ic_r32(pe, T32_CI_PIPE_EXP + 8));
	t32_ic_w32(info, T32_IO_AEINFO + 16, t32_ic_r32(pe, T32_CI_PIPE_EXP));
	t32_ic_w32(info, T32_IO_AEINFO + 20, t32_ic_r32(pe, T32_CI_PIPE_EXP + 4));
	if (ops->copy_to_user(arg + 8, info, T32_IO_AEINFO_SIZE)) {
		ops->print(2, ops->fmt_copy_to, ops->func_aeinfo, 2436);
		return t32_io_err(ops, T32_IO_FAULT);
	}
	return 0;
}

/* idx 26: AWB info (36 B block back to arg, 12 KiB statistics to arg + 36); copy faults only printed */
static inline u32 t32_io_awb_info(const struct t32_io_ops *ops, u32 sd, u32 arg, u32 *ret)
{
	u32 priv = t32_ic_r32(sd, T32_IS_PRIV), buf[9], flags = 0, p, e;

	e = t32_io_hdr(ops, buf, arg, 36, ops->func_awbinfo, 2488);
	if (e)
		return e;
	*ret = ops->awb_waitdone(buf[1]);
	if (*ret)
		return t32_io_wait_err(ops, ops->fmt_awb_wait, *ret);
	ops->lock(priv + T32_CI_LOCK, &flags);
	p = ops->awb_getinfo(T32_IO_A(buf));
	ops->unlock(priv + T32_CI_LOCK, flags);
	if (!p)
		return 0;
	if (ops->copy_to_user(arg, T32_IO_A(buf), 36))
		ops->print(2, ops->fmt_copy_to, ops->func_awbinfo, 2507);
	else if (ops->copy_to_user(arg + 36, p, T32_IO_AWBINFO_SIZE))
		ops->print(2, ops->fmt_copy_to, ops->func_awbinfo, 2512);
	return 0;
}

static inline u32 t32_ispcore_core_ops_ioctl(const struct t32_io_ops *ops, u32 sd, u32 cmd, u32 arg)
{
	u32 idx = cmd - T32_IO_CMD0, ret = 0, fn;

	if (!sd) {
		if (idx > 1)
			return T32_IO_UNPORTED;
		ops->print(2, ops->fmt_err, ops->func, T32_IO_LINE);
		return T32_IO_NODEV;
	}
	switch (idx) {
	case 0:
	case 1:
		fn = t32_io_fn(sd, idx ? 3 : 0, 4);
		if (!fn) {
			ret = T32_IO_NOCMD;
			break;
		}
		ret = ops->call(fn, sd, arg, 0);
		if (ret != 0 && ret != T32_IO_NOCMD) {
			ops->print(2, ops->fmt_err, ops->func, T32_IO_LINE);
			return ret;
		}
		break;
	case 3:
		ret = t32_io_stream_on(ops, sd);
		if (ret)
			return ret;
		break;
	case 5:
		fn = t32_io_wait(ops, sd, &ret);
		if (fn)
			return fn;
		break;
	case 6:	/* arg is a value: 0 = reset request flag, 2 = core reset on, else off + tick */
		if (!arg) {
			*T32_IC_P(t32_ic_r32(sd, T32_IS_PRIV) + T32_CI_RESET_REQ) = 1;
			break;
		}
		ops->core_reset(t32_ic_r32(sd, T32_IS_PRIV), arg == 2);
		if (arg != 2)
			ops->tick(sd);
		break;
	case 14:
	case 15:
		t32_io_users(ops, sd, idx == 15);
		break;
	case 20:
		t32_io_ae_init(ops, sd, arg);
		break;
	case 22:
	case 26:
		fn = idx == 22 ? t32_io_ae_info(ops, sd, arg, &ret) : t32_io_awb_info(ops, sd, arg, &ret);
		if (fn)
			return fn;
		break;	/* ret: 0, or -515 from a waitdone */
	case 9:
		t32_io_bayer(ops, sd);
		break;
	case 16:
		ret = ops->tick(sd);
		if (ret != 0 && ret != T32_IO_NOCMD)
			return t32_io_err(ops, ret);
		break;
	case 21:
		ops->ae_deinit(t32_ic_r32(arg, 4));
		break;
	case 23:
	case 27:
		ret = t32_io_algo(ops, arg, idx == 27);
		if (ret)
			return ret;
		break;
	case 24:
		ops->awb_init(t32_ic_r32(arg, 4), 1, arg + 8);
		break;
	case 25:
		ops->awb_init(t32_ic_r32(arg, 4), 0, 0);
		break;
	case 2: case 4: case 7: case 8: case 10: case 12: case 13: case 17: case 18: case 19:
	case 28: case 29:
		break;
	default:
		if (idx < 31)
			return T32_IO_UNPORTED;
		break;
	}
	return t32_io_bcast(ops, sd, cmd, arg, ret);
}

#endif
