// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the TMO / ADR block of the driver (driver/t32/tx_isp_t32_tmoblk.h: tisp_tmo_init and the
 * interrupt bits 9 / 31 composed from the ports of tx_isp_t32_tmo.h) against the vendor chain (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc): every vendor callee is its reference, none is a stub.
 *
 * Memory: the records hold 32 bit addresses, so both sides run on the SAME arena: private_vmalloc / private_kmalloc
 * (vendor) and ops->vmalloc / kmalloc (ours) take the next block of one bump allocator, logged (kind, size, flags).
 * The vendor runs first, its arena is saved; the arena is restored, ours runs; the two images are compared byte for
 * byte (the whole arena: context, gamma payload, init argument, every record and buffer), then kept as two images
 * that are swapped in for each later step of their side.
 * Part A, tisp_tmo_init (ch 0): the TMO section (11000 bytes) of every *.bin in T32_IQ_DIR (also with random byte
 * edits, 4 variants per bin) and its first gamma record, random init argument (width, height, WDR byte 36), the y
 * filter table d88d4 at its .data value or as a previous stream's adr_fpga left it (random), d03a0 = 0, 1 (shadow:
 * tisp_reg_write = ref/oa_reg_write.c tagged log, tisp_reg_lut_write = the vendor ref into the armed shadow image),
 * 2.  Compared: arena, allocation log, register writes (order, address, value), shadow image, cache syncs, the
 * registered interrupt bits (system_irq_func_set: exactly 9 = tisp_tmo_interrupt_static, 31 =
 * tisp_tmo_fbf_interrupt_calc), the slots e35e0 / e35d0 / e35c0 / e35b0 and every global of the module.
 * Part B, 8 frames per case: random statistics in the 16 KB half that register 28952 selects, random tone buffer
 * (the FBF DMA), status bits {9}, {31}, {9, 31} (+ decoys) through the ported lib_tisp_intp_process on our side and
 * the registered vendor handlers in bit order on the vendor side; tisp_tmo_tgain_update / tisp_tmo_ev_update between
 * frames; registers 0x3240c / 28960 scripted.  From frame 3 on a third of the cases switch stat byte 385 to 0 (the
 * ADR path: adr_stat_calc, adr_ev_func, adr_tgain_func, adr_algorithm -> adr_fpga, adr_reg_write_every; in the
 * vendor reached through tisp_tmo_param_array_set) or to 2 (neither path).  Compared after every step as in part A,
 * plus the return values.  Then tisp_tmo_deinit (free log, slots).
 * Also T32O_NRAND (default 120) cases on fully random sections (ev tables ordered) before the bins, so the oracle runs without
 * T32_IQ_DIR too.
 * Not compared: isp_printf / printk counts (dropped prints of the ports).  Refused (counted): a case where our side
 * would divide by zero (vendor UB, SIGFPE on the host; the reference emulation does not trap).  Channel 0 only
 * (adr_gamma_refresh reads channel 0's record: the vendor dereferences NULL for a lone channel 1).
 * Exit 0 = equal; 1 = mismatch; 2 = no IQ bins with T40_ORACLE_STRICT=1 (after the random cases).
 */
#include <dirent.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_tmoblk.h"

#define WEAK __attribute__((weak))
#define ARN	(1u << 18)
#define NFR	8
#define SHW	8192u

/* ---- the vendor module's .bss / .data ---- */
WEAK u32 t32_data_e35e0[4], t32_data_e35d0[4], t32_data_e35c0[4], t32_data_e35b0[4];
WEAK u32 t32_data_e3580[4], t32_data_e3570[4], t32_data_e3590[4], t32_data_e35a0[4];
WEAK u16 t32_data_e3520[4];
WEAK u32 t32_data_e3504, t32_data_e350c, t32_data_d8b34, t32_data_e3244;
WEAK u16 t32_data_e3516, t32_data_e3518;
WEAK u8 t32_data_e3510[4], t32_data_e3500, t32_data_e3501, t32_data_e34e8[24];
WEAK s16 t32_data_e3248[336];
WEAK int t32_data_e31b0[24];
/* e3528 (s16 per channel), e3530 / e3550 (u64 per channel): adjacent as in the vendor's .bss */
u8 oc_ev[80] __attribute__((aligned(16)));
u32 oc_e3160[12];
u8 oc_d88[160] __attribute__((aligned(16)));	/* d88a4 (24 s16), d88d4 (16), d88f4 (16), d8914 (17): adjacent .data */
__asm__(".globl t32_data_e3528, t32_data_e3530, t32_data_e3534, t32_data_e3550, t32_data_e3160, t32_data_e3188\n"
	".set t32_data_e3528, oc_ev\n.set t32_data_e3530, oc_ev+8\n.set t32_data_e3534, oc_ev+12\n.set t32_data_e3550, oc_ev+40\n"
	".set t32_data_e3160, oc_e3160\n.set t32_data_e3188, oc_e3160+40\n"
	".globl t32_data_d88a4, t32_data_d88d4, t32_data_d88d6, t32_data_d88f0, t32_data_d88f4, t32_data_d8914\n"
	".set t32_data_d88a4, oc_d88\n.set t32_data_d88d4, oc_d88+48\n.set t32_data_d88d6, oc_d88+50\n"
	".set t32_data_d88f0, oc_d88+76\n.set t32_data_d88f4, oc_d88+80\n.set t32_data_d8914, oc_d88+112\n");
/* the vendor's constant tables (values: tx_isp_t32_tmotab.h, checked against data.c by tmotab_check.py) */
WEAK s16 t32_data_c0444[40], t32_data_c0488[16], t32_data_c04a4[16], t32_data_d87a0[136], t32_data_c04b8[8];
WEAK int t32_data_d8120[128], t32_data_d8320[2], t32_data_d8324[256];
WEAK u16 t32_data_c029c[202], t32_data_d89a0[202], t32_data_c0544[202], t32_data_c0430[10];
/* rodata strings of the vendor's prints (not printed) */
WEAK u8 t32_data_ca38c[64], t32_data_ca3a4[64], t32_data_ca3bc[64], t32_data_ca3e0[64], t32_data_ca408[64],
	t32_data_ca418[64], t32_data_c0518[64];

u64 tisp_tmo_init(u32, u32, u32, u32);
u64 tisp_tmo_interrupt_static(u32, u32, u32, u32);
u64 tisp_tmo_fbf_interrupt_calc(u32, u32, u32, u32);
u64 tisp_tmo_tgain_update(u32, u32, u32, u32);
u64 tisp_tmo_ev_update(u32, u32, u32, u32, u32);
u64 tisp_tmo_deinit(u32, u32, u32, u32);
#define FN(x) ((t32o_fn)(void (*)(void))(x))
extern u32 t32o_dma_n, t32o_dma_a[4];

static u32 rs = 0x7d31c9u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static u32 rb(u32 n) { return rnd() % n; }

static unsigned int compared, refused, frames, nbins, n_adr, n_tmo, n_fbf, n_evu, n_tg, cases, n_d03[3];
static const char *cname;
static u32 D03;

#define BAD(...) do { printf("MISMATCH %s case %u (d03a0 %u): ", cname, cases, D03); printf(__VA_ARGS__); printf("\n"); exit(1); } while (0)

/* ---- one bump allocator for both sides (arena A), logged ---- */
static u8 *A;
static u32 bump;
#define LOGN 64
struct alog { u32 n, e[LOGN][3]; };
static struct alog va, oa_;

static void *arena_get(struct alog *l, u32 kind, u32 size, u32 flags)
{
	void *p;

	if (l->n < LOGN) {
		l->e[l->n][0] = kind;
		l->e[l->n][1] = size;
		l->e[l->n][2] = flags;
	}
	l->n++;
	size = (size + 15u) & ~15u;
	if (bump + size > ARN) {
		printf("t32_tmoblk_oracle: arena full\n");
		exit(1);
	}
	p = A + bump;
	bump += size;
	return p;
}

static void arena_free(struct alog *l, u32 kind, u32 a)
{
	if (l->n < LOGN) {
		l->e[l->n][0] = kind;
		l->e[l->n][1] = a ? a - T32O_PTR(A) : 0xffffffffu;
		l->e[l->n][2] = 0;
	}
	l->n++;
}

u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return T32O_PTR(arena_get(&va, 1, a0, 0)); }
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return T32O_PTR(arena_get(&va, 2, a0, a1)); }
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; arena_free(&va, 3, a0); return 0; }
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; arena_free(&va, 4, a0); return 0; }

