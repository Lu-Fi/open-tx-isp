/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host run of the generated T40 vendor AWB (tx_isp_t40_vawb.h top part + tx_isp_t40_vawb_gen.inc):
 * OEM init from a real tuning bin (day bank), then N frames of synthetic AWB statistics
 * (every zone the same R/G/B), prints the gains (ctx[9]), CT (*ctx[7]) and the register writes.
 * 64-bit non-PIE build, data below 4 GB (.vdata at 0x30000000), MAP_32BIT stack.
 *   vawb_host <tuning.bin> <inter_seed.bin> <R> <G> <B> [frames] [trace]
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <ucontext.h>
#include <sys/mman.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
#define noinline __attribute__((noinline))
#define __aligned(x) __attribute__((aligned(x)))
#define unlikely(x) __builtin_expect(!!(x), 0)
#define KERN_WARNING ""
#define KERN_INFO ""
#define BIT(n) (1U << (n))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define PAGE_SIZE 4096
#define THREAD_SIZE 8192
#define module_param(a, b, c)
#define module_param_named(a, b, c, d)
#define module_param_cb(a, b, c, d)
#define MODULE_PARM_DESC(a, b)
#define scnprintf snprintf
#define printk(...) (harness_verbose ? printf(__VA_ARGS__) : 0)
#define div64_u64(a, b) ((a) / (b))
struct kernel_param { int x; };
struct kernel_param_ops { int (*get)(char *, const struct kernel_param *); };
static int harness_verbose = 1;
#define VD __attribute__((section(".vdata")))
static u32 pstMainAwbOri[128] VD, pstSecAwbOri[128] VD, pstMainAwbCtDetectOri[34] VD;
static u32 stMainAwbInter VD, stMainAwbOuter VD, stSecAwbInter VD, stSecAwbOuter VD;
static u8 pstMainAeOri_storage[0x164] VD, pstSecAeOri_storage[0x164] VD;
static u32 tparamsN[4] VD, awb_algo_comp[64] VD;
static u8 tispinfo[64] VD;
static u8 inter[0x10000] __aligned(16) VD, outer[0x2000] __aligned(16) VD, bank[0x19000] __aligned(16) VD;
static u8 stats[0x10000] __aligned(16) VD;
static u32 nreg;
static u32 reg4004, reg4008;
static void private_complete(u32 c) { }
static int system_reg_write(u32 a, u32 v)
{
	nreg++;
	if (a == 0x4004) reg4004 = v;
	if (a == 0x4008) reg4008 = v;
	return 0;
}
static int system_reg_read(u32 a) { return 0; }
static int tisp_event_push(u32 a, u32 b) { return 0; }
static int tisp_awb_main_interrupt_static(void) { return 0; }
static long tisp_awb_main_process(void) { return 0; }
static u8 *stk_lo;
static u32 harness_ok(u32 a, u32 align)
{
	if (a & align) return 0;
	if (a >= (u32)(uintptr_t)stk_lo && a < (u32)(uintptr_t)stk_lo + 0x40000) return 1;
	return a >= 0x30000000U && a < 0x30400000U;
}
#include VAWB_TOP
#include VAWB_INC

