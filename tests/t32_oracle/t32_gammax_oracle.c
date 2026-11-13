// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: GAMMA parameter / API paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_gamma_deinit,
 * _param_array_get, _param_array_set, _api_set, _api_get and our readable
 * t32_gamma_* ports (driver/t32/tx_isp_t32_gamma.h) from the same random state
 * (both start with tisp_gamma_init / t32_gamma_start, then the state is
 * overwritten identically, incl. gs / b12 / cin / b20 / ev / pending / payload
 * capacity) and compares: return value, vendor register writes (must be none,
 * ours writes none), section, payload, API block, gs, b12, cin, ev, state
 * pointers, e1820, info block, da93c, capacity word, pending, output buffer
 * and length (inside canary guards), every private_vfree / private_vmalloc call
 * (count, order, pointer after mapping vendor <-> ours, size), the payload the
 * growth branch allocates, and the LUT written by the following
 * irq_frame_done for every non-deinit case.  Single and multi-record curve ids,
 * record ranges near the exposure, up to 14 records.  Fixed seed, d03a0 = 0.
 *
 * Not covered: the d03a0 == 1 shadow image path (env.c offers no symbol for
 * the vendor's register image, so it cannot be compared here).
 * The oracle supplies weak definitions for symbols env.c may lack (printk,
 * private_vfree / private_vmalloc recording their calls, t32_data_bf754/bf650/
 * bf54c preset curves with distinct dummy bytes, t32_data_da93c/c9618/bf530,
 * tisp_gamma_prepare_write_lut modelled as pending++ at state + 24); a probe
 * at start aborts (rc 2) if a strong definition of the private_* ones hides
 * the recorders.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_gamma.h"

#define SECB		T32_GAMMA_SEC_BYTES
#define SEC_OFF		0x81a8u
#define PLMAX		4096
#define BUFMAX		4096
#define GUARD		64
#define CAP		0x100000u
#define MAXW		T32_GAMMA_MAX_WRITES

extern u64 tisp_gamma_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_api_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_api_get(u32 a0, u32 a1, u32 a2, u32 a3);

/* vendor side call records (filled by the weak private_* below) */
static u32 vf_n, vf_p[16], vm_n, vm_sz[4];

/* weak fallbacks, see the header comment */
__attribute__((weak)) u8 t32_data_bf754[260] = { [0 ... 259] = 0x31 };
__attribute__((weak)) u8 t32_data_bf650[260] = { [0 ... 259] = 0x52 };
__attribute__((weak)) u8 t32_data_bf54c[260] = { [0 ... 259] = 0x73 };
__attribute__((weak)) u16 t32_data_da93c = 0;
__attribute__((weak)) u8 t32_data_c9618[8] = { 0 };
__attribute__((weak)) u8 t32_data_bf530[8] = { 0 };
__attribute__((weak)) u64 printk(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}
__attribute__((weak)) u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (vf_n < 16)
		vf_p[vf_n] = a0;
	vf_n++;
	return 0;
}
__attribute__((weak)) u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (vm_n < 4)
		vm_sz[vm_n] = a0;
	vm_n++;
	return T32O_PTR(t32o_alloc(a0));
}
__attribute__((weak)) u64 tisp_gamma_prepare_write_lut(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u8 *st = T32O_HOST(t32_data_e1834[a0 & 0xff]);

	(void)a1; (void)a2; (void)a3;
	st[24]++;
	return 0;
}

static u32 rs = 0x51c0ffeeu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static void put16(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8);
}

static u32 get32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u32 get16(const u8 *p)
{
	return p[0] | (u32)p[1] << 8;
}

enum { CASE_GET, CASE_SET, CASE_API_SET, CASE_API_GET, CASE_DEINIT, NCASE };
static const char *const names[NCASE] = { "get", "set", "api_set", "api_get", "deinit" };

struct cs {
	int which;
	u32 ch, plbytes, ev_lo, ev_hi, nullmask, cap, pend;
	int nullslot, nullpl;
	u8 initsec[SECB], initlut[258];
	u8 sec[SECB], pl[PLMAX], buf[BUFMAX], b20[264], gs[260], b12[260], cin[258];
};

