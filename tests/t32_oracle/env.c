// SPDX-License-Identifier: GPL-2.0
/*
 * Fake kernel / ISP environment for the reference functions in ref/ (Matt
 * Davis' recovered T32 C, opensensor/open-tx-isp PR #15 @ 2e2e06bc).
 *
 * The recovered code calls through u32 "callee" values with up to 64 u32
 * arguments and returns u64 = v1:v0, so every stub below has the o32 shape
 * u64 f(u32 a0, u32 a1, u32 a2, u32 a3); a 64-bit value is passed as
 * (lo, hi) in two registers.  All memory the reference code sees (these
 * arrays, the pool, the stack in t32o_call) is static and the binary is
 * linked non-PIE, so its addresses fit in u32.
 *
 * Data objects keep the OEM names (t32_data_<OEM .data/.bss address>, see
 * driver/t32/data-aliases.lds@2e2e06bc); initial values of .data objects are
 * copied from driver/t32/data.c@2e2e06bc (line numbers below).
 */
#include <stdint.h>
#include <string.h>
#include <ucontext.h>
#include "t32_oracle_env.h"

/* ---- .bss objects (zero at module load) ---- */
u32 t32_data_e28f0[4] __attribute__((aligned(16)));	/* per-channel CCM buffers (tisp_ccm_init) */
u32 t32_data_dd0a4[4] __attribute__((aligned(16)));	/* per-channel tuning-parameter base */
u32 t32_data_dc274[4] __attribute__((aligned(16)));	/* per-channel LUT info (stLutInfo), +24 = CCM knots */
u32 t32_data_da844[4] __attribute__((aligned(16)));	/* register shadow (tisp_reg_write mode 1) */

/* top block (tisp_top_init / tisp_top_reg_cfg) */
u8 t32_data_e1460[8] __attribute__((aligned(4)));	/* per-channel "configured" flags */
u32 t32_data_e1464[4];		/* per-channel top enable word */
u32 t32_data_e1474[4];		/* per-pipeline-parity DE-IR flag */
u32 t32_data_e1484[4];		/* per-channel copy of the tuning base */
u8 t32_data_dc284[1808] __attribute__((aligned(4)));	/* 2 x 904 B channel state, +900 = pipeline count */

/* IQ bin parser (tisp_bin_data_parse), per-channel state, all .bss */
u8 t32_data_da850[704] __attribute__((aligned(4)));	/* 336 B per channel, +224 channel count, +232 14 B copy */
u8 t32_data_dd114[224] __attribute__((aligned(4)));	/* 112 B per channel: copy of the bin's section table */
u8 t32_data_dc1a4[56] __attribute__((aligned(4)));	/* 28 B per channel: largest payload sizes seen */
u32 t32_data_e1820[4];					/* gamma payload size per channel */

/* ---- .data objects ---- */
/* data.c:19093 (+0x2180) = 1: tisp_reg_write queues into the shadow buffer.
 * The oracle sets mode 0 (direct system_reg_write) so it sees each write;
 * the mode is a transport choice and not part of the block's arithmetic. */
u8 t32_data_d03a0[4] __attribute__((aligned(4))) = { 0 };
/* data.c (bf2d0): first 8 words of the table the parser's checksum uses (the standard CRC-32 table start) */
u32 t32_data_bf2d0[8] = { 0, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3 };
/* data.c (c8064 "1.00", c806c "%s", c8070 "PRJ007"): version tag, format, project name checked in the bin header */
char t32_data_c8064[8] = "1.00", t32_data_c806c[4] = "%s", t32_data_c8070[8] = "PRJ007";
/* printf format strings of the parser: content irrelevant */
char t32_data_bf23c[4] = "f", t32_data_c7ff8[4] = "f", t32_data_c8030[4] = "f", t32_data_c8078[4] = "f",
     t32_data_c80b8[4] = "f", t32_data_c80f4[4] = "f", t32_data_c8134[4] = "f", t32_data_c8174[4] = "f",
     t32_data_c81a4[4] = "f";