#define VAWB_CSP ((u32)(uintptr_t)vawb_argarea)
#define VAWB_TOP() (vawb_loop_n = 0)
static ucontext_t mainctx, runctx;
static int g_frames = 50, g_trace;
static u32 g_r, g_g, g_b;
static void run(void)
{
	u32 *c = pstMainAwbOri, f, i;

	vawb_tisp_awb_params_transmit((u32)(uintptr_t)c, 0, 0, 0, VAWB_CSP);
	*(u16 *)(uintptr_t)c[49] = 3840; *(u16 *)((uintptr_t)c[49] + 2) = 2160;
	vawb_tisp_awb_params_refresh((u32)(uintptr_t)c, 0, tparamsN[0] + 4288, 0, VAWB_CSP);
	vawb_tisp_awb_set_hardware_param((u32)(uintptr_t)c, 0, 0, 0, VAWB_CSP);
	vawb_tisp_awb_set_gain((u32)(uintptr_t)c, 0, 0, 0, VAWB_CSP);
	printf("init faults=%u ct=%u g9=%u,%u reg=%#x,%#x frz=%u\n", vawb_faults, *(u32 *)(uintptr_t)c[7],
	       ((u32 *)(uintptr_t)c[9])[0], ((u32 *)(uintptr_t)c[9])[1], reg4004, reg4008, *(u8 *)(uintptr_t)c[119]);
	vawb_tisp_awb_main_ev_update(3000U << 10, 0, 0, 0, VAWB_CSP);
	for (i = 0; i < sizeof(stats) / 16; i++) {	/* 5 x 22-bit fields per 16-byte zone record */
		u32 *w = (u32 *)(stats + i * 16), n = 200;
		w[0] = (g_r & 0x3fffff) | (g_g << 22);
		w[1] = ((g_g >> 10) & 0xfff) | (g_b << 12);
		w[2] = ((g_b >> 20) & 3) | (0 << 2) | (n << 24);
		w[3] = (n >> 8) & 0x3fff;
	}
	for (f = 0; f < (u32)g_frames; f++) {
		VAWB_TOP();
		vawb_tisp_awb_get_statistics((u32)(uintptr_t)stats, (u32)(uintptr_t)c, 0, 0, VAWB_CSP);
		if (g_trace && f == 0) { vawb_tn = 0; vawb_tarmed = 1; }
		VAWB_TOP();
		vawb_tisp_awb_main_process(0, 0, 0, 0, VAWB_CSP);
		vawb_tarmed = 0;
		if (f < 2) {
			u16 *s123 = (u16 *)(uintptr_t)c[123]; u16 *s4 = (u16 *)(uintptr_t)c[4];
			printf("  c123=%u,%u,%u,%u,%u,%u c100=%u c103=%u c118=%u c120=%u c4=%u,%u,%u,%u\n", s123[0], s123[1], s123[2], s123[3], s123[4], s123[5],
			       *(u8 *)(uintptr_t)c[100], *(u8 *)(uintptr_t)c[103], *(u8 *)(uintptr_t)c[118], *(u8 *)(uintptr_t)c[120], s4[0], s4[1], s4[2], s4[3]);
		}
		if (f < 3 || f % 10 == 9)
			printf("f%u faults=%u loops=%u ct=%u g9=%u,%u reg=%#x,%#x\n", f, vawb_faults, vawb_loop_trips,
			       *(u32 *)(uintptr_t)c[7], ((u32 *)(uintptr_t)c[9])[0], ((u32 *)(uintptr_t)c[9])[1], reg4004, reg4008);
	}
	if (g_trace) { printf("trace:"); for (i = 0; i < vawb_tn; i++) printf(" %x", vawb_tbuf[i]); printf("\n"); }
}

