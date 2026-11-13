// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE per-frame paths tisp_ae_get_bv, tisp_ae_calc_process and
 * tisp_ae_interrupt_static.  Runs the unmodified vendor functions (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc) and our t32_ae_get_bv / t32_ae_calc_process /
 * t32_ae_interrupt_static (driver/t32/tx_isp_t32_stats.h) from the same initial
 * memory and compares the return value, the whole state arena, the register
 * writes, the events and the completion calls.
 *
 * Our hooks call the same env/ref functions through t32o_call, so fixed point
 * maths, allocators and events are identical on both sides.  The debug print
 * block of calc_process (e1500 set and e14f0 >= frame count) is never generated
 * (it reads t32_data_e16b4 via tisp_awb_get_attr).  The interrupt case runs on
 * channel 0 (the vendor long allocator indexes e1588[0]).
 * tisp_ae_lib_bilinear_intp is not provided by env.c: a deterministic stand-in
 * below is used by both sides (short_expt).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

/* data objects: weak definitions in case env.c does not provide them */
#define WEAKD __attribute__((weak))
WEAKD u32 t32_data_e1588[4], t32_data_e15a8[4], t32_data_e1598[4], t32_data_e15b8[4];
WEAKD u32 t32_data_e1500[4], t32_data_e14f0[4], t32_data_e1578[4];
WEAKD u8 t32_data_e1510[4];
WEAKD u32 t32_data_d9008;
WEAKD u8 t32_data_dd2d4[4 * 232];
WEAKD u32 t32_data_e16b4[4];
/* printk format strings of the debug block (never reached) */
WEAKD u8 t32_data_c85e4[64], t32_data_c85f8[64], t32_data_c8604[64], t32_data_c860c[64];
WEAKD u8 t32_data_c8618[64], t32_data_c8628[64], t32_data_c8638[64], t32_data_c8644[64];
WEAKD u8 t32_data_c8654[64], t32_data_c8664[64], t32_data_c8670[64], t32_data_c8680[64];
__attribute__((weak)) int printk(const char *fmt, ...) { (void)fmt; return 0; }

/*
 * env.c has strong stubs for these two: include the unmodified reference files
 * here under other names instead of linking them.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#pragma GCC diagnostic ignored "-Wunused-label"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#define tisp_ae_calc_process ref_tisp_ae_calc_process
#include "ref/fn_00000000000341c4_tisp_ae_calc_process.c"
#undef tisp_ae_calc_process
#define tisp_ae_interrupt_static ref_tisp_ae_interrupt_static
#include "ref/fn_000000000003584c_tisp_ae_interrupt_static.c"
#undef tisp_ae_interrupt_static
#pragma GCC diagnostic pop
u64 tisp_ae_calc_process(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_interrupt_static(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_get_bv(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_long_ev_alloc(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_short_ev_alloc(u32 a0, u32 a1, u32 a2, u32 a3);
u64 fix_point_mult3(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_log2_fixed_to_fixed(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_log2_fixed_to_fixed_64(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3);
u64 system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_event_push(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_algorithm(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_ae_par_update_trig(u32 a0, u32 a1, u32 a2, u32 a3);
u64 api_ae_long_process(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_complete(u32 a0, u32 a1, u32 a2, u32 a3);

/* weak stand-ins for symbols env.c may lack; same symbol for both sides */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define WEAK __attribute__((weak))
WEAK u64 tisp_log2_fixed_to_fixed(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return (u64)((a0 * 0x9e3779b1u ^ (a0 >> 7)) + a1 * 3 + a2 * 5); /* 3 args: vendor leaves a3 undefined */
}
WEAK u64 tisp_log2_fixed_to_fixed_64(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return (u64)((a0 * 0x85ebca6bu ^ a1 * 0xc2b2ae35u ^ (a0 >> 9)) + a2 * 3 + a3);
}
WEAK u64 fix_point_mult3(u32 a0, u32 a1, u32 a2, u32 a3)
{
	/* plain shift: keeps x comparable with the thresholds in gen_img() */
	return ((u64)a3 << 32 | a2) >> (a0 & 31);
}
WEAK u64 tisp_set_ae_short_ag(u32 a0, u32 a1, u32 a2, u32 a3) { return 0; }
WEAK u64 tisp_set_ae_long_ag(u32 a0, u32 a1, u32 a2, u32 a3) { return 0; }
WEAK u64 tisp_ae_short_set_sensor_integration_time(u32 a0, u32 a1, u32 a2, u32 a3) { return 0; }
WEAK u64 tisp_ae_long_set_sensor_integration_time(u32 a0, u32 a1, u32 a2, u32 a3) { return 0; }
WEAK u64 lib_tisp_api_g_runningMode(u32 a0, u32 a1, u32 a2, u32 a3) { return a0 & 1; }
WEAK u64 tisp_ae_algorithm(u32 a0, u32 a1, u32 a2, u32 a3) { return 0; }
WEAK u64 tisp_ae_par_update_trig(u32 a0, u32 a1, u32 a2, u32 a3) { return 0; }
WEAK u64 api_ae_long_process(u32 a0, u32 a1, u32 a2, u32 a3) { return 0; }
WEAK u64 tisp_ae_api_get_luma(u32 a0, u32 a1, u32 a2, u32 a3) { return 0; }
WEAK u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return t32o_rd_hook ? t32o_rd_hook(a1) : 0;
}
WEAK u64 system_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return t32o_rd_hook ? t32o_rd_hook(a0) : 0;
}

