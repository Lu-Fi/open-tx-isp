/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host replacement for driver/t32/compat.h of Matt Davis' recovered T32
 * driver (opensensor/open-tx-isp PR #15, 2e2e06bc), so that selected
 * mechanical functions build as a user-space reference.  The recovered C
 * keeps MIPS o32 registers in u32 and casts pointers through u32, so the
 * oracle links non-PIE and keeps every object it hands to that code (data,
 * heap pool, stack) below 4 GiB (see env.c).
 */
#ifndef T32_ORACLE_REF_COMPAT_H
#define T32_ORACLE_REF_COMPAT_H

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define __aligned(x)		__attribute__((aligned(x)))
/* gp is loaded from current_thread_info() and never used by the block code */
#define current_thread_info()	((void *)0)

#endif