/* system_irq_func_set: the vendor's handler table */
static u32 virq[T32_INTP_BITS], virq_n, virq_ch;
u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	virq_n++;
	virq_ch = a0;
	if (a1 < T32_INTP_BITS)
		virq[a1] = a2;
	return 0;
}

/* ---- register reads (both sides) ---- */
static u32 SEL, R40C, R960;
static u32 rdv(u32 reg)
{
	if (reg == 28952)
		return SEL;
	if (reg == 0x3240c)
		return R40C;
	if (reg == 28960)
		return R960;
	return 0x5a000000u | reg;
}

/* ---- our side: struct t32_tmob_ops ---- */
static u32 OW[T32O_MAX_WRITES * 2][2], OWN, OSYNC, OSA[2], SHN;
static u32 SH[SHW], *VIMG;
static t32_intp_fn oirq[T32_INTP_BITS];
static u32 oirq_n, oirq_ch;

static void ow_add(u32 reg, u32 val)
{
	if (OWN < T32O_MAX_WRITES * 2) {
		OW[OWN][0] = reg;
		OW[OWN][1] = val;
	}
	OWN++;
}
static void *o_vm(u32 size) { return arena_get(&oa_, 1, size, 0); }
static void *o_km(u32 size, u32 flags) { return arena_get(&oa_, 2, size, flags); }
static void o_vf(void *p) { arena_free(&oa_, 3, T32O_PTR(p)); }
static void o_kf(void *p) { arena_free(&oa_, 4, T32O_PTR(p)); }
static u32 o_rd(u32 reg) { return rdv(reg); }
static void o_sysw(u32 reg, u32 val) { ow_add(reg, val); }
static void o_regw(u32 ch, u32 reg, u32 val) { ow_add(D03 == 1 ? 0x80000000u | (ch & 7) << 24 | (reg & 0xffffff) : reg, val); }
static void o_lutw(u32 ch, u32 reg, u32 val, u32 idx)
{
	if (D03 != 1) {
		ow_add(reg, val);
		return;
	}
	if (ch == 0 && reg + idx < SHW)
		SH[reg + idx] = val;
	else
		SHN++;
}
static void o_sync(void *p, u32 len) { OSYNC++; OSA[0] = T32O_PTR(p); OSA[1] = len; }
static void o_irq(u32 ch, u32 bit, t32_intp_fn fn) { oirq_n++; oirq_ch = ch; if (bit < T32_INTP_BITS) oirq[bit] = fn; }
static const struct t32_tmob_ops oops = { o_vm, o_km, o_vf, o_kf, o_rd, o_sysw, o_regw, o_lutw, o_sync, o_irq };

