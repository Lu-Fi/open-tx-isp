// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: GAMMA parameter / API paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_gamma_deinit,
 * _param_array_get, _param_array_set, _api_set, _api_get and our readable
 * t32_gamma_* ports (driver/t32/tx_isp_t32_gamma.h) from the same initial state
 * (both start with tisp_gamma_init / t32_gamma_start, then the test state is
 * overwritten identically) and compares: return value, every register write,
 * section, payload, API block, e1820, payload info size, da93c, the output
 * buffer and length, the pending counter, and the LUT written by the following
 * irq_frame_done (which shows the curve state).  Fixed seed, d03a0 = 0.
 *
 * Not covered: the vmalloc growth branch of param_array_set (payload capacity is
 * set large; env's private_vmalloc is not known), the d03a0 == 1 shadow image
 * path, and the multi-record curve form (single-curve ids only).
 * The oracle supplies weak fallbacks for symbols env.c may lack (printk,
 * private_vfree/vmalloc, t32_data_bf754/bf650/bf54c preset curves with dummy
 * bytes, t32_data_da93c/c9618/bf530, tisp_gamma_prepare_write_lut modelled as
 * pending++ at state + 24); a strong definition in env.c / ref wins.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_gamma.h"

#define SECB		T32_GAMMA_SEC_BYTES
#define SEC_OFF		0x81a8u
#define PLMAX		2048
#define BUFMAX		2048
#define CAP		0x100000u
#define MAXW		T32_GAMMA_MAX_WRITES

extern u64 tisp_gamma_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_api_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gamma_api_get(u32 a0, u32 a1, u32 a2, u32 a3);

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
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}
__attribute__((weak)) u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
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
	u32 ch, plbytes, ev_lo, ev_hi, nullmask;
	int nullslot;
	u8 initsec[SECB], initlut[258];
	u8 sec[SECB], pl[PLMAX], buf[BUFMAX], b20[264];
};

static unsigned int compared, caseno;
static void *freed[8];
static unsigned int nfreed;

static void rec_vfree(void *p)
{
	if (nfreed < 8)
		freed[nfreed] = p;
	nfreed++;
}

static void *host_valloc(u32 size)
{
	(void)size;
	return NULL;	/* never reached: the payload capacity is large */
}

static void fail(int which, const char *field)
{
	printf("MISMATCH %s case %u: %s\n", names[which], caseno, field);
	exit(1);
}

#define CHECK(cond, f) do { if (!(cond)) fail(c->which, f); } while (0)

