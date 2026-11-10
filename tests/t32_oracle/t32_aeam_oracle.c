// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: ioctl attribute glue of the AE scene /
 * target / only-read attributes (tx_isp_ae_hlc_or_blc_g_attr, tx_isp_ae_target_s/g_attr,
 * tx_isp_ae_only_read_g_attr, the *_isra_27 / 28 / 61 / 59 functions).  Runs the unmodified
 * vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our
 * t32_isra_* from driver/t32/tx_isp_t32_stats.h from the same random state
 * (copy_from_user / copy_to_user / getter failures, user data) and compares the
 * user area (the vendor's scratch block is its own stack: only seen through the
 * block hashes in the call log), the returned value and the ordered log of all
 * stub calls (copies with sizes, isp_printf levels, tisp_[sg]_* calls with a
 * hash of the block).  All of them are recording stubs on both sides.
 * Exit 0 = all equal, 1 = first mismatch, 2 = setup failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define BUFB	1100u
#define UB	1100u
#define LOGN	16

/* printk file / message strings of the vendor functions: only handed to isp_printf (a stub) */
char t32_data_bdc94[4] = "f";
char t32_data_bdccc[4] = "f";
char t32_data_bdd04[4] = "f";
char t32_data_be094[4] = "f";
char t32_data_c4ad4[4] = "f";
char t32_data_c4b2c[4] = "f";
char t32_data_c4bdc[4] = "f";
char t32_data_c4c00[4] = "f";
char t32_data_c4c2c[4] = "f";
char t32_data_c4c50[4] = "f";
char t32_data_c4c74[4] = "f";
char t32_data_c4c98[4] = "f";
char t32_data_c5008[4] = "f";
char t32_data_c502c[4] = "f";

extern u64 tx_isp_ae_hlc_or_blc_g_attr_isra_27(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tx_isp_ae_target_s_attr_isra_28(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tx_isp_ae_target_g_attr_isra_61(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tx_isp_ae_only_read_g_attr_isra_59(u32 a0, u32 a1, u32 a2, u32 a3);

enum { C_HLC, C_TS, C_TG, C_OR, C_N };
static const char *const names[C_N] = { "hlc_or_blc_g", "target_s", "target_g", "only_read_g" };

struct logrec {
	u32 n, kind[LOGN], a[LOGN], b[LOGN], h[LOGN];
};
static struct logrec lg;
static u32 cur, fail_cfu, fail_ctu, fail_call, pat;
static u8 *ubase;

static u32 hash(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static void rec(u32 kind, u32 a, u32 b, u32 h)
{
	if (lg.n < LOGN) {
		lg.kind[lg.n] = kind; lg.a[lg.n] = a; lg.b[lg.n] = b; lg.h[lg.n] = h;
	}
	lg.n++;
}

u64 private_copy_from_user(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	rec(1, a2, a1 - T32O_PTR(ubase), 0);
	if (fail_cfu)
		return fail_cfu;
	memcpy(T32O_HOST(a0), T32O_HOST(a1), a2);
	return 0;
}

u64 private_copy_to_user(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	rec(2, a2, a0 - T32O_PTR(ubase), hash(T32O_HOST(a1), a2));
	if (fail_ctu)
		return fail_ctu;
	memcpy(T32O_HOST(a0), T32O_HOST(a1), a2);
	return 0;
}

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rec(3, a0, 0, 0);
	return 0;
}

static u32 call_idx, fail_mask, scn_a, scn_m, scn_b, scn_k;

static u64 callee(u32 id, u32 a0, u32 a1, u32 n, int is_get)
{
	u8 *b = T32O_HOST(a1);
	u32 i;

	if (is_get)
		for (i = 0; i < n; i++)
			b[i] = (u8)(pat * 31 + i * 7 + (i >> 3) + id * 13);
	if (is_get && id == 5 && cur == C_HLC) {	/* tisp_g_ae_scence_attr: the hlc / blc state words */
		*(u32 *)(b + 0) = scn_a; b[4] = (u8)scn_m; *(u32 *)(b + 8) = scn_b; b[12] = (u8)scn_k;
	}
	rec(4, id << 8 | a0, 0, hash(b, n));
	return (fail_mask >> call_idx++) & 1 ? 0xffffffeau : 0;
}

u64 tisp_g_ae_scence_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return callee(5, a0, a1, 40, 1); }
u64 tisp_s_ae_scence_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return callee(6, a0, a1, 40, 0); }
u64 tisp_s_at_list(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return callee(7, a0, a1, 128, 0); }
u64 tisp_s_ev_list(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return callee(8, a0, a1, 128, 0); }
u64 tisp_g_at_list(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return callee(9, a0, a1, 128, 1); }
u64 tisp_g_ev_list(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return callee(10, a0, a1, 128, 1); }
u64 tisp_g_ae_exprinfo_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return callee(11, a0, a1, 220, 1); }
u64 tisp_g_bv(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return callee(12, a0, a1, 4, 1); }
u64 tisp_get_ae_stable(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return callee(13, a0, a1, 12, 1); }