/* ---- SIGFPE guard of our side (vendor UB: a zero divisor) ---- */
static sigjmp_buf fpe_jb;
static volatile int fpe_armed;
static void on_fpe(int s) { (void)s; if (fpe_armed) siglongjmp(fpe_jb, 1); abort(); }

/* ---- the two images ---- */
static u8 IMG0[ARN], VI[ARN], OI[ARN];
static u32 vbump, obump;

static void begin_vendor(void)
{
	memcpy(A, VI, ARN);
	bump = vbump;
	memset(&t32o_log, 0, sizeof(t32o_log));
	t32o_dma_n = 0;
	memset(t32o_dma_a, 0, sizeof(t32o_dma_a));
	t32o_arg5 = t32o_arg6 = t32o_arg7 = t32o_arg8 = t32o_arg9 = 0;
}
static void end_vendor(void) { memcpy(VI, A, ARN); vbump = bump; }
static void begin_ours(void) { memcpy(A, OI, ARN); bump = obump; OWN = 0; OSYNC = 0; OSA[0] = OSA[1] = 0; SHN = 0; }
static void end_ours(void) { memcpy(OI, A, ARN); obump = bump; }

static void cmp_writes(const char *what)
{
	u32 i;

	if (t32o_log.n > T32O_MAX_WRITES)
		BAD("%s: vendor log overflow %u", what, t32o_log.n);
	if (t32o_log.n != OWN || SHN)
		BAD("%s: write count vendor %u ours %u (shadow misses %u)", what, t32o_log.n, OWN, SHN);
	for (i = 0; i < OWN; i++)
		if (t32o_log.w[i].reg != OW[i][0] || t32o_log.w[i].val != OW[i][1])
			BAD("%s: write %u vendor %x=%x ours %x=%x", what, i, t32o_log.w[i].reg, t32o_log.w[i].val, OW[i][0], OW[i][1]);
	if (t32o_dma_n != OSYNC || (OSYNC && (t32o_dma_a[0] || t32o_dma_a[1] != OSA[0] || t32o_dma_a[2] != OSA[1] || t32o_dma_a[3])))
		BAD("%s: cache sync vendor %u (%x %x) ours %u (%x %x)", what, t32o_dma_n, t32o_dma_a[1], t32o_dma_a[2], OSYNC, OSA[0], OSA[1]);
	if (D03 == 1 && memcmp(VIMG, SH, sizeof(SH))) {
		for (i = 0; i < SHW && VIMG[i] == SH[i]; i++)
			;
		BAD("%s: shadow word %u vendor %x ours %x", what, i, VIMG[i], SH[i]);
	}
}

