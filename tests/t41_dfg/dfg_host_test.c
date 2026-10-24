/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host test of the translated T41 OEM defog chain (tx_isp_t41_dfg.h +
 * tx_isp_t41_dfg_gen.inc): runs tisp_defog_init on a real tuning bin
 * (parameter loading), feeds synthetic statistics through the statistics
 * IRQ + event 3 (tisp_defog_process) and the strength path, and requires
 * zero guard faults and the OEM register blocks.  Built by t41_dfg_host.sh.
 * DFG_BIN = tuning bin (default: the Cinnado sc5336 bin, test skipped if
 * it is not there).
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <ucontext.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
#define noinline __attribute__((noinline))
#define __aligned(x) __attribute__((aligned(x)))
#define unlikely(x) __builtin_expect(!!(x), 0)
#define KERN_WARNING ""
#define BIT(n) (1U << (n))
#define module_param(a, b, c)
#define module_param_named(a, b, c, d)
#define MODULE_PARM_DESC(a, b)
#define READ_ONCE(x) (*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile typeof(x) *)&(x) = (v))
#define EINVAL 22
#define EBUSY 16
#define EOPNOTSUPP 95
#define GFP_KERNEL 0
#define printk(...) (getenv("DFG_VERBOSE") ? (void)fprintf(stderr, __VA_ARGS__) : (void)0)
#define local_irq_save(f) ((f) = 0)
#define local_irq_restore(f) ((void)(f))
#define do_div(n, d) ({ u32 _r = (u32)((n) % (d)); (n) /= (d); _r; })
#define div64_u64(a, b) ((a) / (b))
struct completion; struct device;
static void private_complete(struct completion *c) { }

/* ---- 32-bit-addressable memory ---- */
static u8 *heap; static u32 heap_off;
static void *hmalloc(size_t n)
{
	void *p = heap + heap_off;
	heap_off += (n + 63) & ~63U;
	return p;
}
static void *private_kmalloc(size_t n, int f) { return memset(hmalloc(n), 0xa5, n); }
static void *private_vmalloc(size_t n) { return memset(hmalloc(n), 0xa5, n); }
static void private_kfree(const void *p) { }
static void private_vfree(const void *p) { }
static int private_dma_cache_sync(struct device *d, void *v, size_t n, int dir) { return 0; }

static u32 regs[0x10000];
static struct { u32 a, v; } rlog[100000]; static u32 nrlog;
static int system_reg_write(u32 a, u32 v)
{
	if (nrlog < 100000) { rlog[nrlog].a = a; rlog[nrlog].v = v; nrlog++; }
	regs[(a >> 2) & 0xffff] = v;
	return 0;
}
static u32 stat_phys0;
static int system_reg_read(u32 a)
{
	if (a == 0xc2b4) return stat_phys0;
	return regs[(a >> 2) & 0xffff];
}
static u32 irq_cb[96];
static int system_irq_func_set(int ch, int slot, int fn) { irq_cb[ch * 42 + slot] = fn; return 0; }
static int ev_cb[2][19];
static int tisp_event_set_cb(int ch, int ev, int fn) { ev_cb[ch][ev] = fn; return 0; }
static u32 nevent3, nevent6;
static long tisp_event_push(u32 ch, u32 ev) { return 0; }
static u8 tuning[1024] __aligned(16);
static u32 tisp_get_tuning(void) { return (u32)(uintptr_t)tuning; }
static unsigned char tparamsP_storage[8] __aligned(4);
static unsigned char tparams_day_storage[8] __aligned(4);
static unsigned char tparams_night_storage[8] __aligned(4);
static void *vae_x_tisp_event_push_unused;
static bool dfg_inited;
static long tisp_defog_init(u32 c, uintptr_t p) { return -1; }
static int tisp_defog_deinit(u32 c) { return 0; }
static int tisp_defog_wdr_en(u32 c, u32 e) { return 0; }