/* stand-in, same symbol for the vendor and our side */
u64 tisp_ae_lib_bilinear_intp(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 lo = a0 * 0x9e3779b1u ^ a2 * 0x85ebca6bu ^ (a3 >> 3);
	u32 hi = a1 * 0xc2b2ae35u ^ (a0 >> 5);

	return ((u64)hi << 32) | lo;
}

#define CALLF(f, a, b, c, d, e) t32o_call((t32o_fn)(void (*)(void))(f), a, b, c, d, e)

#define OFF_ST		0x0000u
#define OFF_TUNE	0x1000u
#define OFF_BUF		0x3000u
#define OFF_CTX		0x7000u
#define OFF_SRC		0x8000u
#define OFF_E98		0xc000u
#define ARENA		0xc100u

enum { K_BV, K_CALC, K_INT };

struct par {
	u32 ch, stopped, mode, dbg, dbg_max, cnt0, trace, flag, salt;
};

struct snap {
	u8 mem[ARENA];
	struct t32o_log log;
	u32 ev_n, evlog[T32O_EVLOG][9], cn, carg, ret, cnt, stop;
	struct t32_stats_write w[16];
	unsigned int nw;
};

static u32 rs = 0x5e3a91u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 rd_salt;
static u32 rdhook(u32 reg)
{
	u32 v = (reg + rd_salt) * 0x9e3779b1u;

	return v ^ (v >> 15) ^ rd_salt;
}

static u8 img[ARENA];
static u32 evbuf[8];
static u8 *base_a;
static struct snap sref, sour;
static unsigned int ncmp[3];

static void put32(u8 *p, u32 v) { memcpy(p, &v, 4); }
static void put16(u8 *p, u32 v) { u16 x = (u16)v; memcpy(p, &x, 2); }