static unsigned int compared, caseno;

/* our side call records */
static void *o_freed[16];
static unsigned int o_nfreed;
static u32 o_vsz[4], o_nv;
static u8 o_newpl[PLMAX];
static struct t32_gamma *g_st;
static u8 *g_olut;
static u32 g_vctx, g_vpl;

static void rec_vfree(void *p)
{
	if (o_nfreed < 16)
		o_freed[o_nfreed] = p;
	o_nfreed++;
}

static void *host_valloc(u32 size)
{
	if (o_nv < 4)
		o_vsz[o_nv] = size;
	o_nv++;
	return o_newpl;
}

/* our pointer -> the vendor's value of the same object */
static u32 omap(void *p)
{
	if (p == (void *)g_st)
		return g_vctx;
	if (p == (void *)g_olut)
		return g_vpl;
	return (u32)(uintptr_t)p;
}

static int guard_ok(const u8 *p, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (p[i] != 0xcc)
			return 0;
	return 1;
}

static void fail(int which, const char *field)
{
	printf("MISMATCH %s case %u: %s\n", names[which], caseno, field);
	exit(1);
}

#define CHECK(cond, f) do { if (!(cond)) fail(c->which, f); } while (0)

static void run_case(const struct cs *c)
{
	static u8 r_sec[SECB], r_pl[PLMAX], r_buf[BUFMAX], r_b20[264], r_gs[260], r_b12[260], r_cin[258];
	static struct t32_front_write rw[MAXW], iw[MAXW], ow[MAXW], fw[MAXW];
	static struct t32_gamma st;
	static u8 osec[SECB], olutg[PLMAX + GUARD], obufg[BUFMAX + 2 * GUARD], isec[SECB], ilut[258 + 8];
	static u8 infob[64];
	static const u8 *const presets[3] = { t32_data_bf754, t32_data_bf650, t32_data_bf54c };
	u8 *base, *info, *vpl, *buf, *lenp, *ctx, *ev, *b20p, *gsp, *b12p, *cinp, *obuf = obufg + GUARD, *olut = olutg;
	u8 *bufg, *vplg;
	u32 ch = c->ch, r_len = 0, o_len = 0, r_e1820 = 0, r_da93c = 0, r_info = 0, r_cap = 0;
	u32 r_own[5], r_ptr[5], r_vfn = 0, r_vf[16], r_vmn = 0, r_vm0 = 0, r_ret = 0;
	int o_ret = 0, r_fn, o_fn;
	unsigned int r_n0, k, r_pend = 0;
	int inited;

	caseno++;
	/* ---- vendor ---- */
	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	info = t32o_alloc(64);
	vplg = t32o_alloc(PLMAX + GUARD);
	bufg = t32o_alloc(BUFMAX + 2 * GUARD);
	lenp = t32o_alloc(8);
	if (!base || !info || !vplg || !bufg || !lenp) {
		printf("t32_gammax_oracle: pool exhausted\n");
		exit(2);
	}
	vpl = vplg;
	buf = bufg + GUARD;
	memcpy(base + SEC_OFF, c->initsec, SECB);
	memset(vpl, 0, PLMAX);
	memcpy(vpl, c->initlut, 258);
	memset(info, 0, 64);
	put32(info + 32, T32O_PTR(vpl));
	t32_data_dd0a4[ch] = T32O_PTR(base);
	t32_data_dc274[ch] = T32O_PTR(info);
	t32_data_e1820[0] = 258;
	t32_data_d03a0[0] = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_gamma_init, ch, 0, 0, 0, 0);
	ctx = T32O_HOST(t32_data_e1834[ch]);
	if (!ctx || get32(ctx) != T32O_PTR(base + SEC_OFF)) {
		printf("t32_gammax_oracle: unexpected vendor state layout after init\n");
		exit(2);
	}
	memset(base, 0xcc, SEC_OFF);
	memcpy(base + SEC_OFF, c->sec, SECB);
	memset(base + SEC_OFF + SECB, 0xcc, 64);
	memcpy(vpl, c->pl, PLMAX);
	memset(vpl + PLMAX, 0xcc, GUARD);
	t32_data_e1820[0] = c->plbytes;
	for (k = 0; k < 5; k++)
		r_ptr[k] = get32(ctx + 4 + 4 * k);
	gsp = T32O_HOST(r_ptr[0]);
	ev = T32O_HOST(r_ptr[1]);
	b12p = T32O_HOST(r_ptr[2]);
	cinp = T32O_HOST(r_ptr[3]);
	b20p = T32O_HOST(r_ptr[4]);
	put32(ev, c->ev_lo);
	put32(ev + 4, c->ev_hi);
	memcpy(gsp, c->gs, 260);
	memcpy(b12p, c->b12, 260);
	memcpy(cinp, c->cin, 258);
	memcpy(b20p, c->b20, 264);
	ctx[24] = (u8)c->pend;
	put32(t32_data_dc1a4 + ch * 28 + 12, c->cap);
	info[4] = 0x34; info[5] = 0x12;
	if (c->nullpl)
		put32(info + 32, 0);
	memcpy(infob, info, 64);
	t32_data_da93c = 0xdead;
	memset(bufg, 0xcc, GUARD);
	memcpy(buf, c->buf, BUFMAX);
	memset(buf + BUFMAX, 0xcc, GUARD);
	put32(lenp, 0x77777777u);
	put32(lenp + 4, 0x88888888u);
	for (k = 0; k < 5; k++)
		r_own[k] = (c->nullmask >> k & 1) ? 0 : 0x1000 + 16 * k;
	if (c->which == CASE_DEINIT) {
		if (c->nullslot)
			t32_data_e1834[ch] = 0;
		else
			for (k = 0; k < 5; k++)
				put32(ctx + 4 + 4 * k, r_own[k]);
	}
	t32o_log.n = 0;
	vf_n = 0;
	vm_n = 0;
	switch (c->which) {
	case CASE_GET:
		r_ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_gamma_param_array_get, ch, T32O_PTR(buf), T32O_PTR(lenp), 0, 0);
		break;
	case CASE_SET:
		r_ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_gamma_param_array_set, ch, T32O_PTR(buf), 0, 0, 0);
		break;
	case CASE_API_SET:
		r_ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_gamma_api_set, ch, T32O_PTR(buf), 0, 0, 0);
		break;
	case CASE_API_GET:
		r_ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_gamma_api_get, ch, T32O_PTR(buf), 0, 0, 0);
		break;
	case CASE_DEINIT:
		r_ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_gamma_deinit, ch, 0, 0, 0, 0);
		break;
	}
	r_n0 = t32o_log.n;
	if (r_n0 > MAXW)
		exit(2);
	memcpy(rw, t32o_log.w, r_n0 * sizeof(rw[0]));
	r_vfn = vf_n;
	r_vmn = vm_n;
	r_vm0 = vm_sz[0];
	for (k = 0; k < 16; k++)
		r_vf[k] = vf_p[k];
	g_vctx = T32O_PTR(ctx);
	g_vpl = T32O_PTR(vpl);
	if (c->which == CASE_DEINIT) {
		CHECK(t32_data_e1834[ch] == 0, "vendor slot not cleared");
	} else {
		memcpy(r_sec, base + SEC_OFF, SECB);
		memcpy(r_pl, vpl, PLMAX);
		memcpy(r_buf, buf, BUFMAX);
		memcpy(r_b20, b20p, 264);
		memcpy(r_gs, gsp, 260);
		memcpy(r_b12, b12p, 260);
		memcpy(r_cin, cinp, 258);
		r_len = get32(lenp);
		r_e1820 = t32_data_e1820[0];
		r_da93c = t32_data_da93c;
		r_info = get16(info + 4);
		r_cap = get32(t32_data_dc1a4 + ch * 28 + 12);
		r_pend = ctx[24];
		CHECK(guard_ok(base, SEC_OFF), "vendor wrote below the section");
		CHECK(guard_ok(base + SEC_OFF + SECB, 64), "vendor wrote behind the section");
		CHECK(guard_ok(vpl + PLMAX, GUARD), "vendor payload guard");
		CHECK(guard_ok(bufg, GUARD) && guard_ok(buf + BUFMAX, GUARD), "vendor buffer guard");
		CHECK(get32(lenp + 4) == 0x88888888u, "vendor lenp guard");
		CHECK(get32(ctx) == T32O_PTR(base + SEC_OFF), "vendor section pointer");
		for (k = 0; k < 5; k++)
			CHECK(get32(ctx + 4 + 4 * k) == r_ptr[k], "vendor state pointer");
		for (k = 0; k < 64; k++)
			if (k != 4 && k != 5 && (k < 32 || k > 35))
				CHECK(info[k] == infob[k], "info block");
	}
	r_fn = 0;
	if (c->which != CASE_DEINIT) {
		t32o_log.n = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_irq_frame_done, ch, 0, 0, 0, 0);
		r_fn = (int)t32o_log.n;
		if (r_fn > MAXW)
			exit(2);
		memcpy(fw, t32o_log.w, r_fn * sizeof(fw[0]));
	}

	/* ---- ours ---- */
	memcpy(isec, c->initsec, SECB);
	memset(ilut, 0, sizeof(ilut));
	memcpy(ilut, c->initlut, 258);
	inited = t32_gamma_start(&st, isec, ilut, 258, iw);
	if (inited < 0) {
		printf("t32_gammax_oracle: our init refused the initial section\n");
		exit(2);
	}
	memcpy(osec, c->sec, SECB);
	memcpy(olut, c->pl, PLMAX);
	memset(olut + PLMAX, 0xcc, GUARD);
	memset(obufg, 0xcc, GUARD);
	memcpy(obuf, c->buf, BUFMAX);
	memset(obuf + BUFMAX, 0xcc, GUARD);
	st.sec = osec;
	st.pl = c->nullpl ? NULL : olut;
	st.plbytes = c->plbytes;
	st.pl_cap = c->cap;
	st.info_sz = 0x1234;
	st.last_sz = 0xdead;
	st.ev_lo = c->ev_lo;
	st.ev_hi = c->ev_hi;
	st.pending = (u8)c->pend;
	memcpy(st.gs, c->gs, 260);
	memcpy(st.b12, c->b12, 260);
	memcpy(st.cin, c->cin, 258);
	memcpy(st.b20, c->b20, 264);
	g_st = &st;
	g_olut = olut;
	o_nfreed = 0;
	o_nv = 0;
	switch (c->which) {
	case CASE_GET:
		o_ret = t32_gamma_param_array_get(&st, obuf, &o_len);
		break;
	case CASE_SET:
		o_ret = t32_gamma_param_array_set(&st, (u8)ch, obuf, host_valloc, rec_vfree, NULL);
		break;
	case CASE_API_SET:
		o_ret = t32_gamma_api_set(&st, (u8)ch, obuf, presets, NULL);
		break;
	case CASE_API_GET:
		o_ret = t32_gamma_api_get(&st, obuf);
		break;
	case CASE_DEINIT: {
		struct t32_gamma *slot = c->nullslot ? NULL : &st;

		for (k = 0; k < 5; k++)
			st.own[k] = (void *)(uintptr_t)r_own[k];
		o_ret = t32_gamma_deinit(rec_vfree, &slot);
		CHECK(slot == NULL, "our slot not cleared");
		break;
	}
	}
	/* register writes: the vendor writes none (d03a0 = 0), ours has no write path */
	CHECK(r_n0 == 0, "vendor register writes");
	CHECK((u32)o_ret == r_ret, "return value");
	CHECK(r_vfn == o_nfreed, "free count");
	for (k = 0; k < r_vfn && k < 16; k++)
		CHECK(omap(o_freed[k]) == r_vf[k], "free pointer/order");
	CHECK(r_vmn == o_nv, "vmalloc count");
	if (o_nv)
		CHECK(r_vm0 == o_vsz[0], "vmalloc size");
	if (c->which == CASE_DEINIT) {
		compared++;
		return;
	}
	CHECK(guard_ok(olut + PLMAX, GUARD), "our payload guard");
	CHECK(guard_ok(obufg, GUARD) && guard_ok(obuf + BUFMAX, GUARD), "our buffer guard");
	CHECK(!memcmp(r_sec, osec, SECB), "section");
	CHECK(!memcmp(r_pl, olut, PLMAX), "payload");
	CHECK(!memcmp(r_buf, obuf, BUFMAX), "buffer");
	CHECK(!memcmp(r_b20, st.b20, 264), "api block");
	CHECK(!memcmp(r_gs, st.gs, 260), "gs");
	CHECK(!memcmp(r_b12, st.b12, 260), "b12");
	CHECK(!memcmp(r_cin, st.cin, 258), "cin");
	CHECK(get32(ev) == st.ev_lo && get32(ev + 4) == st.ev_hi, "ev");
	if (c->which == CASE_GET)
		CHECK(r_len == o_len, "length");
	else
		CHECK(r_len == 0x77777777u, "length untouched");
	CHECK(r_e1820 == st.plbytes, "e1820");
	CHECK(r_da93c == st.last_sz, "da93c");
	CHECK(r_info == st.info_sz, "info size");
	CHECK(r_cap == st.pl_cap, "payload capacity");
	CHECK(r_pend == st.pending, "pending");
	if (o_nv) {
		u8 *newpv = T32O_HOST(get32(info + 32));

		CHECK(newpv != vpl, "vendor payload not replaced");
		CHECK(st.pl == o_newpl, "our payload not replaced");
		CHECK(!memcmp(newpv, st.pl, (u16)st.info_sz), "grown payload");
	} else {
		CHECK(get32(info + 32) == T32O_PTR(vpl), "vendor payload pointer");
		CHECK(st.pl == olut, "our payload pointer");
	}
	o_fn = t32_gamma_frame_done(&st, ow);
	CHECK(o_fn == r_fn, "lut write count");
	for (k = 0; k < (unsigned int)r_fn; k++)
		CHECK(fw[k].reg == ow[k].reg && fw[k].val == ow[k].val, "lut write");
	CHECK(ctx[24] == st.pending, "pending after frame done");
	compared++;
}