static void cmp_mem(const char *what)
{
	u32 i;

	if (memcmp(VI, OI, ARN) || vbump != obump) {
		for (i = 0; i < ARN && VI[i] == OI[i]; i++)
			;
		BAD("%s: arena byte %#x vendor %02x ours %02x (bump %x / %x)", what, i, i < ARN ? VI[i] : 0, i < ARN ? OI[i] : 0, vbump, obump);
	}
	if (va.n != oa_.n || memcmp(va.e, oa_.e, sizeof(va.e)))
		BAD("%s: allocation log (%u / %u entries)", what, va.n, oa_.n);
}

#define G1(v, o) do { if ((u64)(v) != (u64)(o)) BAD("%s: global %s vendor %llx ours %llx", what, #v, (unsigned long long)(v), (unsigned long long)(o)); } while (0)
static void cmp_glob(const char *what)
{
	const struct t32_tmob *m = &t32_tmob;
	int k;

	for (k = 0; k < 4; k++) {
		u32 o = k < T32_TMOB_CH;

		G1(t32_data_e35e0[k], o ? m->e35e0[k] : 0);
		G1(t32_data_e35d0[k], o ? m->e35d0[k] : 0);
		G1(t32_data_e35c0[k], o ? m->e35c0[k] : 0);
		G1(t32_data_e35b0[k], o ? m->e35b0[k] : 0);
		G1(t32_data_e3580[k], o ? m->e3580[k] : 0);
		G1(t32_data_e3570[k], o ? m->e3570[k] : 0);
		G1(t32_data_e3590[k], o ? m->tg.b[k] : 0);
		G1(t32_data_e35a0[k], o ? m->tg.a[k] : 0);
		G1(t32_data_e3520[k], o ? (u16)m->e3520[k] : 0);
		G1(((u16 *)oc_ev)[k], o ? (u16)m->e3528[k] : 0);
		G1(((u64 *)(oc_ev + 8))[k], o ? m->e3530[k] : 0);
		if (k < 3)	/* e3550 [3] would be past the blob */
			G1(((u64 *)(oc_ev + 40))[k], o ? m->e3550[k] : 0);
		G1(t32_data_e3510[k], o ? m->e3510[k] : 0);
	}
	G1(t32_data_e3504, m->e3504);
	G1(t32_data_e350c, m->e350c);
	G1(t32_data_e3516, m->e3516);
	G1(t32_data_e3518, m->e3518);
	G1(t32_data_d8b34, m->d8b34);
	G1(t32_data_e3500, m->e3500);
	G1(t32_data_e3501, m->e3501);
	G1(t32_data_e3244, m->e3244);
	if (memcmp(t32_data_e31b0, m->e31b0, sizeof(m->e31b0)) || memcmp(oc_e3160, m->e3160, sizeof(m->e3160)) ||
	    memcmp(oc_d88 + 48, m->d88d4, sizeof(m->d88d4)) || memcmp(t32_data_e34e8, m->e34e8, 24) ||
	    memcmp(t32_data_e3248, m->e3248, sizeof(m->e3248)))
		BAD("%s: a table of the module (e31b0 / e3160 / d88d4 / e34e8 / e3248) differs", what);
}

static void cmp_all(const char *what)
{
	cmp_writes(what);
	cmp_mem(what);
	cmp_glob(what);
	compared++;
}

/* ours under the SIGFPE guard: 0 = ran, 1 = trapped; the arguments are globals */
static int guard(void (*fn)(void))
{
	fpe_armed = 1;
	if (sigsetjmp(fpe_jb, 1)) {
		fpe_armed = 0;
		return 1;
	}
	fn();
	fpe_armed = 0;
	return 0;
}
static const u32 *g_info;
static u8 *g_ctx, *g_gm;
static u32 g_a2, g_a3, g_a4, g_sta;
static int g_ro;
static void g_init(void) { t32_tmob_init(0, g_info, g_ctx, g_gm); }
static void g_evu(void) { t32_tmob_ev_update(0, g_a2, g_a3, g_a4); }
static void g_intp(void)
{
	struct t32_intp_glob g;

	memset(&g, 0, sizeof(g));
	g.st[0] = g_sta;
	g_ro = (int)t32_lib_intp_process(&g, oirq, 0);
}