/* hooks: same functions as the vendor calls */
static u32 h_div32(u32 sh, u32 a, u32 b) { return (u32)CALLF(fix_point_div_32, sh, a, b, 0, 0); }
static u32 h_log2(u32 v, u32 a, u32 b) { return (u32)CALLF(tisp_log2_fixed_to_fixed, v, a, b, 0, 0); }
static u32 h_log2_64(t32_u64 v, u32 a, u32 b)
{
	return (u32)CALLF(tisp_log2_fixed_to_fixed_64, (u32)v, (u32)(v >> 32), a, b, 0);
}
static t32_u64 h_mult3(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c)
{
	u64 r;

	t32o_arg5 = (u32)(b >> 32);
	t32o_arg6 = (u32)c;
	r = CALLF(fix_point_mult3, sh, 0, (u32)a, (u32)(a >> 32), (u32)b);
	t32o_arg5 = 0;
	t32o_arg6 = 0;
	return r;
}
static u32 h_rd(u32 ch, u32 reg) { return (u32)CALLF(tisp_reg_read, ch, reg, 0, 0, 0); }
static void h_alg(u32 ch) { CALLF(tisp_ae_algorithm, ch, 0, 0, 0, 0); }
static void h_trig(u32 ch) { CALLF(tisp_ae_par_update_trig, ch, 0, 0, 0, 0); }
static void h_long_proc(u32 ch) { CALLF(api_ae_long_process, ch, 0, 0, 0, 0); }
static void h_complete(void *d) { CALLF(private_complete, T32O_PTR(d), 0, 0, 0, 0); }
static void h_long(u32 ch) { CALLF(tisp_ae_long_ev_alloc, ch, 0, 0, 0, 0); }
static void h_short(u32 ch) { CALLF(tisp_ae_short_ev_alloc, ch, 0, 0, 0, 0); }
static u32 h_sysrd(u32 reg) { return (u32)CALLF(system_reg_read, reg, 0, 0, 0, 0); }
static void h_sync(const u8 *p, u32 len) { (void)p; (void)len; }
static void h_event(u32 ch, const u32 *w)
{
	memcpy(evbuf, w, sizeof(evbuf));
	CALLF(tisp_event_push, ch, T32O_PTR(evbuf), 0, 0, 0);
}

static void gen_img(void)
{
	u32 i, k, e0[16];
	int sorted = rnd() & 1;
	u32 s576;
	unsigned int mode = rnd() % 3;

	for (i = 0; i < ARENA; i++)
		img[i] = mode ? (u8)rnd() : (u8)(rnd() & 7);
	for (k = 0; k < 16; k++)
		e0[k] = rnd() >> (rnd() % 24);
	if (sorted) {
		for (i = 0; i < 16; i++)
			for (k = 0; k + 1 < 16 - i; k++)
				if (e0[k] > e0[k + 1]) {
					u32 t = e0[k]; e0[k] = e0[k + 1]; e0[k + 1] = t;
				}
	}
	s576 = rnd() >> (rnd() % 28);
	if (sorted && (rnd() & 3)) {
		/* thresholds (e0 << 10) bracketing x, so every segment index is hit */
		int pos = (int)(rnd() % 17) - 1, step = (int)(rnd() % 5);
		long base = (long)(s576 >> 10), v;

		for (k = 0; k < 16; k++) {
			v = base + ((long)k - pos) * step;
			e0[k] = v < 0 ? 0 : (u32)v;
		}
	}
	for (k = 0; k < 16; k++)
		put32(img + OFF_TUNE + 4 * (452 + 3 * k), e0[k]);
	put32(img + OFF_ST + 576, s576);
	put32(img + OFF_ST + 580, rnd() >> (rnd() % 28));
	put32(img + OFF_ST + 588, rnd() >> (rnd() % 28));
	img[OFF_ST + 2182] = rnd() & 1;
	img[OFF_ST + 1297] = rnd() & 1;
	img[OFF_ST + 1696] = rnd() & 1;
	img[OFF_ST + 1285] = rnd() & 1;
	img[OFF_ST + 1286] = rnd() & 1;
	put16(img + OFF_TUNE + 1656, rnd() % 8);
	put16(img + OFF_TUNE + 2774, rnd() % 16);
	put16(img + OFF_TUNE + 2770, rnd() % 16);
	put16(img + OFF_TUNE + 1736, rnd() % 17);
}