#define VAWB_ONE (64 + 60328 + 4752 + 512 + 136 + 356)
/* replay of a device dump (/proc/t40_vawb_dump) */
static u8 *g_dump;
static void replay(void)
{
	u32 *c = pstMainAwbOri, *h = (u32 *)g_dump, i, d_in = 0, d_out = 0, first = ~0U;
	u8 *d = g_dump + 64;
	u32 *dctx = (u32 *)(d + 60328 + 4752);

	vawb_tisp_awb_params_transmit((u32)(uintptr_t)c, 0, 0, 0, VAWB_CSP);
	for (i = 0; i < 128; i++) {	/* same layout: device ctx[i] - dev base == host ctx[i] - host base */
		u32 dv = dctx[i], hv = c[i], ok;
		if (dv >= h[1] && dv < h[1] + 60328) ok = hv - (u32)(uintptr_t)inter == dv - h[1];
		else if (dv >= h[2] && dv < h[2] + 4752) ok = hv - (u32)(uintptr_t)outer == dv - h[2];
		else ok = dv == 0 && hv == 0;
		if (!ok) printf("ctx[%u] dev %#x host %#x\n", i, dv, hv);
	}
	memcpy(inter, d, 60328); memcpy(outer, d + 60328, 4752);
	vawb_tn = 0; vawb_tarmed = g_trace;
	VAWB_TOP();
	vawb_tisp_awb_main_process(0, 0, 0, 0, VAWB_CSP);
	vawb_tarmed = 0;
	for (i = 0; i < 60328; i++) if (inter[i] != g_dump[VAWB_ONE + i]) { d_in++; if (first == ~0U) first = i; }
	for (i = 0; i < 4752; i++) if (outer[i] != g_dump[VAWB_ONE + 60328 + i]) d_out++;
	printf("replay frame %u: faults=%u loops=%u ct=%u g9=%u,%u reg=%#x,%#x  diff vs device: inter %u B (first %#x) outer %u B\n",
	       h[5], vawb_faults, vawb_loop_trips, *(u32 *)(uintptr_t)c[7], ((u32 *)(uintptr_t)c[9])[0],
	       ((u32 *)(uintptr_t)c[9])[1], reg4004, reg4008, d_in, first, d_out);
	if (g_trace) { printf("trace:"); for (i = 0; i < vawb_tn; i++) printf(" %x", vawb_tbuf[i]); printf("\n"); }
}
static void load(const char *fn, u8 *dst, long off, u32 n)
{
	FILE *f = fopen(fn, "rb");

	if (!f || fseek(f, off, SEEK_SET) || fread(dst, 1, n, f) != n) { printf("read %s failed\n", fn); exit(2); }
	fclose(f);
}

int main(int argc, char **argv)
{
	if (argc >= 3 && !strcmp(argv[1], "replay")) {
		FILE *f = fopen(argv[2], "rb");
		static u8 buf[VAWB_ONE + 60328 + 4752];

		if (!f || fread(buf, 1, sizeof(buf), f) != sizeof(buf)) { printf("dump short\n"); return 2; }
		g_dump = buf; g_trace = argc > 3;
		stMainAwbInter = (u32)(uintptr_t)inter; stMainAwbOuter = (u32)(uintptr_t)outer;
		memcpy(vawb_wide_tele_para, vawb_wide_tele_para_t8416, 144); vawb_wide_tele_para[0] = 0;
		stk_lo = mmap(NULL, 0x40000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
		getcontext(&runctx);
		runctx.uc_stack.ss_sp = stk_lo; runctx.uc_stack.ss_size = 0x40000; runctx.uc_link = &mainctx;
		makecontext(&runctx, replay, 0);
		swapcontext(&mainctx, &runctx);
		return 0;
	}
	if (argc < 6) return 2;
	load(argv[1], bank, 16, 0x18ff4);		/* day bank */
	load(argv[2], inter, 0x1d36c, 0xeba8);		/* OEM .data stMainAwbInterOri */
	g_r = strtoul(argv[3], 0, 0); g_g = strtoul(argv[4], 0, 0); g_b = strtoul(argv[5], 0, 0);
	if (argc > 6) g_frames = atoi(argv[6]);
	if (argc > 7) g_trace = atoi(argv[7]);
	stMainAwbInter = (u32)(uintptr_t)inter; stMainAwbOuter = (u32)(uintptr_t)outer;
	tparamsN[0] = (u32)(uintptr_t)bank;
	memcpy(vawb_wide_tele_para, vawb_wide_tele_para_t8416, 144); vawb_wide_tele_para[0] = 0;
	stk_lo = mmap(NULL, 0x40000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	getcontext(&runctx);
	runctx.uc_stack.ss_sp = stk_lo; runctx.uc_stack.ss_size = 0x40000; runctx.uc_link = &mainctx;
	makecontext(&runctx, run, 0);
	swapcontext(&mainctx, &runctx);
	return 0;
}