static const u32 plsizes[5] = { 258, 259, 264, 266, 267 };

static void gen_sec(u8 *sec, u32 n, int multi)
{
	u32 x = rnd() % (1u << 20);
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (u8)rnd();
	if (multi) {
		switch (rnd() % 3) {
		case 0:
			put32(sec, T32_GAMMA_MULTI_ID);
			break;
		case 1:
			put32(sec, T32_GAMMA_MULTI_ID + 1 + rnd() % (1u << 20));
			break;
		default:
			put32(sec, 0xffffffffu);
			break;
		}
	} else {
		put32(sec, rnd() % 2 ? 0x134da11 : 0x134ff90);
	}
	sec[0x5a] = (u8)n;
	sec[0x48] = (u8)(rnd() % 3);
	for (i = 0; i < 12; i++) {
		if (rnd() % 4)
			x += rnd() % (1u << 26);
		put32(sec + 0x18 + 4 * i, x);
	}
	sec[T32_GAMMA_API_A] = (u8)(rnd() % 4);
	sec[T32_GAMMA_API_B] = (u8)(rnd() % 4);
	sec[T32_GAMMA_COPY_OUT] = (u8)(rnd() % 3);
	sec[T32_GAMMA_SAVE_PAYLOAD] = (u8)(rnd() % 3);
}

