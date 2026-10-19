/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host harness for the generated T41 vendor AE (tx_isp_t41_vae.h top part +
 * tx_isp_t41_vae_gen.inc).  Built twice by t41_vae_diff.sh (reference and
 * new generator/guard output), run on identical pseudo-random driver state;
 * the per-step digests of all AE-visible memory, return values, fault
 * counters and register writes must be identical.
 *
 * Needs a 64-bit non-PIE host build whose data lives below 4 GB (the OEM
 * code works on 32-bit addresses); the AE runs on an mmap(MAP_32BIT) stack.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdbool.h>
#include <ucontext.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/mman.h>

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
#define noinline __attribute__((noinline))
#define __aligned(x) __attribute__((aligned(x)))
#define unlikely(x) __builtin_expect(!!(x), 0)
#define KERN_WARNING ""
#define BIT(n) (1U << (n))
#define module_param(a, b, c)
#define MODULE_PARM_DESC(a, b)
#define printk(...) (harness_printk++)
#define local_irq_save(f) ((f) = 0x55)
#define local_irq_restore(f) (harness_irq += (f))
#define do_div(n, d) ({ u32 _r = (u32)((n) % (d)); (n) /= (d); _r; })
#define div64_u64(a, b) ((a) / (b))
struct completion;
static unsigned long harness_printk, harness_irq;
static u64 harness_reglog[2];
static u32 harness_nreg, harness_ncomp, harness_nevt;
static void private_complete(struct completion *c) { harness_ncomp++; }
static int system_reg_write(u32 a, u32 v)
{
	harness_reglog[0] = harness_reglog[0] * 1000003u + a;
	harness_reglog[1] = harness_reglog[1] * 1000003u + v;
	harness_nreg++;
	return 0;
}
static int tisp_event_push(u32 a, u32 b) { harness_nevt++; return 0; }
static u32 harness_ok(u32 a, u32 align);
static u64 harness_dispatch(u32 tg, u32 a0, u32 a1, u32 a2, u32 a3, u32 sp);
static void *harness_bad_call;

#include VAE_TOP

/* ---- driver objects the generated code names ---- */
#define VD __attribute__((section(".vdata")))
#define ARENA(n) static u8 n[0x10000] __aligned(16) VD
#define NARENA 14
ARENA(ae_info); ARENA(ae_cache); ARENA(ae_cache_init); ARENA(sensor_ctrl_main_storage);
ARENA(g_ispcore); ARENA(y_arr_storage); ARENA(fliker_info); ARENA(fliker_para);
ARENA(fliker_ae_stable); ARENA(blc_info); ARENA(gib_info); ARENA(ae_algo_comp_storage);
ARENA(ae_api_tmp_storage); ARENA(tisp_ae_hist_last); ARENA(clm_info); ARENA(tisp_par_info_storage);
static u8 *const arenas[] = { ae_info, ae_cache, ae_cache_init, sensor_ctrl_main_storage, g_ispcore,
	y_arr_storage, fliker_info, fliker_para, fliker_ae_stable, blc_info, gib_info,
	ae_algo_comp_storage, ae_api_tmp_storage, tisp_ae_hist_last, clm_info, tisp_par_info_storage };
#define NAR (sizeof(arenas) / sizeof(arenas[0]))

#include VAE_INC

#define STK (256 * 1024)
static u8 *stk_lo;
static u8 *const hdrs[] = { (u8 *)vae_tisp_ae_hist, (u8 *)vae_ta_custom_en, (u8 *)vae_ae_ceguang,
	(u8 *)vae_slock };
static const u32 hdrsz[] = { sizeof(vae_tisp_ae_hist), sizeof(vae_ta_custom_en), sizeof(vae_ae_ceguang),
	sizeof(vae_slock) };

static jmp_buf budget_jb, alarm_jb;
static void on_alarm(int sig) { longjmp(alarm_jb, 1); }
static unsigned long budget;