static u32 harness_ok(u32 a, u32 align) { return a >= 0x10000 && a < 0x80000000U && !(a & align); }
static u32 t41_vae_events_unused;
#include VAE_TOP
#include "rodata.h"
#include "tx_isp_t41_dfg.h"

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s (faults=%u addr=%#x)\n", __FILE__, __LINE__, #c, vae_faults, vae_fault_addr); return 1; } } while (0)

static u32 statword(u32 k, long n)
{
	/* smooth hazy-scene-like statistics: a gradient of two 12-bit fields */
	u32 lo = 600 + (k * 5 + n * 3) % 900, hi = 700 + (k * 3) % 1200;
	return (hi << 16) | lo;
}
static int frame(u8 *info, long n)
{
	u32 k;

	for (k = 0; k < 8192 / 4; k++)
		((u32 *)(uintptr_t)*(u32 *)(info + 4))[k] = statword(k, n);
	stat_phys0 = *(u32 *)(info + 8) + 2048 * (n % 4);
	t41_dfg_irq();
	return t41_dfg_process_cb(0);
}
static u32 hash8(const u8 *p, u32 n)
{
	u32 h = 7;

	while (n--) h = h * 33 + *p++;
	return h;
}
static u32 wdigest(u32 from)
{
	u32 h = 0, i;

	for (i = from; i < nrlog; i++)
		h = h * 1000003u + rlog[i].a * 31 + rlog[i].v;
	return h;
}
static int run(int argc, char **argv)
{
	const char *path = getenv("DFG_BIN") ? getenv("DFG_BIN") : argv[1];
	FILE *f = path ? fopen(path, "rb") : NULL;
	u8 *bin, *info;
	long n;
	u32 *par, i, nw, dig[3];
	u64 r;

	if (!f) { printf("t41_dfg_host: SKIP (no tuning bin)\n"); return 0; }
	heap = mmap(NULL, 16 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	CHECK(heap != MAP_FAILED);
	t41_defog = 1;	/* the driver default is 0 (bypass), the chain is what is tested */
	par = hmalloc(8); par[0] = 2560; par[1] = 1440;
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	bin = hmalloc(n); CHECK(fread(bin, 1, n, f) == (size_t)n);
	*(u32 *)tparamsP_storage = (u32)(uintptr_t)bin;
	*(u32 *)tparams_day_storage = (u32)(uintptr_t)bin + 0x40;
	*(u32 *)tparams_night_storage = (u32)(uintptr_t)bin + 0x40;

	/* parameter loading */
	CHECK(t41_dfg_init(0, (uintptr_t)par) == 0);
	CHECK(vae_faults == 0);
	info = (u8 *)(uintptr_t)dfg_bss[60];
	CHECK(info && *(u32 *)(info + 4) && *(u32 *)(info + 8) == *(u32 *)(info + 4) + 0x80000000U);
	CHECK(*(u32 *)(info + 12) == (u32)(uintptr_t)bin + 65536 + 15052);	/* defog block of the bin */
	CHECK(irq_cb[10] != 0 && ev_cb[0][3] == (int)(uintptr_t)t41_dfg_process_cb);
	{ int w = 0; for (i = 0; i < nrlog; i++) w += rlog[i].a >= 0xc000 && rlog[i].a < 0xc300; CHECK(w >= 8); }
	printf("t41_dfg_host: init ok, %u reg writes, ev=%u\n", nrlog, *(u32 *)(info + 24));

	/* statistics IRQ -> event 3 -> process, several frames of varying stats */
	for (n = 0; n < 12; n++) {
		nw = nrlog;
		CHECK(frame(info, n) == 0);
		CHECK(nrlog > nw);	/* per-frame register update (para3) */
		if (n == 4) { u32 sc[16]; vae_tisp_defog_ev_update(0, 0, 3 << 10, 0, (u32)(uintptr_t)sc); }
	}
	CHECK(vae_faults == 0);
	/* strength 0 / 128 / 255 through the tuning attributes: same statistics,
	 * different per-frame register digests */
	for (i = 0; i < 3; i++) {
		u32 ratios[3] = { 0, 128, 255 }, k;
		*(u32 *)(tuning + 216) = 1; tuning[220] = ratios[i];
		CHECK(t41_dfg_ratio_apply() >= 0);
		for (k = 0; k < 3; k++) { nw = nrlog; CHECK(frame(info, 100) == 0); }
		dig[i] = wdigest(nw) * 31 + hash8((u8 *)(uintptr_t)*(u32 *)(info + 12) + 644, 108);
		if (getenv("DFG_VERBOSE")) { u8 *pp = (u8 *)(uintptr_t)*(u32 *)(info + 12); u32 q; fprintf(stderr, "info:"); for (q = 0; q < 14; q++) fprintf(stderr, " %u", *(u32 *)(info + q * 4)); fprintf(stderr, "\n arrs:"); for (q = 680; q < 725; q++) fprintf(stderr, " %u", pp[q]); fprintf(stderr, "\n"); }
		if (getenv("DFG_VERBOSE")) { u32 q; { u32 q2, la = 0, cnt = 0; for (q2 = nw; q2 <= nrlog; q2++) { if (q2 == nrlog || rlog[q2].a != la) { if (cnt) fprintf(stderr, " [%x x%u]", la, cnt); la = q2 < nrlog ? rlog[q2].a : 0; cnt = 0; } cnt++; } fprintf(stderr, "\n"); } fprintf(stderr, "strength %u: %u writes:", ratios[i], nrlog - nw); for (q = nw; q < nrlog && q < nw + 12; q++) fprintf(stderr, " %x=%x", rlog[q].a, rlog[q].v); fprintf(stderr, "\n"); }
	}
	printf("t41_dfg_host: strength digests %08x %08x %08x\n", dig[0], dig[1], dig[2]);
	/* the strength reaches the OEM trans_k tables (params+644..752) */
	CHECK(dig[0] != dig[2] && dig[0] != dig[1] && vae_faults == 0);
	CHECK(t41_dfg_deinit(0) == 0 && vae_faults == 0);
	printf("t41_dfg_host: OK (init, 12 frames, ev update, 3 strengths, deinit; 0 faults, %u reg writes)\n", nrlog);
	return 0;
}

static ucontext_t main_ctx, run_ctx;
static int g_argc, g_rc; static char **g_argv;
static void tramp(void) { g_rc = run(g_argc, g_argv); }
int main(int argc, char **argv)
{
	size_t sz = 1 << 20;
	void *stk = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

	g_argc = argc; g_argv = argv;
	getcontext(&run_ctx);
	run_ctx.uc_stack.ss_sp = stk; run_ctx.uc_stack.ss_size = sz; run_ctx.uc_link = &main_ctx;
	makecontext(&run_ctx, tramp, 0);
	swapcontext(&main_ctx, &run_ctx);
	return g_rc;
}