/* sensible, mostly increasing exposure ranges (2 x 2 u16 at +258) in n records */
static void fix_ranges(u8 *pl, u32 n)
{
	u32 x = rnd() % (1u << 28), c;

	for (c = 0; c < n; c++) {
		u32 w = rnd() % 4 ? rnd() % (1u << 26) : 0, lo = x, hi = x + w;
		u8 *r = pl + T32_GAMMA_REC_BYTES * c;

		put16(r + 258, lo);
		put16(r + 260, lo >> 16);
		put16(r + 262, hi);
		put16(r + 264, hi >> 16);
		x = hi + (rnd() % 3 ? rnd() % (1u << 24) : 0);
	}
}

/* a record bound +-1, as an exposure target in [0, 1 << 30] */
static u32 rec_edge(const u8 *pl, u32 n)
{
	const u8 *r = pl + T32_GAMMA_REC_BYTES * (rnd() % n);
	int s = rnd() % 2 ? (int)((get16(r + 260) << 16) + get16(r + 258)) :
			    (int)((get16(r + 264) << 16) + get16(r + 262));

	s += (int)(rnd() % 3) - 1;
	if (s < 0)
		s = 0;
	if (s > (1 << 30))
		s = 1 << 30;
	return (u32)s;
}

static void set_ev(struct cs *c, u32 t)
{
	c->ev_hi = t >> 22;
	c->ev_lo = (t & 0x3fffffu) << 10;
}