static u32 harness_ok(u32 a, u32 align)
{
	unsigned i;

	if (a >= (u32)(uintptr_t)stk_lo && a < (u32)(uintptr_t)stk_lo + STK) return !(a & align);
	/* runaway loop on garbage state: cut at the same logical access in both builds */
	if (++budget > 400000) longjmp(budget_jb, 1);
	if (a & align) return 0;
	for (i = 0; i < NAR; i++) if (a >= (u32)(uintptr_t)arenas[i] && a < (u32)(uintptr_t)arenas[i] + 0x10000) return 1;
	for (i = 0; i < 4; i++) if (a >= (u32)(uintptr_t)hdrs[i] && a < (u32)(uintptr_t)hdrs[i] + hdrsz[i]) return 1;
	if (a >= (u32)(uintptr_t)vae_rodata && a < (u32)(uintptr_t)vae_rodata + sizeof(vae_rodata)) return 1;
	if (a >= (u32)(uintptr_t)stk_lo && a < (u32)(uintptr_t)stk_lo + STK) return 1;
	return 0;
}

#define FN(x) { (uintptr_t)vae_##x, #x }
static const struct { uintptr_t a; const char *n; } fns[] = {
#define F(x) FN(x),
#include "vae_fns.h"
#undef F
};
static u64 harness_dispatch(u32 tg, u32 a0, u32 a1, u32 a2, u32 a3, u32 sp)
{
	unsigned i;

	for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++)
		if (fns[i].a == tg)
			return ((vae_fn_t)(uintptr_t)tg)(a0, a1, a2, a3, sp);
	vae_fault_note(tg, 0xbad);
	return 0;
}

/* ---- deterministic state ---- */
static u64 rs;
static u32 rnd(void) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (u32)(rs >> 33); }
static u32 pptr, pzero, pbits;  /* per-seed value mix (percent) */
static void fill(u8 *p, u32 n)
{
	u32 i, *w = (u32 *)p;

	for (i = 0; i < n / 4; i++) {
		u32 k = rnd() % 100;
		if (k < pzero) w[i] = 0;
		else if (k < pzero + pptr) w[i] = (u32)(uintptr_t)arenas[rnd() % NAR] + ((rnd() % 0x4000) & ~3u);
		else if (k < pzero + pptr + 2) w[i] = (u32)(uintptr_t)vae_rodata + ((rnd() % 15000) & ~3u);
		else if (k < 100) w[i] = rnd() & ((1u << (1 + rnd() % pbits)) - 1);
		else w[i] = rnd();
	}
}
static u64 dg;
static void mix(const void *p, u32 n)
{
	const u8 *b = p; u32 i;
	for (i = 0; i < n; i++) dg = (dg ^ b[i]) * 1099511628211ULL;
}
static void digest(u64 ret, int step)
{
	unsigned i;

	if (getenv("DUMP")) { char nm[200]; FILE *f; snprintf(nm, sizeof nm, "%s.%d", getenv("DUMP"), step); f = fopen(nm, "wb"); fwrite(arenas[atoi(getenv("DUMPA"))], 1, 0x10000, f); fclose(f); }
	dg = 1469598103934665603ULL;
	for (i = 0; i < NAR; i++) { u64 o = dg; mix(arenas[i], 0x10000); if (getenv("DBG")) printf(" a%u %016llx\n", i, (unsigned long long)(dg ^ o)); }
	for (i = 0; i < 4; i++) mix(hdrs[i], hdrsz[i]);
	mix(vae_rodata, sizeof(vae_rodata));
	if (getenv("DBG")) printf(" ret=%llx fa=%x calls=%u reg=%llx,%llx nreg=%u c=%u e=%u ev=%x/%u\n", (unsigned long long)ret, vae_fault_addr, vae_calls, (unsigned long long)harness_reglog[0], (unsigned long long)harness_reglog[1], harness_nreg, harness_ncomp, harness_nevt, vae_events_seen, vae_events_dropped);
	mix(&ret, 8); mix(&vae_faults, 4); { u32 fa = vae_fault_addr >> 28 == 3 ? vae_fault_addr : 0; mix(&fa, 4); }  /* code addresses differ per build */
	mix(&vae_calls, 4); mix(harness_reglog, 16); mix(&harness_nreg, 4);
	mix(&harness_ncomp, 4); mix(&harness_nevt, 4); mix(&vae_events_seen, 4);
	mix(&vae_events_dropped, 4); mix(&harness_irq, 8);
	printf("%d %016llx f=%u c=%u\n", step, (unsigned long long)dg, vae_faults, vae_calls); fflush(stdout);
}