static void run_case(const struct cs *c)
{
	static u8 r_sec[SECB], r_pl[PLMAX], r_buf[BUFMAX], r_b20[264];
	static struct t32_front_write rw[MAXW], iw[MAXW], ow[MAXW], fw[MAXW];
	static struct t32_gamma st;
	static u8 osec[SECB], olut[PLMAX], obuf[BUFMAX], isec[SECB], ilut[258 + 8];
	static const u8 *const presets[3] = { t32_data_bf754, t32_data_bf650, t32_data_bf54c };
	u8 *base, *info, *vpl, *buf, *lenp, *ctx, *ev, *b20p;
	u32 ch = c->ch, r_len = 0, o_len = 0, r_e1820 = 0, r_da93c = 0, r_info = 0;
	int r_ret = 0, o_ret = 0, r_fn, o_fn;
	unsigned int r_n0, k, r_pend = 0;
	int inited;

	caseno++;
	/* ---- vendor ---- */
	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	info = t32o_alloc(64);
	vpl = t32o_alloc(PLMAX);
	buf = t32o_alloc(BUFMAX);
	lenp = t32o_alloc(8);
	if (!base || !info || !vpl || !buf || !lenp) {
		printf("t32_gammax_oracle: pool exhausted\n");
		exit(2);
	}
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
	memcpy(base + SEC_OFF, c->sec, SECB);
	memcpy(vpl, c->pl, PLMAX);
	t32_data_e1820[0] = c->plbytes;
	ev = T32O_HOST(get32(ctx + 8));
	put32(ev, c->ev_lo);
	put32(ev + 4, c->ev_hi);
	b20p = T32O_HOST(get32(ctx + 20));
	memcpy(b20p, c->b20, 264);
	ctx[24] = 0;
	put32(t32_data_dc1a4 + ch * 28 + 12, CAP);
	info[4] = 0x34; info[5] = 0x12;
	t32_data_da93c = 0xdead;
	memcpy(buf, c->buf, BUFMAX);
	put32(lenp, 0x77777777u);
	if (c->which == CASE_DEINIT) {
		if (c->nullslot)
			t32_data_e1834[ch] = 0;
		else
			for (k = 0; k < 5; k++)
				if (c->nullmask >> k & 1)
					put32(ctx + 4 + 4 * k, 0);
	}
	t32o_log.n = 0;
	switch (c->which) {
	case CASE_GET:
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_param_array_get, ch, T32O_PTR(buf), T32O_PTR(lenp), 0, 0);
		break;
	case CASE_SET:
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_param_array_set, ch, T32O_PTR(buf), 0, 0, 0);
		break;
	case CASE_API_SET:
		r_ret = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_gamma_api_set, ch, T32O_PTR(buf), 0, 0, 0);
		break;
	case CASE_API_GET:
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_api_get, ch, T32O_PTR(buf), 0, 0, 0);
		break;
	case CASE_DEINIT:
		t32o_call((t32o_fn)(void (*)(void))tisp_gamma_deinit, ch, 0, 0, 0, 0);
		break;
	}
	r_n0 = t32o_log.n;
	if (r_n0 > MAXW)
		exit(2);
	memcpy(rw, t32o_log.w, r_n0 * sizeof(rw[0]));
	if (c->which == CASE_DEINIT) {
		CHECK(t32_data_e1834[ch] == 0, "vendor slot not cleared");
	} else {
		memcpy(r_sec, base + SEC_OFF, SECB);
		memcpy(r_pl, vpl, PLMAX);
		memcpy(r_buf, buf, BUFMAX);
		memcpy(r_b20, T32O_HOST(get32(ctx + 20)), 264);
		r_len = get32(lenp);
		r_e1820 = t32_data_e1820[0];
		r_da93c = t32_data_da93c;
		r_info = get16(info + 4);
		r_pend = ctx[24];
	}
	r_fn = 0;
	if (c->which == CASE_SET || c->which == CASE_API_SET) {
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
	memcpy(obuf, c->buf, BUFMAX);
	st.sec = osec;
	st.pl = olut;
	st.plbytes = c->plbytes;
	st.pl_cap = CAP;
	st.info_sz = 0x1234;
	st.last_sz = 0xdead;
	st.ev_lo = c->ev_lo;
	st.ev_hi = c->ev_hi;
	st.pending = 0;
	memcpy(st.b20, c->b20, 264);
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
		void *exp[6];
		unsigned int ne = 0;
		static const u8 order[5] = { 0, 2, 1, 3, 4 };

		for (k = 0; k < 5; k++)
			st.own[k] = (c->nullmask >> k & 1) ? NULL : (void *)(uintptr_t)(0x1000 + 16 * k);
		nfreed = 0;
		o_ret = t32_gamma_deinit(rec_vfree, &slot);
		if (!c->nullslot) {
			for (k = 0; k < 5; k++)
				if (st.own[order[k]])
					exp[ne++] = st.own[order[k]];
			exp[ne++] = &st;
		}
		CHECK(slot == NULL, "our slot not cleared");
		CHECK(nfreed == ne, "free count");
		for (k = 0; k < ne; k++)
			CHECK(freed[k] == exp[k], "free order");
		CHECK(r_n0 == 0, "vendor register writes");
		compared++;
		return;
	}
	}
	CHECK(r_n0 == 0, "vendor register writes");
	CHECK(o_ret == (c->which == CASE_API_SET ? r_ret : 0), "return value");
	CHECK(!memcmp(r_sec, osec, SECB), "section");
	CHECK(!memcmp(r_pl, olut, PLMAX), "payload");
	CHECK(!memcmp(r_buf, obuf, BUFMAX), "buffer");
	CHECK(!memcmp(r_b20, st.b20, 264), "api block");
	if (c->which == CASE_GET)
		CHECK(r_len == o_len, "length");
	CHECK(r_e1820 == st.plbytes, "e1820");
	CHECK(r_da93c == st.last_sz, "da93c");
	CHECK(r_info == st.info_sz, "info size");
	CHECK(r_pend == st.pending, "pending");
	o_fn = 0;
	if (c->which == CASE_SET || c->which == CASE_API_SET) {
		o_fn = t32_gamma_frame_done(&st, ow);
		CHECK(o_fn == r_fn, "lut write count");
		for (k = 0; k < (unsigned int)r_fn; k++)
			CHECK(fw[k].reg == ow[k].reg && fw[k].val == ow[k].val, "lut write");
	}
	compared++;
}

static const u32 plsizes[5] = { 258, 259, 264, 266, 267 };

static void gen_sec(u8 *sec, u32 n)
{
	u32 x = rnd() % (1u << 20);
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (u8)rnd();
	put32(sec, rnd() % 2 ? 0x134da11 : 0x134ff90);
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

int main(void)
{
	static struct cs c;
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int it, i, which;

	for (it = 0; it < n; it++) {
		for (which = 0; which < NCASE; which++) {
			c.which = (int)which;
			c.ch = rnd() & 3;
			c.plbytes = plsizes[rnd() % 5];
			c.ev_lo = rnd();
			c.ev_hi = rnd() % 257;
			c.nullmask = rnd() % 32;
			c.nullslot = !(rnd() % 8);
			gen_sec(c.initsec, 1);
			for (i = 0; i < 258; i++)
				c.initlut[i] = (u8)rnd();
			gen_sec(c.sec, rnd() % 7);
			for (i = 0; i < PLMAX; i++)
				c.pl[i] = (u8)rnd();
			for (i = 0; i < 264; i++)
				c.b20[i] = (u8)rnd();
			memset(c.buf, 0xa5, sizeof(c.buf));
			if (which == CASE_SET) {
				gen_sec(c.buf, rnd() % 7);
				for (i = SECB; i < BUFMAX; i++)
					c.buf[i] = (u8)rnd();
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
