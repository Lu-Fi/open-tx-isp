/* SPDX-License-Identifier: GPL-2.0 */
/* Host oracle environment for Matt Davis' recovered T32 C (see env.c). */
#ifndef T32_ORACLE_ENV_H
#define T32_ORACLE_ENV_H

#include <stdint.h>

#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
typedef uint64_t u64;

#define T32O_MAX_WRITES	4096

struct t32o_log {
	unsigned int n, printfs;
	u32 irq;
	struct { u32 reg, val; } w[T32O_MAX_WRITES];
};
extern struct t32o_log t32o_log;

extern u32 t32_data_e28f0[4], t32_data_dd0a4[4], t32_data_dc274[4], t32_data_da844[4];
extern u8 t32_data_d03a0[4];

typedef u64 (*t32o_fn)(u32, u32, u32, u32, u32);

void *t32o_alloc(u32 size);
void t32o_reset(void);
u64 t32o_call(t32o_fn fn, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

/* reference functions (ref/, o32 shape) */
u64 tisp_ccm_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ccm_interp_by_ev(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

#define T32O_HOST(v)	((void *)(uintptr_t)(v))
#define T32O_PTR(p)	((u32)(uintptr_t)(p))

#endif
