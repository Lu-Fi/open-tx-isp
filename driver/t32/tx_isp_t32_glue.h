/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common helpers of the T32 kernel-glue ports (sensor ops, system register
 * access, VIC/CSI/VIN/frame-channel glue) from the vendor module (Matt Davis'
 * recovered C, PR #15).
 *
 * The vendor objects are plain memory with 32-bit fields; a pointer stored in
 * such an object is a 32-bit value on the T32 (MIPS32).  T32_PTR() turns a
 * stored pointer word back into a pointer, T32_WORD() a pointer into the word
 * to store.  On the host oracle every object lives below 4 GiB, so the same
 * code runs unchanged there.
 */
#ifndef TX_ISP_T32_GLUE_H
#define TX_ISP_T32_GLUE_H

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

#define T32_PTR(w)	((u8 *)(uintptr_t)(u32)(w))
#define T32_WORD(p)	((u32)(uintptr_t)(p))

static inline u32 t32_g_rd32(const u8 *p, u32 off)
{
	u32 v;

	memcpy(&v, p + off, 4);
	return v;
}

static inline u16 t32_g_rd16(const u8 *p, u32 off)
{
	u16 v;

	memcpy(&v, p + off, 2);
	return v;
}

static inline void t32_g_wr32(u8 *p, u32 off, u32 v)
{
	memcpy(p + off, &v, 4);
}

static inline void t32_g_wr16(u8 *p, u32 off, u16 v)
{
	memcpy(p + off, &v, 2);
}

/* struct tx_isp_subdev (T32 4.4.94 SDK, see tx_isp_t32_abi.h) */
#define T32_G_SD_NOTIFY		0x098	/* int (*)(sd, u32 cmd, void *arg) */
#define T32_G_SD_BASE		0x0f4	/* ioremapped register base */

#endif