static void tables_init(void)
{
	memcpy(t32_data_c0444, t32_tmo_tab_c0444, sizeof(t32_tmo_tab_c0444));
	memcpy(t32_data_c0488, t32_tmo_tab_c0488, sizeof(t32_tmo_tab_c0488));
	memcpy(t32_data_c04a4, t32_tmo_tab_c04a4, sizeof(t32_tmo_tab_c04a4));
	memcpy(t32_data_d87a0, t32_tmo_tab_d87a0, sizeof(t32_tmo_tab_d87a0));
	memcpy(t32_data_c04b8, t32_tmo_tab_c04b8, sizeof(t32_tmo_tab_c04b8));
	memcpy(t32_data_d8120, t32_tmo_tab_d8120, sizeof(t32_tmo_tab_d8120));
	t32_data_d8320[0] = T32_TMO_TAB_D8320;
	memcpy(t32_data_d8324, t32_tmo_tab_d8324, sizeof(t32_tmo_tab_d8324));
	memcpy(t32_data_c029c, t32_tmo_tab_c029c, sizeof(t32_tmo_tab_c029c));
	memcpy(t32_data_d89a0, t32_tmo_tab_d89a0, sizeof(t32_tmo_tab_d89a0));
	memcpy(t32_data_c0544, t32_tmo_tab_c0544, sizeof(t32_tmo_tab_c0544));
	memcpy(t32_data_c0430, t32_tmo_tab_c0430, sizeof(t32_tmo_tab_c0430));
	memcpy(oc_d88, t32_tmo_tab_d88a4, sizeof(t32_tmo_tab_d88a4));
	memcpy(oc_d88 + 80, t32_tmo_tab_d88f4, sizeof(t32_tmo_tab_d88f4));
	memcpy(oc_d88 + 112, t32_tmo_tab_d8914, sizeof(t32_tmo_tab_d8914));
}

/* the module state at load: zero .bss, d88d4 = its .data value (both sides) */
static void module_load(void)
{
	memset(t32_data_e35e0, 0, sizeof(t32_data_e35e0)); memset(t32_data_e35d0, 0, sizeof(t32_data_e35d0));
	memset(t32_data_e35c0, 0, sizeof(t32_data_e35c0)); memset(t32_data_e35b0, 0, sizeof(t32_data_e35b0));
	memset(t32_data_e3580, 0, sizeof(t32_data_e3580)); memset(t32_data_e3570, 0, sizeof(t32_data_e3570));
	memset(t32_data_e3590, 0, sizeof(t32_data_e3590)); memset(t32_data_e35a0, 0, sizeof(t32_data_e35a0));
	memset(t32_data_e3520, 0, sizeof(t32_data_e3520)); memset(oc_ev, 0, sizeof(oc_ev)); memset(oc_e3160, 0, sizeof(oc_e3160));
	t32_data_e3504 = t32_data_e350c = t32_data_d8b34 = t32_data_e3244 = 0;
	t32_data_e3516 = t32_data_e3518 = 0;
	memset(t32_data_e3510, 0, sizeof(t32_data_e3510));
	t32_data_e3500 = t32_data_e3501 = 0;
	memset(t32_data_e34e8, 0, sizeof(t32_data_e34e8)); memset(t32_data_e3248, 0, sizeof(t32_data_e3248));
	memset(t32_data_e31b0, 0, sizeof(t32_data_e31b0));
	memcpy(oc_d88 + 48, t32_tmo_tab_d88d4, sizeof(t32_tmo_tab_d88d4));
	memset(&t32_tmob, 0, sizeof(t32_tmob));
	t32_tmob_load(&t32_tmob);
}

static const u32 sizes[][2] = { { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }, { 1280, 720 }, { 640, 360 }, { 2304, 1296 },
				{ 2880, 1620 }, { 320, 240 }, { 15, 25 }, { 3000, 3000 }, { 7, 12 }, { 4000, 4000 } };

