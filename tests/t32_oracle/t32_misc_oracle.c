// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the small helpers of driver/t32/tx_isp_t32_misc.h against
 * Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc): tisp_max,
 * tisp_min, tisp_simple_intp, tisp_simple_intp_uint32, dfg_array_cmp,
 * isStringAssigned, lsc_exchange_data, lsc_exchange_line, u3bw and the
 * constant hooks.
 *
 * Compared: return values (all 64 bits for min / max), every byte of the
 * tables / buffers the vendor writes plus canaries around them.  u3bw runs
 * on a buffer mapped at 0x8xxxxxxx so that its address quirk is visible.
 * Inputs: random values with forced ties, equal neighbours, idx 9 / 10 / 11,
 * big products, n <= 0.  Exit 0 = all equal, 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_misc.h"

u64 tisp_max(u32, u32, u32, u32);
u64 tisp_min(u32, u32, u32, u32);
u64 tisp_simple_intp(u32, u32, u32, u32);
u64 tisp_simple_intp_uint32(u32, u32, u32, u32);
u64 dfg_array_cmp(u32, u32, u32, u32);
u64 isStringAssigned(u32, u32, u32, u32);
u64 lsc_exchange_data(u32, u32, u32, u32);
u64 lsc_exchange_line(u32, u32, u32, u32, u32);
u64 u3bw(u32, u32, u32, u32);
int32_t tisp_bin_data_process(void);
int32_t tisp_channel_main_stop(void);
int32_t tisp_channel_main_fifo_clear(void);
int32_t tisp_rdma_fra_down_interrupt(void);
int32_t tisp_wdma_fra_down_interrupt(void);
int32_t add_sensor_chose(void);
u64 tisp_process_init(u32, u32, u32, u32);
u64 release_sensor_setting(u32, u32, u32, u32);

#define CALL(f, a, b, c, d, e) t32o_call((t32o_fn)(void (*)(void))(f), a, b, c, d, e)

static u32 rs = 0x5eed0808u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

static void bad(const char *what, u64 v, u64 o)
{
	if (failed++ < 8)
		printf("MISMATCH %s: vendor %016llx ours %016llx\n", what, (unsigned long long)v,
		       (unsigned long long)o);
}

static void chk(const char *what, u64 v, u64 o)
{
	compared++;
	if (v != o)
		bad(what, v, o);
}

static u32 pick(void)
{
	static const u32 edge[] = { 0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe, 0x10000, 0xffff };

	return (rnd() & 3) ? rnd() : edge[rnd() & 7];
}

static void minmax(void)
{
	u32 al = pick(), ah = pick(), bl = pick(), bh = pick();
	s64 a, b;

	switch (rnd() & 3) {
	case 0: bh = ah; break;		/* same high word: the low word decides */
	case 1: bh = ah; bl = al; break;	/* tie */
	default: break;
	}
	a = (s64)((u64)ah << 32 | al);
	b = (s64)((u64)bh << 32 | bl);
	chk("tisp_max", CALL(tisp_max, al, ah, bl, bh, 0), (u64)t32_tisp_max(a, b));
	chk("tisp_min", CALL(tisp_min, al, ah, bl, bh, 0), (u64)t32_tisp_min(a, b));
}

static void intp(void)
{
	u32 *t = t32o_alloc(64), i, idx, frac;

	for (i = 0; i < 11; i++)
		t[i] = (rnd() & 1) ? pick() : (rnd() & 0xfff);
	if (!(rnd() & 3))
		t[(rnd() % 10) + 1] = t[rnd() % 11];
	idx = (rnd() & 1) ? rnd() % 12 : ((rnd() & 1) ? rnd() : 9 + (rnd() % 3));
	if (idx < 10 && !(rnd() & 7))
		t[idx + 1] = t[idx];
	frac = (rnd() & 1) ? (rnd() & 0x1ffff) : pick();
	chk("tisp_simple_intp", (u32)CALL(tisp_simple_intp, idx, frac, T32O_PTR(t), 0, 0),
	    (u32)t32_simple_intp(idx, frac, (const s32 *)t));
	chk("tisp_simple_intp_uint32", (u32)CALL(tisp_simple_intp_uint32, idx, frac, T32O_PTR(t), 0, 0),
	    t32_simple_intp_u32(idx, frac, t));
}