static int h_cfu(void *priv, u8 *dst, u32 user, u32 n) { (void)priv; return (int)private_copy_from_user(T32O_PTR(dst), user, n, 0); }
static int h_ctu(void *priv, u32 user, const u8 *src, u32 n) { (void)priv; return (int)private_copy_to_user(user, T32O_PTR(src), n, 0); }
static void h_printk(void *priv, u32 level) { (void)priv; isp_printf(level, 0, 0, 0); }
static int h_calln(void *priv, u32 id, u32 ch, u8 *buf)
{
	u32 p = T32O_PTR(buf);
	u64 r = 0;

	(void)priv;
	switch (cur << 8 | id) {
	case C_HLC << 8 | 1: r = tisp_g_ae_scence_attr(ch, p, 0, 0); break;
	case C_TS << 8 | 1: r = tisp_g_ae_scence_attr(ch, p, 0, 0); break;
	case C_TS << 8 | 2: r = tisp_s_ae_scence_attr(ch, p, 0, 0); break;
	case C_TS << 8 | 3: r = tisp_s_at_list(ch, p, 0, 0); break;
	case C_TS << 8 | 4: r = tisp_s_ev_list(ch, p, 0, 0); break;
	case C_TG << 8 | 1: r = tisp_g_ae_scence_attr(ch, p, 0, 0); break;
	case C_TG << 8 | 2: r = tisp_g_at_list(ch, p, 0, 0); break;
	case C_TG << 8 | 3: r = tisp_g_ev_list(ch, p, 0, 0); break;
	case C_OR << 8 | 1: r = tisp_g_ae_exprinfo_attr(ch, p, 0, 0); break;
	case C_OR << 8 | 2: r = tisp_g_ae_scence_attr(ch, p, 0, 0); break;
	case C_OR << 8 | 3: r = tisp_g_bv(ch, p, 0, 0); break;
	case C_OR << 8 | 4: r = tisp_get_ae_stable(ch, p, 0, 0); break;
	}
	return (int)r;
}

static u32 rs = 0x7f4a7c15u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct snap {
	u8 buf[BUFB], user[UB];
	u32 ret;
	struct logrec lg;
};
static struct snap sr, so;
static u8 d_user[UB];

static void take(struct snap *s, const u8 *buf, u32 ret)
{
	memcpy(s->buf, buf, BUFB);
	memcpy(s->user, ubase, UB);
	s->ret = ret;
	s->lg = lg;
}