/* data.c:19437.. (+0x26e0, 128 B): colour-order map of tisp_top_init */
u32 t32_data_d0900[32] = { 0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
/* data.c:25849 (+0x8b10, 40 B): API matrix default, unity, flag byte 0 */
u32 t32_data_d6d30[10] = { 0, 0x400, 0, 0, 0, 0x400, 0, 0, 0, 0x400 };
/* data.c:25859 (+0x8b38, 36 B): matrix default, unity */
u32 t32_data_d6d58[9] = { 0x400, 0, 0, 0, 0x400, 0, 0, 0, 0x400 };
/* data.c:25868 (+0x8b5c, 104 B): working parameter block default */
u32 t32_data_d6d7c[26] = { 0x400, 0, 0, 0, 0x400, 0, 0, 0, 0x400, 0, 0, 0, 0, 0, 0,
			   0x00800010, 0x06000400, 0x00800040, 0, 0,
			   0x04000400, 0x04000400, 0x04000400, 0x04000400, 0x04000400, 0x04000400 };
/* data.c:25895 (+0x8bc8, 16 B): state { ev lo, ev hi, ct 5000, 255 } */
u32 t32_data_d6de8[4] = { 0, 0, 0x1388, 0xff };
/* printf format strings: content irrelevant */
char t32_data_bfe60[4] = "f", t32_data_ca1b4[4] = "f", t32_data_ca1f8[4] = "f";

/* ---- log ---- */
struct t32o_log t32o_log;

/* ---- heap pool for private_vmalloc and the oracle's own buffers ---- */
static u8 pool[8u << 20] __attribute__((aligned(64)));
static u32 pool_used;

void *t32o_alloc(u32 size)
{
	void *p;

	size = (size + 63u) & ~63u;
	if (pool_used + size > sizeof(pool))
		return 0;
	p = pool + pool_used;
	pool_used += size;
	memset(p, 0, size);
	return p;
}

void t32o_reset(void)
{
	pool_used = 0;
	memset(&t32o_log, 0, sizeof(t32o_log));
	memset(t32_data_e28f0, 0, sizeof(t32_data_e28f0));
	memset(t32_data_dd0a4, 0, sizeof(t32_data_dd0a4));
	memset(t32_data_dc274, 0, sizeof(t32_data_dc274));
	memset(t32_data_da844, 0, sizeof(t32_data_da844));
	memset(t32_data_e1460, 0, sizeof(t32_data_e1460));
	memset(t32_data_e1464, 0, sizeof(t32_data_e1464));
	memset(t32_data_e1474, 0, sizeof(t32_data_e1474));
	memset(t32_data_e1484, 0, sizeof(t32_data_e1484));
	memset(t32_data_dc284, 0, sizeof(t32_data_dc284));
	memset(t32_data_da850, 0, sizeof(t32_data_da850));
	memset(t32_data_dd114, 0, sizeof(t32_data_dd114));
	memset(t32_data_dc1a4, 0, sizeof(t32_data_dc1a4));
	memset(t32_data_e1820, 0, sizeof(t32_data_e1820));
}

/* ---- kernel / driver stubs (o32 shape) ---- */
u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return a0 ? (u32)(uintptr_t)t32o_alloc(a0) : 0;
}

/* kernel allocators: vmalloc(0) and kmalloc(0) return NULL as in the kernel's callers expect an error */
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return a0 ? (u32)(uintptr_t)t32o_alloc(a0) : 0;
}

u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = a0;
		t32o_log.w[t32o_log.n].val = a1;
	}
	t32o_log.n++;
	return 0;
}

u64 system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return 0x5a000000u | a0;	/* only the second pipeline reads back */
}

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	t32o_log.printfs++;
	return 0;
}

u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a2; (void)a3;
	t32o_log.irq = a1;
	return 0;
}

u64 tisp_ccm_irq_frame_done(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

/* kernel div64_u64(u64 dividend, u64 divisor) */
u64 div64_u64(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u64 n = (u64)a1 << 32 | a0, d = (u64)a3 << 32 | a2;

	return d ? n / d : 0;
}

/* libgcc __ashrdi3(s64, int): renamed by objcopy (host libgcc has none) */
u64 t32o___ashrdi3(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	return (u64)((int64_t)((u64)a1 << 32 | a0) >> (a2 & 63));
}

/* memcpy: renamed by objcopy so the reference never reaches libc through a
 * truncated address */
u64 t32o_memcpy(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	memmove((void *)(uintptr_t)a0, (const void *)(uintptr_t)a1, a2);
	return a0;
}

/* ---- run a reference function on a stack below 4 GiB ---- */
static u8 stack_lo[1u << 20] __attribute__((aligned(64)));
static ucontext_t ctx_main, ctx_ref;
static t32o_fn call_fn;
static u32 call_a[5];
static u64 call_ret;

static void trampoline(void)
{
	call_ret = call_fn(call_a[0], call_a[1], call_a[2], call_a[3], call_a[4]);
}

u64 t32o_call(t32o_fn fn, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	call_fn = fn;
	call_a[0] = a0; call_a[1] = a1; call_a[2] = a2; call_a[3] = a3; call_a[4] = a4;
	getcontext(&ctx_ref);
	ctx_ref.uc_stack.ss_sp = stack_lo;
	ctx_ref.uc_stack.ss_size = sizeof(stack_lo);
	ctx_ref.uc_link = &ctx_main;
	makecontext(&ctx_ref, trampoline, 0);
	swapcontext(&ctx_main, &ctx_ref);
	return call_ret;
}