static void cmp_str(void)
{
	u8 *a = t32o_alloc(64), *b = t32o_alloc(64);
	s32 n = (rnd() & 7) ? (s32)(rnd() % 40) : -(s32)(rnd() % 3);
	u32 i;

	for (i = 0; i < 64; i++)
		a[i] = b[i] = (u8)rnd();
	if (rnd() & 1)
		b[rnd() % 40] ^= (rnd() & 1) ? 0x80 : (u8)(1 + rnd() % 255);
	chk("dfg_array_cmp", (u32)CALL(dfg_array_cmp, T32O_PTR(a), T32O_PTR(b), (u32)n, 0, 0),
	    (u32)t32_dfg_array_cmp(a, b, n));
	a[0] = (rnd() & 1) ? 0 : (u8)rnd();
	chk("isStringAssigned", (u32)CALL(isStringAssigned, T32O_PTR(a), 0, 0, 0, 0),
	    (u32)t32_is_string_assigned((const char *)a));
	chk("isStringAssigned(NULL)", (u32)CALL(isStringAssigned, 0, 0, 0, 0, 0), (u32)t32_is_string_assigned(NULL));
}

static void lsc(void)
{
	static u8 mine[256], tmo[64];
	u8 *v = t32o_alloc(256), *tmv = t32o_alloc(64);
	u32 i, x = rnd() % 64, y = rnd() % 64, k, bytes;

	for (i = 0; i < 256; i++)
		v[i] = mine[i] = (u8)rnd();
	CALL(lsc_exchange_data, T32O_PTR(v), x, y, 0, 0);
	t32_lsc_exchange_data((u16 *)mine, x, y);
	compared++;
	if (memcmp(v, mine, 256))
		bad("lsc_exchange_data", x, y);
	/* rows: entries i, j, bytes <= 64, rows may overlap (memmove) */
	x = rnd() % 32; y = rnd() % 32; bytes = 2 * (rnd() % 33);
	for (k = 0; k < 64; k++)
		tmv[k] = tmo[k] = (u8)rnd();
	CALL(lsc_exchange_line, T32O_PTR(v), T32O_PTR(tmv), x, y, bytes);
	t32_lsc_exchange_line((u16 *)mine, tmo, x, y, bytes);
	compared++;
	if (memcmp(v, mine, 256) || memcmp(tmv, tmo, 64))
		bad("lsc_exchange_line", x << 16 | y, bytes);
}

static u8 *hi_page;
static void unpack(void)
{
	static u32 mine[64];
	u32 *vo = t32o_alloc(256), i, bytes = (rnd() & 3) ? 12 * (rnd() % 11) : rnd() % 121;
	u8 *in = hi_page + 4 * (rnd() % 64);	/* word aligned, as the vendor lw needs */

	for (i = 0; i < 132; i++)
		in[i] = (u8)rnd();
	for (i = 0; i < 64; i++)
		vo[i] = mine[i] = rnd();
	CALL(u3bw, T32O_PTR(vo), T32O_PTR(in), bytes, 0, 0);
	t32_u3bw(mine, in, bytes);
	compared++;
	if (memcmp(vo, mine, 256))
		bad("u3bw", bytes, T32O_PTR(in));
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;

	hi_page = mmap((void *)(uintptr_t)0x8a5c0000u, 1u << 16, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (hi_page == MAP_FAILED || T32O_PTR(hi_page) != 0x8a5c0000u) {
		printf("t32_misc_oracle: cannot map the high test page\n");
		return 1;
	}
	for (c = 0; c < n; c++) {
		t32o_reset();
		minmax();
		intp();
		cmp_str();
		lsc();
		unpack();
	}
	chk("tisp_bin_data_process", (u32)tisp_bin_data_process(), (u32)t32_tisp_bin_data_process());
	chk("tisp_channel_main_stop", (u32)tisp_channel_main_stop(), (u32)t32_tisp_channel_main_stop());
	chk("tisp_channel_main_fifo_clear", (u32)tisp_channel_main_fifo_clear(),
	    (u32)t32_tisp_channel_main_fifo_clear());
	chk("tisp_rdma_fra_down_interrupt", (u32)tisp_rdma_fra_down_interrupt(),
	    (u32)t32_tisp_rdma_fra_down_interrupt());
	chk("tisp_wdma_fra_down_interrupt", (u32)tisp_wdma_fra_down_interrupt(),
	    (u32)t32_tisp_wdma_fra_down_interrupt());
	chk("add_sensor_chose", (u32)add_sensor_chose(), (u32)t32_add_sensor_chose());
	chk("tisp_process_init", (u32)CALL(tisp_process_init, 0, 0, 0, 0, 0), (u32)t32_tisp_process_init());
	chk("release_sensor_setting", (u32)CALL(release_sensor_setting, 0, 0, 0, 0, 0),
	    (u32)t32_release_sensor_setting());
	printf("t32_misc_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