static void gen_par(struct par *p, int kind)
{
	p->ch = kind == K_INT ? 0 : (rnd() & 3);
	p->stopped = (rnd() % 5 < 3) ? 0 : (rnd() & 1 ? 1 : 2);
	p->mode = (rnd() % 4 < 3) ? 1 : (rnd() % 3);
	p->cnt0 = rnd() % 20;
	p->dbg = rnd() & 1;
	/* never reach the debug print: dbg_max < cnt0 + 1 */
	p->dbg_max = p->dbg ? p->cnt0 - rnd() % 5 : rnd();
	p->trace = rnd() & 1;
	p->flag = rnd() & 1;
	p->salt = rnd();
}

static void setup(u8 *A, const struct par *p)
{
	u32 ch = p->ch;

	memcpy(A, img, ARENA);
	put32(A + OFF_CTX + 0, T32O_PTR(A + OFF_TUNE));
	put32(A + OFF_CTX + 4, T32O_PTR(A + OFF_BUF));
	put32(A + OFF_CTX + 8, p->flag);
	put32(A + OFF_E98 + 4, T32O_PTR(A + OFF_SRC));
	t32_data_e1588[ch] = T32O_PTR(A + OFF_ST);
	t32_data_e15a8[ch] = T32O_PTR(A + OFF_CTX);
	t32_data_e1598[ch] = T32O_PTR(A + OFF_E98);
	t32_data_e15b8[ch] = p->stopped;
	t32_data_e1510[ch] = (u8)p->mode;
	t32_data_e1500[ch] = p->dbg;
	t32_data_e14f0[ch] = p->dbg_max;
	t32_data_e1578[ch] = p->cnt0;
	t32_data_d9008 = p->trace;
	t32_data_dd2d4[ch * 232 + 123] = (u8)(rd_salt % 8);
	/* let the stored AE value equal the register read (>>16) in half the cases */
	if (p->salt & 0x100) {
		u32 idx = (u32)(img[OFF_TUNE + 1656] | img[OFF_TUNE + 1657] << 8);

		put32(A + OFF_ST + 288 + 4 * idx, rdhook(0x3058) >> 16);
	}
}

static void run_side(int side, int kind, const struct par *p, struct snap *s)
{
	u8 *A;
	u32 ch = p->ch;

	t32o_reset();
	t32o_ev_n = 0;
	memset(t32o_evlog, 0, sizeof(t32o_evlog));
	t32o_complete_n = 0;
	t32o_complete_arg = 0;
	rd_salt = p->salt;
	t32o_rd_hook = rdhook;
	A = t32o_alloc(ARENA);
	if (!side)
		base_a = A;
	else if (A != base_a) {
		fprintf(stderr, "arena address differs between runs\n");
		exit(2);
	}
	setup(A, p);
	s->nw = 0;
	if (!side) {
		u32 a = kind == K_BV ? (u32)CALLF(tisp_ae_get_bv, ch, 0, 0, 0, 0) :
			kind == K_CALC ? (u32)CALLF(tisp_ae_calc_process, ch, 0, 0, 0, 0) :
			(u32)CALLF(tisp_ae_interrupt_static, ch, 0, 0, 0, 0);

		s->ret = a;
	} else if (kind == K_BV) {
		struct t32_ae_bv_ops ops = { h_div32, h_log2, h_log2_64, h_mult3 };

		s->ret = t32_ae_get_bv(A + OFF_TUNE, A + OFF_BUF, A + OFF_ST, &ops);
	} else {
		struct t32_ae_ctx ctx = { A + OFF_TUNE, A + OFF_BUF, 0, 0, 0, 0 };

		if (kind == K_CALC) {
			struct t32_ae_cp_ops ops = { h_rd, h_alg, h_trig, h_long_proc, h_complete };
			struct t32_ae_cp_glob g = { &t32_data_e15b8[ch], &t32_data_e1578[ch], p->dbg,
						    p->dbg_max, p->trace };

			s->nw = t32_ae_calc_process(ch, &ctx, A + OFF_ST, p->flag, &g, A + OFF_CTX + 2008,
						    &ops, s->w);
			s->ret = 0;
		} else {
			struct t32_ae_irq_ops ops = { h_long, h_short, h_sysrd, h_sync, h_event };

			s->ret = t32_ae_interrupt_static(ch, &ctx, (const u32 *)(A + OFF_CTX + 8),
							 t32_data_e15b8[ch], t32_data_e1510[ch],
							 A + OFF_SRC, &ops);
		}
	}
	memcpy(s->mem, A, ARENA);
	s->log = t32o_log;
	s->ev_n = t32o_ev_n;
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->cn = t32o_complete_n;
	s->carg = t32o_complete_arg;
	s->cnt = t32_data_e1578[ch];
	s->stop = t32_data_e15b8[ch];
}

