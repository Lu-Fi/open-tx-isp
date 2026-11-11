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
 *
 * Phased port: only the commands above are ported so far (idx 0, 1, 9, 16, 21, 23..25, 27 and
 * every idx that the vendor table sends straight to the broadcast: 2, 4, 7, 8, 10, 12, 13,
 * 17..19, 28, 29 and every cmd outside 0x1000000..0x100001e).  The other commands (idx 3, 5,
 * 6, 11, 14, 15, 20, 22, 26, 30) and sd NULL with any other idx (vendor: NULL / table-entry paths, partly a NULL
 * dereference) return T32_IO_UNPORTED; an error pointer sd (vendor: dereferenced) is not
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
