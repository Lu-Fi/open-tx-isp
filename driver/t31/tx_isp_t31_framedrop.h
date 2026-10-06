/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T31_FRAMEDROP_H
#define TX_ISP_T31_FRAMEDROP_H

/*
 * T31 frame drop as in the stock tx-isp module (tisp_set_frame_drop,
 * tisp_get_frame_drop and the ioctls 0xc00456e6 / 0xc00456e7, which copy
 * three 12-byte IMPISPFrameDrop {enable, lsize, fmark} records).
 *
 * Per channel two ISP registers at ((ch + 0x98) << 8) hold the setting:
 *   +0x130  lsize   window of lsize + 1 frames, lsize 0..31
 *   +0x134  fmark   bit n set = frame n of the window is output, clear = drop
 * Reset state lsize 0 / fmark 1 outputs every frame; "disabled" writes it.
 * The ISP drops the frames itself; the driver only programs the registers
 * (a software window on top would drop a second time).
 * Plain data in and out so the host test can run it.
 */
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#else
#include <stdint.h>
#include <errno.h>
typedef uint32_t u32;
typedef uint8_t u8;
#endif

#define TX_ISP_SET_FRAME_DROP_CMD 0xc00456e6U
#define TX_ISP_GET_FRAME_DROP_CMD 0xc00456e7U
#define TISP_FRAME_DROP_CHANNELS 3U
#define TISP_FRAME_DROP_LSIZE_MAX 32U
#define TISP_FRAME_DROP_REG_LSIZE 0x130U
#define TISP_FRAME_DROP_REG_FMARK 0x134U

/* user layout of one channel (IMPISPFrameDrop, 12 bytes on MIPS32) */
struct tisp_frame_drop_user {
	u32 enable;
	u8 lsize;
	u8 pad[3];
	u32 fmark;
};

static inline u32 tisp_frame_drop_base(u32 channel)
{
	return (channel + 0x98U) << 8;
}

/* Register pair for a request: -EINVAL for lsize >= 32 (stock refuses). */
static inline int tisp_frame_drop_regs(u32 enable, u32 lsize, u32 fmark,
				       u32 *reg_lsize, u32 *reg_fmark)
{
	if (lsize >= TISP_FRAME_DROP_LSIZE_MAX)
		return -EINVAL;
	*reg_lsize = enable ? lsize : 0;
	*reg_fmark = enable ? fmark : 1;
	return 0;
}

#endif