/* one case: init + NFR frames + deinit on section sec (11000 bytes) and gamma record gam (258 bytes) */
static void run_case(const char *name, const u8 *sec, const u8 *gam)
{
	u8 *ctx, *gm, *info, *lut;
	u32 i, ch = 0, adr_from = 99, adr_val = 0, *w, k;
	static const char *const what[] = { "init", "frame", "deinit" };
	int rv, ro;

	cname = name;
	cases++;
	t32o_reset();
	t32o_rd_hook = rdv;
	A = t32o_alloc(ARN);
	D03 = rb(3);
	t32_data_d03a0[0] = (u8)D03;
	n_d03[D03]++;
	VIMG = D03 == 1 ? t32o_shadow_arm(SHW) : 0;
	if (VIMG)
		memcpy(SH, VIMG, sizeof(SH));
	module_load();
	t32_tmob.ops = &oops;
	t32_tmob.d03a0 = (u8)D03;
	if (rb(3) == 0)		/* a previous stream's adr_fpga moved the y filter positions */
		for (k = 0; k < 16; k++) {
			s16 v = (s16)(k * 256 + rb(200));

			memcpy(oc_d88 + 48 + 2 * k, &v, 2);
			t32_tmob.d88d4[k] = v;
		}
	memset(virq, 0, sizeof(virq));
	memset(oirq, 0, sizeof(oirq));
	virq_n = oirq_n = 0;
	va.n = oa_.n = 0;
	memset(&va, 0, sizeof(va));
	memset(&oa_, 0, sizeof(oa_));
	SEL = 0; R40C = 0; R960 = rnd();

	/* the arena: random pattern (kmalloc'd memory is not cleared), then context, gamma, LUT info and argument */
	for (i = 0; i < ARN; i += 4) {
		u32 v = rnd();

		memcpy(A + i, &v, 4);
	}
	bump = 0;
	ctx = arena_get(&va, 0, T32_TMOB_SEC_BYTES, 0);
	memcpy(ctx, sec, T32_TMOB_SEC_BYTES);
	gm = arena_get(&va, 0, 512, 0);
	memcpy(gm, gam, T32_TMOB_GAMMA_BYTES);
	lut = arena_get(&va, 0, 64, 0);
	info = arena_get(&va, 0, 64, 0);
	{
		u32 sz = rb(4) ? rb(sizeof(sizes) / sizeof(sizes[0])) : 99, wd, ht;

		wd = sz == 99 ? 16 + rb(5000) : sizes[sz][0];
		ht = sz == 99 ? 16 + rb(5000) : sizes[sz][1];
		memcpy(info, &wd, 4);
		memcpy(info + 4, &ht, 4);
		info[36] = (u8)(rb(3) == 0 ? rnd() : rb(2));
	}
	{
		u32 p = T32O_PTR(gm);

		memcpy(lut + 32, &p, 4);
	}
	t32_data_dc274[0] = T32O_PTR(lut);
	t32_data_dd0a4[0] = T32O_PTR(ctx) - 11976u;
	va.n = 0;
	memset(&va, 0, sizeof(va));
	memcpy(IMG0, A, ARN);
	memcpy(VI, A, ARN);
	memcpy(OI, A, ARN);
	vbump = obump = bump;

	/* ---- part A: tisp_tmo_init ---- */
	begin_vendor();
	t32o_call(FN(tisp_tmo_init), ch, T32O_PTR(info), 0, 0, 0);
	end_vendor();
	begin_ours();
	g_info = (const u32 *)(void *)info;
	g_ctx = ctx;
	g_gm = gm;
	if (guard(g_init)) {
		refused++;
		return;
	}
	end_ours();
	cmp_all(what[0]);
	if (virq_n != 2 || oirq_n != 2 || virq_ch != ch || oirq_ch != ch)
		BAD("irq registration count %u / %u", virq_n, oirq_n);
	for (i = 0; i < T32_INTP_BITS; i++) {
		int vb = virq[i] == (u32)(uintptr_t)tisp_tmo_interrupt_static ? 1 : virq[i] == (u32)(uintptr_t)tisp_tmo_fbf_interrupt_calc ? 2 : virq[i] ? 3 : 0;
		int ob = oirq[i] == t32_tmob_irq_tmo ? 1 : oirq[i] == t32_tmob_irq_fbf ? 2 : oirq[i] ? 3 : 0;

		if (vb != ob || (vb && i != (vb == 1 ? 9u : 31u)))
			BAD("irq bit %u: vendor %d ours %d", i, vb, ob);
	}

	/* ---- part B: frames ---- */
	if (rb(3) == 0) {
		adr_from = 3;
		adr_val = rb(4) ? 0 : 2;
	}
	for (k = 0; k < NFR; k++) {
		static const u32 stat[] = { 1u << 9, 1u << 31, (1u << 9) | (1u << 31), (1u << 9) | (1u << 8) | (1u << 30), (1u << 31) | 1u };
		u32 sta = stat[rb(5)], buf, half, tb, bit, mag;
		u8 *st;

		/* the hardware: statistics DMA into the selected half, the FBF tone buffer; both images */
		SEL = rnd();
		R40C = rb(4) ? 0 : rnd();
		R960 = rnd();
		w = (u32 *)(void *)(VI + (t32_data_e35e0[ch] - T32O_PTR(A)));
		buf = ((u32 *)(void *)(VI + (t32_data_e35d0[ch] - T32O_PTR(A))))[1] - T32O_PTR(A);
		half = buf + ((SEL & 3u) << 14);
		tb = w[5] - T32O_PTR(A);
		st = VI + (w[1] - T32O_PTR(A));
		{
			static const u32 mk[] = { 0x7fff7fffu, 0x0fff0fffu, 0x00ff00ffu, 0x003f003fu, 0x00070007u };

			mag = mk[rb(5)];
		}
		for (i = 0; i < 16384; i += 4) {
			u32 v = rnd() & (rb(8) ? mag : 0xffffffffu);

			memcpy(VI + half + i, &v, 4);
		}
		for (i = 0; i < 15040; i += 2) {
			u16 v = (u16)(rb(6) ? rb(4096) : rnd());

			memcpy(VI + tb + i, &v, 2);
		}
		if (k >= adr_from)
			st[385] = (u8)adr_val;
		if (rb(4) == 0) {	/* tisp_tmo_api_strength_ctrl: record u16 +54 = 1 (drc on), +56 = strength */
			u16 on = (u16)(rb(4) ? 1 : rb(3)), str = (u16)(rb(3) ? rb(257) : rnd());

			memcpy((u8 *)w + 54, &on, 2);
			memcpy((u8 *)w + 56, &str, 2);
			memcpy(OI + ((u8 *)w - VI) + 54, &on, 2);
			memcpy(OI + ((u8 *)w - VI) + 56, &str, 2);
		}
		memcpy(OI + half, VI + half, 16384);
		memcpy(OI + tb, VI + tb, 15040);
		OI[st - VI + 385] = st[385];
		if (st[385] == 0)
			n_adr++;

		/* the AE per frame: tgain / ev updates */
		if (rb(2)) {
			u32 a2 = rb(3) ? rb(0x200000) : rnd(), a3 = rb(4) ? 0 : rb(4);

			begin_vendor();
			t32o_call(FN(tisp_tmo_tgain_update), ch, 0, a2, a3, 0);
			end_vendor();
			begin_ours();
			t32_tmob_tgain_update((int)ch, a2, a3);
			end_ours();
			cmp_all("tgain_update");
			n_tg++;
		}
		if (rb(3) == 0) {
			u32 a2 = rnd(), a3 = rb(3) ? 0 : rb(16), a4 = rb(2) ? rnd() : rb(0x40000);
			u32 *cw = (u32 *)(void *)(VI + (t32_data_e35e0[ch] - T32O_PTR(A)));
			u32 *tbl = (u32 *)(void *)(VI + (cw[0] - T32O_PTR(A)) + 888);

			if (rb(2))	/* near a table entry of the ev search */
				a2 = (tbl[rb(10)] + rb(3) - 1) << 10;
			begin_vendor();
			t32o_call(FN(tisp_tmo_ev_update), ch, 0, a2, a3, a4);
			end_vendor();
			begin_ours();
			g_a2 = a2;
			g_a3 = a3;
			g_a4 = a4;
			if (guard(g_evu)) {
				refused++;
				return;
			}
			end_ours();
			cmp_all("ev_update");
			n_evu++;
		}

		/* the interrupt: vendor handlers in bit order; ours through lib_tisp_intp_process */
		begin_vendor();
		rv = 1;
		for (bit = 0; bit < 32; bit++)
			if ((sta >> bit & 1) && virq[bit]) {
				u32 r = (u32)t32o_call((t32o_fn)(uintptr_t)virq[bit], ch, 0, 0, 0, 0);

				if (r != 1)
					rv = (int)r;
			}
		end_vendor();
		begin_ours();
		g_sta = sta;
		if (guard(g_intp)) {
			refused++;
			return;
		}
		ro = g_ro;
		end_ours();
		if (rv != ro)
			BAD("frame %u: return vendor %d ours %d", k, rv, ro);
		cmp_all(what[1]);
		n_tmo += (sta >> 9) & 1;
		n_fbf += sta >> 31;
		frames++;
	}

	/* ---- tisp_tmo_deinit ---- */
	va.n = oa_.n = 0;
	memset(&va, 0, sizeof(va));
	memset(&oa_, 0, sizeof(oa_));
	begin_vendor();
	t32o_call(FN(tisp_tmo_deinit), ch, 0, 0, 0, 0);
	end_vendor();
	begin_ours();
	t32_tmob_deinit((int)ch);
	end_ours();
	cmp_all(what[2]);
}