int main(void)
{
	static struct cs c;
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int it, i, which;

	/* the private_* recorders of this file must be the ones linked */
	t32o_reset();
	private_vfree(0xfeed, 0, 0, 0);
	private_vmalloc(8, 0, 0, 0);
	if (vf_n != 1 || vm_n != 1 || vf_p[0] != 0xfeed || vm_sz[0] != 8) {
		printf("t32_gammax_oracle: private_vfree / private_vmalloc are not the recording ones\n");
		return 2;
	}

	for (it = 0; it < n; it++) {
		for (which = 0; which < NCASE; which++) {
			u32 nn = rnd() % 4 == 0 ? rnd() % 15 : rnd() % 7;
			int multi = rnd() % 2;

			c.which = (int)which;
			c.ch = rnd() & 3;
			c.plbytes = plsizes[rnd() % 5];
			c.ev_lo = rnd();
			c.ev_hi = rnd() % 257;
			c.nullmask = rnd() % 32;
			c.nullslot = !(rnd() % 8);
			c.nullpl = 0;
			c.cap = CAP;
			c.pend = rnd() % 3;
			gen_sec(c.initsec, 1, 0);
			for (i = 0; i < 258; i++)
				c.initlut[i] = (u8)rnd();
			gen_sec(c.sec, nn, multi);
			for (i = 0; i < PLMAX; i++)
				c.pl[i] = (u8)rnd();
			for (i = 0; i < 264; i++)
				c.b20[i] = (u8)rnd();
			for (i = 0; i < 260; i++) {
				c.gs[i] = (u8)rnd();
				c.b12[i] = (u8)rnd();
			}
			for (i = 0; i < 258; i++)
				c.cin[i] = (u8)rnd();
			if (multi && nn) {
				fix_ranges(c.pl, nn);
				if (rnd() % 2)
					set_ev(&c, rec_edge(c.pl, nn));
			}
			memset(c.buf, 0xa5, sizeof(c.buf));
			if (which == CASE_SET) {
				u32 n2 = rnd() % 4 == 0 ? rnd() % 15 : rnd() % 7, sz = n2 * T32_GAMMA_REC_BYTES;
				int m2 = rnd() % 2;

				gen_sec(c.buf, n2, m2);
				for (i = SECB; i < BUFMAX; i++)
					c.buf[i] = (u8)rnd();
				if (m2 && n2) {
					fix_ranges(c.buf + SECB, n2);
					if (rnd() % 2)
						set_ev(&c, rec_edge(c.buf + SECB, n2));
				}
				switch (rnd() % 6) {
				case 0: c.cap = CAP; break;
				case 1: c.cap = sz; break;
				case 2: c.cap = sz ? sz - 1 : 0; break;
				case 3: c.cap = sz + 1; break;
				case 4: c.cap = 0; break;
				default: c.cap = rnd() % 4000; break;
				}
				if (sz > c.cap && rnd() % 3 == 0)
					c.nullpl = 1;
			} else if (which == CASE_API_SET) {
				u32 v = rnd() % 8 == 7 ? 0xffffffffu : rnd() % 7;

				for (i = 0; i < 4 + 258 + 8; i++)
					c.buf[i] = (u8)rnd();
				put32(c.buf, v);
			}
			run_case(&c);
		}
	}
	printf("t32_gammax_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