static int is_calc_reg(u32 r)
{
	return r == T32_AE_REG_A || r == T32_AE_REG_B || r == T32_AE_REG_C || r == T32_AE_REG_D ||
	       r == T32_AE_REG_E;
}

static int bad(const char *what, int kind, unsigned int c)
{
	printf("MISMATCH kind %d case %u: %s\n", kind, c, what);
	return 1;
}

static int compare(int kind, unsigned int c)
{
	unsigned int i, j;

	if (sref.ret != sour.ret) {
		printf("  ret ref %08x ours %08x\n", sref.ret, sour.ret);
		return bad("return value", kind, c);
	}
	for (i = 0; i < ARENA; i++)
		if (sref.mem[i] != sour.mem[i]) {
			printf("  arena +%x ref %02x ours %02x\n", i, sref.mem[i], sour.mem[i]);
			return bad("state arena", kind, c);
		}
	if (sref.ev_n != sour.ev_n || memcmp(sref.evlog, sour.evlog, sizeof(sref.evlog)))
		return bad("events", kind, c);
	if (sref.cn != sour.cn || sref.carg != sour.carg)
		return bad("completion", kind, c);
	if (sref.cnt != sour.cnt || sref.stop != sour.stop)
		return bad("per channel words", kind, c);
	if (kind == K_CALC) {
		for (i = 0, j = 0; i < sref.log.n; i++) {
			if (!is_calc_reg(sref.log.w[i].reg))
				continue;
			if (j >= sour.nw || sour.w[j].reg != sref.log.w[i].reg ||
			    sour.w[j].val != sref.log.w[i].val) {
				printf("  write #%u ref %05x=%08x\n", j, sref.log.w[i].reg, sref.log.w[i].val);
				return bad("register writes", kind, c);
			}
			j++;
		}
		if (j != sour.nw)
			return bad("register write count", kind, c);
	} else {
		if (sref.log.n != sour.log.n)
			return bad("register write count", kind, c);
		for (i = 0; i < sref.log.n; i++)
			if (sref.log.w[i].reg != sour.log.w[i].reg || sref.log.w[i].val != sour.log.w[i].val) {
				printf("  write #%u ref %05x=%08x ours %05x=%08x\n", i, sref.log.w[i].reg,
				       sref.log.w[i].val, sour.log.w[i].reg, sour.log.w[i].val);
				return bad("register writes", kind, c);
			}
	}
	return 0;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 3000;
	int kind;
	unsigned int c;

	for (kind = K_BV; kind <= K_INT; kind++) {
		for (c = 0; c < n; c++) {
			struct par p;

			gen_img();
			gen_par(&p, kind);
			run_side(0, kind, &p, &sref);
			run_side(1, kind, &p, &sour);
			ncmp[kind]++;
			if (compare(kind, c))
				return 1;
		}
	}
	printf("t32_aeb_oracle: get_bv %u, calc_process %u, interrupt_static %u compared, 0 mismatches\n",
	       ncmp[K_BV], ncmp[K_CALC], ncmp[K_INT]);
	return 0;
}