static int run_case(unsigned int which, u32 ch)
{
	const struct t32_isra_ops ops = { h_cfu, h_ctu, h_printk, NULL, NULL, NULL, h_calln };
	u8 *buf, *argp, *loc, *b2, *b3, *b4, *b5;
	u32 ret = 0;
	const char *what = NULL;

	
	cur = which;
	t32o_reset();
	buf = t32o_alloc(BUFB);
	loc = t32o_alloc(BUFB);
	b2 = t32o_alloc(BUFB);
	b3 = t32o_alloc(BUFB);
	b4 = t32o_alloc(BUFB);
	b5 = t32o_alloc(BUFB);
	ubase = t32o_alloc(UB);
	argp = t32o_alloc(16);
	if (!buf || !loc || !b2 || !b3 || !b4 || !b5 || !ubase || !argp)
		return 2;
	memset(buf, 0, BUFB);
	memset(loc, 0, BUFB);
	memset(b2, 0, BUFB);
	memset(b3, 0, BUFB);
	memset(b4, 0, BUFB);
	memset(b5, 0, BUFB);
	call_idx = 0;
	memcpy(ubase, d_user, UB);
	*(u32 *)argp = T32O_PTR(ubase);
	memset(&lg, 0, sizeof(lg));
	switch (which) {
	case C_HLC: ret = (u32)t32o_call((t32o_fn)(void (*)(void))tx_isp_ae_hlc_or_blc_g_attr_isra_27, ch, T32O_PTR(argp), 0, 0, 0); break;
	case C_TS: ret = (u32)t32o_call((t32o_fn)(void (*)(void))tx_isp_ae_target_s_attr_isra_28, ch, T32O_PTR(ubase), 0, 0, 0); break;
	case C_TG: ret = (u32)t32o_call((t32o_fn)(void (*)(void))tx_isp_ae_target_g_attr_isra_61, ch, T32O_PTR(argp), 0, 0, 0); break;
	case C_OR: ret = (u32)t32o_call((t32o_fn)(void (*)(void))tx_isp_ae_only_read_g_attr_isra_59, ch, T32O_PTR(argp), 0, 0, 0); break;
	}
	take(&sr, buf, ret);

	t32o_reset();
	buf = t32o_alloc(BUFB);
	loc = t32o_alloc(BUFB);
	b2 = t32o_alloc(BUFB);
	b3 = t32o_alloc(BUFB);
	b4 = t32o_alloc(BUFB);
	b5 = t32o_alloc(BUFB);
	ubase = t32o_alloc(UB);
	argp = t32o_alloc(16);
	if (!buf || !loc || !b2 || !b3 || !b4 || !b5 || !ubase || !argp)
		return 2;
	memset(buf, 0, BUFB);
	memset(loc, 0, BUFB);
	memset(b2, 0, BUFB);
	memset(b3, 0, BUFB);
	memset(b4, 0, BUFB);
	memset(b5, 0, BUFB);
	call_idx = 0;
	memcpy(ubase, d_user, UB);
	*(u32 *)argp = T32O_PTR(ubase);
	memset(&lg, 0, sizeof(lg));
	switch (which) {
	case C_HLC: ret = (u32)t32_isra_ae_hlc_or_blc_g(&ops, ch, *(u32 *)argp, loc, buf); break;
	case C_TS: ret = (u32)t32_isra_ae_target_s(&ops, ch, T32O_PTR(ubase), buf, loc, b2, b3); break;
	case C_TG: ret = (u32)t32_isra_ae_target_g(&ops, ch, *(u32 *)argp, loc, b2, b3, buf); break;
	case C_OR: ret = (u32)t32_isra_ae_only_read_g(&ops, ch, *(u32 *)argp, loc, b2, b3, b4, buf); break;
	}
	take(&so, buf, ret);

	if (so.ret != sr.ret)
		what = "return value";
	if (!what && memcmp(&so.lg, &sr.lg, sizeof(sr.lg)))
		what = "stub call log";
	if (!what && memcmp(so.user, sr.user, UB))
		what = "user area";
	if (what) {
		printf("MISMATCH %s: %s\n", names[which], what);
		return 1;
	}
	return 0;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % C_N;
		u32 i, r;
		int rc;

		for (i = 0; i < UB; i++)
			d_user[i] = (u8)(rnd() >> 11);
		r = rnd() % 4;
		fail_cfu = r == 0 ? (rnd() | 1) : 0;
		r = rnd() % 4;
		fail_ctu = r == 0 ? (rnd() | 1) : 0;
		r = rnd() % 4;
		fail_call = r == 0 ? (rnd() | 1) : 0;
		pat = rnd();
		fail_mask = (rnd() & 3) ? 0 : rnd();
		fail_mask = (rnd() & 3) ? fail_mask : (1u << (rnd() % 5));
		r = rnd() % 4; scn_a = r == 0 ? 3 : rnd() % 5; r = rnd() % 4; scn_b = r == 0 ? 3 : rnd() % 5;
		scn_m = (rnd() & 1) ? 0 : 1 + rnd() % 3; scn_k = (rnd() & 1) ? 0 : 1 + rnd() % 3;
		rc = run_case(which, rnd() & 3);
		if (rc) {
			printf("t32_aeam_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aeam_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