static ucontext_t mainctx, runctx;
static int g_seed, g_nsteps;
typedef u64 (*rootfn)(u32, u32, u32, u32, u32);
static const rootfn roots[] = {
#define F(x) vae_##x,
#include "vae_roots.h"
#undef F
};
#define NROOT (sizeof(roots) / sizeof(roots[0]))
static u32 argarea[8] __aligned(8);

static void run(void)
{
	int s;

	for (s = 0; s < g_nsteps; s++) {
		u64 ret;
		u32 a0, a1, a2, a3;

		memset(stk_lo, 0xa5, STK - 4096);
		budget = 0;
		{ struct itimerval it = { { 0, 0 }, { 0, 300000 } }; setitimer(ITIMER_REAL, &it, NULL); }
		if (setjmp(alarm_jb)) { printf("%d timeout\n", s); return; }
		if (setjmp(budget_jb)) { digest(~0ULL, s); fill(g_ispcore + (rnd() % 0x80) * 0x100, 0x100); continue; }
		/* calc_process as the driver calls it: ae_info[0] -> info, channel 0 */
		a0 = rnd() % 3; a1 = rnd() % 4; a2 = rnd() & 0xffff; a3 = (u32)(uintptr_t)arenas[rnd() % NAR] + ((rnd() % 0x2000) & ~3u);
		if (s % 3 == 0) {
			ret = vae_tisp_ae_calc_process(a0 & 1, 0, 0, 0, (u32)(uintptr_t)argarea);
		} else {
			rootfn f = roots[rnd() % NROOT];
			ret = f(a0 ? (u32)(uintptr_t)arenas[rnd() % NAR] + ((rnd() % 0x2000) & ~3u) : a2, a1, a2, a3,
				(u32)(uintptr_t)argarea);
		}
		{ struct itimerval it = { { 0, 0 }, { 0, 0 } }; setitimer(ITIMER_REAL, &it, NULL); }
		digest(ret, s);
		/* perturb: statistics change between frames */
		fill(g_ispcore + (rnd() % 0x80) * 0x100, 0x100);
	}
}

int main(int argc, char **argv)
{
	unsigned i;
	int n = argc > 1 ? atoi(argv[1]) : 50;

	g_nsteps = argc > 2 ? atoi(argv[2]) : 6;
	signal(SIGALRM, on_alarm);
	stk_lo = mmap(NULL, STK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (stk_lo == MAP_FAILED || (uintptr_t)vae_rodata > 0xffff0000ULL) { puts("no low memory"); return 2; }
	for (g_seed = 1; g_seed <= n; g_seed++) {
		rs = 0x9e3779b97f4a7c15ULL * g_seed;
		pptr = (g_seed * 7) % 40; pzero = (g_seed * 13) % 50; pbits = 1 + (g_seed * 5) % 10;
		for (i = 0; i < NAR; i++) fill(arenas[i], 0x10000);
		fill((u8 *)vae_tisp_ae_hist, sizeof(vae_tisp_ae_hist));
		fill((u8 *)vae_ae_ceguang, sizeof(vae_ae_ceguang));
		vae_faults = vae_calls = 0; harness_nreg = 0;
		printf("seed %d\n", g_seed);
		fflush(stdout);
		{
			pid_t pid = fork();
			int st;

			if (pid) {  /* a wild store on garbage state can smash the host stack: isolate each seed */
				waitpid(pid, &st, 0);
				if (!WIFEXITED(st)) printf("seed %d crashed\n", g_seed);
				continue;
			}
		}
		getcontext(&runctx);
		runctx.uc_stack.ss_sp = stk_lo;
		runctx.uc_stack.ss_size = STK;
		runctx.uc_link = &mainctx;
		makecontext(&runctx, run, 0);
		swapcontext(&mainctx, &runctx);
		fflush(stdout);
		exit(0);
	}
	return 0;
}