/* the mode selectors of the chain: input_prep 10928, ev search 10931, ev_interp 4138, stat_limit 3382 / 3366 / 3368,
 * adr_ev_func 9574 / 9846 / 9896 / 9268, sometimes the input_prep weights 10891 .. 10893 and maps 10894 .. 10927 */
static void steer(u8 *sec)
{
	static const u16 sel8[] = { 10928, 10931 }, sel16[] = { 4138, 3382, 3366, 9574, 9846, 9896, 9268 };
	u32 j;
	u16 x;

	for (j = 0; j < 2; j++)
		if (rb(2))
			sec[sel8[j]] = (u8)(rb(4) ? rb(3) : rnd());
	for (j = 0; j < 7; j++)
		if (rb(2)) {
			x = (u16)(rb(4) ? rb(3) : rnd());
			memcpy(sec + sel16[j], &x, 2);
		}
	x = (u16)rb(17);
	memcpy(sec + 3368, &x, 2);
	if (rb(2))
		for (j = 10891; j < 10928; j++)
			sec[j] = (u8)rnd();
}

static void bin_cases(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char path[1024];
		static u8 bin[1u << 20], sec[T32_TMOB_SEC_BYTES], gam[T32_TMOB_GAMMA_BYTES];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);
		u32 off, len = 0, goff, glen = 0, v, j;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_TMOB_SEC, &len);
		goff = t32_iq_section(bin, (u32)bytes, 13, &glen);
		if (!off || len < T32_TMOB_SEC_BYTES)
			continue;
		memset(gam, 0, sizeof(gam));
		if (goff && goff + glen + T32_TMOB_GAMMA_BYTES <= bytes)
			memcpy(gam, bin + goff + glen, T32_TMOB_GAMMA_BYTES);
		nbins++;
		for (v = 0; v < 4; v++) {
			memcpy(sec, bin + off, T32_TMOB_SEC_BYTES);
			if (v)	/* random byte edits */
				for (j = 0; j < 4u * v; j++)
					sec[rb(T32_TMOB_SEC_BYTES)] = (u8)rnd();
			if (v >= 2)
				steer(sec);
			run_case(e->d_name, sec, gam);
		}
	}
	closedir(d);
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");

	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 120;
	static u8 sec[T32_TMOB_SEC_BYTES], gam[T32_TMOB_GAMMA_BYTES];
	unsigned int c, j;

	signal(SIGFPE, on_fpe);
	tables_init();
	for (c = 0; c < n; c++) {	/* random sections: every byte random, the ev tables ascending / descending */
		u32 *t;

		for (j = 0; j < T32_TMOB_SEC_BYTES; j++)
			sec[j] = (u8)rnd();
		for (j = 0; j < T32_TMOB_GAMMA_BYTES; j++)
			gam[j] = (u8)rnd();
		t = (u32 *)(void *)(sec + 888);
		for (j = 0; j < 10; j++)
			t[j] = (j + 1) * (rb(4) ? 4000u : rnd() >> 4);
		t = (u32 *)(void *)(sec + 1008);
		for (j = 0; j < 10; j++)
			t[j] = (10 - j) * (rb(4) ? 4000u : rnd() >> 4);
		if (rb(3))
			steer(sec);
		run_case("random", sec, gam);
	}
	if (dir)
		bin_cases(dir);
	if (!nbins && getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"))) {
		printf("t32_tmoblk_oracle: no IQ bins with a TMO section (set T32_IQ_DIR)\n");
		return 2;
	}
	printf("t32_tmoblk_oracle: %u cases (%u IQ bins, %u refused; d03a0 0 / 1 / 2: %u / %u / %u), %u compares, %u frames (%u bit 9, "
	       "%u bit 31, %u ADR frames), %u tgain / %u ev updates, 0 mismatches\n", cases, nbins, refused, n_d03[0], n_d03[1], n_d03[2],
	       compared, frames, n_tmo, n_fbf, n_adr, n_tg, n_evu);
	return 0;
}
