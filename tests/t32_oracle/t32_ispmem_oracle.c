// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable reserved-memory allocator (driver/t32/tx_isp_t32_ispmem.h) against
 * the vendor find_new_buffer, isp_mem_init, isp_malloc_buffer and isp_free_buffer from Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 * Each case builds a random allocator state (random filler, some slots leaked, init, 0..30 random
 * mallocs / frees run by ours) twice and runs one function on each side; compared: return value,
 * the call log (get_priv_mem, mutex init, isp_printf lines and arguments) and every byte of the
 * pool block.  private_mutex_lock / _unlock are env.c no-ops (not observable).
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispmem.h"

u64 find_new_buffer(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_mem_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_malloc_buffer(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_free_buffer(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_mutex_lock(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_mutex_unlock(u32 a0, u32 a1, u32 a2, u32 a3);
extern u8 t32o_op5_mem[0x1a0], t32_small_bss[64];
extern u32 t32_data_d9850[8], t32_data_c521c[1];

#define MAXL	16
static u32 rs = 0x3e3a110cu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
struct lg { unsigned int n; u32 e[MAXL][6]; };
static struct lg lg, lg_ours;
static int quiet;
static u32 pm_base, pm_size;
static void L(u32 id, u32 a, u32 b, u32 c, u32 d, u32 e)
{
	if (quiet)
		return;
	if (lg.n < MAXL) {
		u32 *p = lg.e[lg.n];

		p[0] = id; p[1] = a; p[2] = b; p[3] = c; p[4] = d; p[5] = e;
	}
	lg.n++;
}
u64 private_get_isp_priv_mem(u32 bp, u32 sp, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	L(1, bp, sp, 0, 0, 0);
	memcpy(T32O_HOST(bp), &pm_base, 4);
	memcpy(T32O_HOST(sp), &pm_size, 4);
	return 0;
}
u64 private_raw_mutex_init(u32 m, u32 name, u32 key, u32 a3);
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	(void)a1; (void)a2;
	L(2, a0, a3, a4, a5, a3 == 66 ? a6 : 0);
	return 0;
}

static void o_pm(u32 b, u32 s) { private_get_isp_priv_mem(b, s, 0, 0); }
static void o_mi(u32 m) { private_raw_mutex_init(m, T32O_PTR(t32_data_c521c), T32O_PTR(t32_small_bss), 0); }
static void o_lock(u32 m) { private_mutex_lock(m, 0, 0, 0); }
static void o_unlock(u32 m) { private_mutex_unlock(m, 0, 0, 0); }
static void o_log(u32 line, u32 a4, u32 a5, u32 a6) { isp_printf(0, 0, 0, line, a4, a5, a6); }
static const struct t32_mem_ops ops = { o_pm, o_mi, o_lock, o_unlock, o_log };
static const struct t32_mem_glob g = { 0, 0 };
static struct t32_mem_glob gg;

static u32 addrs[32];
static unsigned int naddr;
static u8 snap[sizeof(t32o_op5_mem)];

static u32 rsize(void)
{
	u32 k = rnd() % 6;

	return k == 0 ? 0 : k == 1 ? (rnd() % 8) * T32_MEM_PAGE : k == 2 ? 1 + rnd() % 0x1000 : k == 3 ? rnd() % 0x40000 : rnd() % 0x400000;
}

static void build(u32 seed)
{
	u32 save = rs, i, n;

	rs = seed | 1;
	for (i = 0; i < sizeof(t32o_op5_mem); i++)
		t32o_op5_mem[i] = rnd();
	for (i = 0; i < T32_MEM_NSLOTS; i++)
		t32_mb_w8(gg.pool + T32_MEM_SLOTS + i * T32_MEM_SLOT_SIZE, T32_MB_TAKEN, rnd() % 10 == 0);
	t32_mb_w8(gg.pool + T32_MEM_SLOTS + (rnd() % 20) * T32_MEM_SLOT_SIZE, T32_MB_TAKEN, 0);
	pm_base = rnd() % 10 == 0 ? 0 : (rnd() & 0x7ffff000u) | (rnd() % 3 ? 0 : rnd() & 0xfff);
	pm_size = rnd() % 0x2000000;
	quiet = 1;
	naddr = 0;
	t32_isp_mem_init(&ops, &gg);
	n = rnd() % 31;
	for (i = 0; i < n; i++) {
		if (rnd() % 3 && naddr < 32) {
			u32 a = t32_isp_malloc_buffer(&ops, &gg, rsize());

			if (a)
				addrs[naddr++] = a;
		} else if (naddr) {
			u32 k = rnd() % naddr;

			t32_isp_free_buffer(&ops, &gg, addrs[k]);
			addrs[k] = addrs[--naddr];
		}
	}
	quiet = 0;
	memset(&lg, 0, sizeof(lg));
	rs = save;
}

#define REF(fn, a0)	((u32)t32o_call((t32o_fn)(void (*)(void))(fn), (a0), rnd(), rnd(), rnd(), 0))

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	(void)g;
	gg.pool = T32O_PTR(t32o_op5_mem);
	gg.mutex = T32O_PTR(t32_data_d9850);
	for (it = 0; it < 30000; it++) {
		u32 seed = rnd(), k = it % 4, a = 0, r, rr;

		build(seed);
		if (k == 2)
			a = rsize();
		if (k == 3)
			a = naddr && rnd() % 4 ? addrs[rnd() % naddr] : rnd() % 3 ? pm_base + (rnd() % 64) * T32_MEM_PAGE : rnd();
		if (k == 1) {	/* all slots taken: the vendor init writes through NULL (vendor UB, not run) */
			unsigned int i, fr = 0;

			for (i = 0; i < T32_MEM_NSLOTS; i++)
				fr += !t32_mb_r8(gg.pool + T32_MEM_SLOTS + i * T32_MEM_SLOT_SIZE, T32_MB_TAKEN);
			if (!fr) {
				compared++;
				continue;
			}
		}
		switch (k) {
		case 0: r = t32_find_new_buffer(&gg); break;
		case 1: r = t32_isp_mem_init(&ops, &gg); break;
		case 2: r = t32_isp_malloc_buffer(&ops, &gg, a); break;
		default: t32_isp_free_buffer(&ops, &gg, a); r = 0; break;
		}
		lg_ours = lg;
		memcpy(snap, t32o_op5_mem, sizeof(snap));
		build(seed);
		{
			static u64 (*const f[4])(u32, u32, u32, u32) = { find_new_buffer, isp_mem_init, isp_malloc_buffer, isp_free_buffer };

			rr = REF(f[k], a);
		}
		if (k == 3)
			rr = 0;
		compared++;
		if (r != rr || lg.n != lg_ours.n || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, t32o_op5_mem, sizeof(snap))) {
			if (failed++ < 5)
				printf("t32_ispmem_oracle: MISMATCH fn %u seed %08x: %08x/%08x log %u/%u\n", k, seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispmem_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
