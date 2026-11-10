// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: CSC per-frame paths.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our t32_csc_* port from
 * driver/t32/tx_isp_t32_front.h.
 *
 * Compared: sync_api_para (section and API image bytes), param_array_get
 * (buffer, length), get_full_range (value), deinit (slot, context fields,
 * vfree order), and for param_array_set, api_set and wake_up the register
 * writes (address, value, order) and the working block (+0..+39).  The
 * section / API bytes after set and api_set are not compared (the port does
 * not build the inverse matrix / second range group, which feed no
 * register).  Wake: both sides are primed with their param_array_set first.
 * Modes are kept 0..5.  Exits at the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_front.h"

#define SECB		T32_CSC_BYTES
#define APIB		T32_CSC_API_BYTES
#define SEC_OFF		27488u
#define BLK		256u
#define MAXW		64

extern u32 t32_data_e3110[4];

extern u64 tisp_csc_sync_api_para(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_csc_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_csc_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_csc_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_csc_api_set(u32 a0, u32 a1, u32 a2, u32 a3);
/* tisp_csc_wake_up is a tail call to tisp_csc_write_reg(ch & 255); the ref file
 * does not compile with a C23 compiler (unprototyped callee), so call the callee. */
extern u64 tisp_csc_write_reg(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_csc_get_full_range(u32 a0, u32 a1, u32 a2, u32 a3);

static void *vlog[8];
static unsigned int nvlog;
static void vfree_log(void *p)
{
	if (nvlog < 8)
		vlog[nvlog] = p;
	nvlog++;
}

static u32 rs = 0x3c9e17u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared;

enum { CASE_SYNC1, CASE_SYNC0, CASE_GET, CASE_SET, CASE_API, CASE_WAKE, CASE_FULL };

static void gen_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (u8)rnd();
	sec[0x5b] = (u8)(rnd() % 6);
	if (rnd() & 1)
		sec[100] = 1;
	if (rnd() & 1)
		sec[101] = 1;
}

static void gen_api(u8 *api, int small_mode)
{
	unsigned int i;

	for (i = 0; i < APIB; i++)
		api[i] = (u8)rnd();
	if (small_mode) {
		api[0] = (u8)(rnd() % 6);
		api[1] = api[2] = api[3] = 0;
	}
}

static u8 dbg_sec[SECB];
/* Y offset diagnostics: inputs and vendor state after the call */
static u8 dbg_api_in[APIB], dbg_sec_ref[SECB], dbg_api_ref[APIB], dbg_full_ref[40];
static u32 dbg_fv, dbg_ch;

/* wake diagnostics: vendor state around the priming set and the write_reg */
static u8 dbg_full_pre[48], dbg_full_set[48], dbg_full_wake[48];
static u8 dbg_sec_wake[SECB], dbg_api_wake[APIB];
static struct t32_front_write dbg_setw[MAXW];
static unsigned int dbg_setn, dbg_wake;

static void dump(const char *name, const u8 *p, unsigned int n)
{
	unsigned int i;

	printf("MISMATCH wake-diag %s ", name);
	for (i = 0; i < n; i++)
		printf("%02x", p[i]);
	printf("\n");
}

static unsigned int nfail;

static void fail(const char *what, const char *why, unsigned int rn, int on,
		 const struct t32_front_write *rw, const struct t32_front_write *ow)
{
	unsigned int i;

	++nfail;
	printf("MISMATCH %s: %s (writes ref %u ours %d)\n", what, why, rn, on);
	if (dbg_wake) {
		int same_set = dbg_setn == (unsigned int)on;

		for (i = 0; same_set && i < dbg_setn; i++)
			if (dbg_setw[i].reg != ow[i].reg || dbg_setw[i].val != ow[i].val)
				same_set = 0;
		printf("MISMATCH wake-diag priming set writes %s ours (n %u)", same_set ? "EQUAL" : "DIFFER from",
		       dbg_setn);
		for (i = 0; i < dbg_setn && i < 8; i++)
			printf(" %05x=%08x", dbg_setw[i].reg, dbg_setw[i].val);
		printf("\n");
		dump("full-before", dbg_full_pre, 48);
		dump("full-after-set", dbg_full_set, 48);
		dump("full-after-wake", dbg_full_wake, 48);
		dump("sec-in", dbg_sec, SECB);
		dump("sec-after-wake", dbg_sec_wake, SECB);
		dump("api-after-wake", dbg_api_wake, APIB);
	}
	printf("  mode %u sec[92..97] %02x %02x %02x %02x %02x %02x sec[99..101] %02x %02x %02x\n",
	       dbg_sec[0x5b], dbg_sec[92], dbg_sec[93], dbg_sec[94], dbg_sec[95], dbg_sec[96],
	       dbg_sec[97], dbg_sec[99], dbg_sec[100], dbg_sec[101]);
	printf("  ch %u full+36 in %08x\n", dbg_ch, dbg_fv);
	printf("  sec-in[76..101]");
	for (i = 76; i < 102; i++)
		printf(" %02x", dbg_sec[i]);
	printf("\n  api-in[0..3] %02x %02x %02x %02x api-in[40..45] %02x %02x %02x %02x %02x %02x\n",
	       dbg_api_in[0], dbg_api_in[1], dbg_api_in[2], dbg_api_in[3], dbg_api_in[40],
	       dbg_api_in[41], dbg_api_in[42], dbg_api_in[43], dbg_api_in[44], dbg_api_in[45]);
	printf("  vendor sec[82..87] %02x %02x %02x %02x %02x %02x api[40..45] %02x %02x %02x %02x %02x %02x"
	       " full[0x1c..0x21] %02x %02x %02x %02x %02x %02x\n",
	       dbg_sec_ref[82], dbg_sec_ref[83], dbg_sec_ref[84], dbg_sec_ref[85], dbg_sec_ref[86],
	       dbg_sec_ref[87], dbg_api_ref[40], dbg_api_ref[41], dbg_api_ref[42], dbg_api_ref[43],
	       dbg_api_ref[44], dbg_api_ref[45], dbg_full_ref[0x1c], dbg_full_ref[0x1d],
	       dbg_full_ref[0x1e], dbg_full_ref[0x1f], dbg_full_ref[0x20], dbg_full_ref[0x21]);
	for (i = 0; i < rn || (int)i < on; i++)
		if (i >= rn || (int)i >= on || rw[i].reg != ow[i].reg || rw[i].val != ow[i].val) {
			printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
			       i < rn ? rw[i].reg : 0, i < rn ? rw[i].val : 0,
			       (int)i < on ? ow[i].reg : 0, (int)i < on ? ow[i].val : 0);
			if (i > 8)
				break;
		}
	/* stop at the first mismatch */
	printf("t32_cscx_oracle: %u compared, %u mismatches\n", compared, nfail);
	exit(1);
}

static void run_case(const char *what, int which, u32 ch, const u8 *section,
		     const u8 *api_in, u32 full_value)
{
	u8 *base_r, *sec_r, *api_r, *full_r, *ctx_r;
	u8 *base_o, *sec_o, *api_o, *full_o;
	struct t32_csc_ctx *ctx_o;
	u8 sec_ar[SECB], api_ar[APIB], sec_ao[SECB], api_ao[APIB];
	u8 full_ar[40], full_ao[40];
	u8 *getbuf_r = NULL, getbuf_o[SECB];
	u32 *len_rp = NULL, len_r = 0, len_o = 0, fr = 0, fo = 0;
	struct t32_front_write rw[MAXW] = {{0, 0}}, ow[MAXW] = {{0, 0}};
	unsigned int rn = 0, i;
	int on = 0;

	memcpy(dbg_sec, section, SECB);
	memcpy(dbg_api_in, api_in, APIB);
	dbg_fv = full_value;
	dbg_ch = ch;
	dbg_wake = 0;
	/* vendor side */
	t32o_reset();
	base_r = t32o_alloc(SEC_OFF + SECB + 64);
	api_r = t32o_alloc(BLK);
	full_r = t32o_alloc(BLK);
	ctx_r = t32o_alloc(12);
	if (!base_r || !api_r || !full_r || !ctx_r)
		exit(2);
	memset(api_r, 0, BLK);
	memset(full_r, 0, BLK);
	memcpy(base_r + SEC_OFF, section, SECB);
	sec_r = base_r + SEC_OFF;
	memcpy(api_r, api_in, APIB);
	*(u32 *)(full_r + 36) = full_value;
	*(u32 *)(ctx_r + 0) = T32O_PTR(sec_r);
	*(u32 *)(ctx_r + 4) = T32O_PTR(full_r);
	*(u32 *)(ctx_r + 8) = T32O_PTR(api_r);
	t32_data_e3110[ch] = T32O_PTR(ctx_r);
	t32_data_dd0a4[ch] = T32O_PTR(base_r);

	switch (which) {
	case CASE_SYNC1:
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_sync_api_para, ch, 1, 0, 0, 0);
		break;
	case CASE_SYNC0:
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_sync_api_para, ch, 0, 0, 0, 0);
		break;
	case CASE_GET:
		getbuf_r = t32o_alloc(SECB);
		len_rp = t32o_alloc(4);
		if (!getbuf_r || !len_rp)
			exit(2);
		memset(getbuf_r, 0, SECB);
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_param_array_get, ch,
			  T32O_PTR(getbuf_r), T32O_PTR(len_rp), 0, 0);
		memcpy(&len_r, len_rp, 4);
		break;
	case CASE_SET: {
		u8 *in = t32o_alloc(SECB);

		if (!in)
			exit(2);
		memcpy(in, section, SECB);
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_param_array_set, ch,
			  T32O_PTR(in), 0, 0, 0);
		break;
	}
	case CASE_API: {
		u8 *in = t32o_alloc(APIB);

		if (!in)
			exit(2);
		memcpy(in, api_in, APIB);
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_api_set, ch,
			  T32O_PTR(in), 0, 0, 0);
		break;
	}
	case CASE_WAKE: {
		u8 *in = t32o_alloc(SECB);

		if (!in)
			exit(2);
		memcpy(in, section, SECB);
		dbg_wake = 1;
		memcpy(dbg_full_pre, full_r, 48);
		/* prime the vendor working block, then drop the priming writes */
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_param_array_set, ch,
			  T32O_PTR(in), 0, 0, 0);
		dbg_setn = t32o_log.n < MAXW ? t32o_log.n : MAXW;
		memcpy(dbg_setw, t32o_log.w, dbg_setn * sizeof(dbg_setw[0]));
		memcpy(dbg_full_set, full_r, 48);
		t32o_log.n = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_csc_write_reg, ch, 0, 0, 0, 0);
		memcpy(dbg_full_wake, full_r, 48);
		memcpy(dbg_sec_wake, sec_r, SECB);
		memcpy(dbg_api_wake, api_r, APIB);
		break;
	}
	case CASE_FULL:
		fr = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_csc_get_full_range,
				    ch, 0, 0, 0, 0);
		break;
	}
	rn = t32o_log.n;
	if (rn > MAXW)
		exit(2);
	memcpy(rw, t32o_log.w, rn * sizeof(rw[0]));
	memcpy(sec_ar, sec_r, SECB);
	memcpy(api_ar, api_r, APIB);
	memcpy(full_ar, full_r, 40);
	memcpy(dbg_sec_ref, sec_ar, SECB);
	memcpy(dbg_api_ref, api_ar, APIB);
	memcpy(dbg_full_ref, full_ar, 40);

	/* our side */
	t32o_reset();
	base_o = t32o_alloc(SEC_OFF + SECB + 64);
	api_o = t32o_alloc(BLK);
	full_o = t32o_alloc(BLK);
	ctx_o = t32o_alloc(sizeof(*ctx_o));
	if (!base_o || !api_o || !full_o || !ctx_o)
		exit(2);
	memset(api_o, 0, BLK);
	memset(full_o, 0, BLK);
	memcpy(base_o + SEC_OFF, section, SECB);
	sec_o = base_o + SEC_OFF;
	memcpy(api_o, api_in, APIB);
	*(u32 *)(full_o + 36) = full_value;
	ctx_o->sec = sec_o;
	ctx_o->full = full_o;
	ctx_o->api = api_o;

	switch (which) {
	case CASE_SYNC1:
		t32_csc_sync_api_para(ctx_o, 1);
		break;
	case CASE_SYNC0:
		t32_csc_sync_api_para(ctx_o, 0);
		break;
	case CASE_GET:
		t32_csc_param_array_get(ctx_o, getbuf_o, &len_o);
		break;
	case CASE_SET:
		on = t32_csc_param_array_set(ctx_o, section, ow);
		break;
	case CASE_API:
		on = t32_csc_api_set(ctx_o, api_in, ow);
		break;
	case CASE_WAKE:
		t32_csc_param_array_set(ctx_o, section, ow);
		on = t32_csc_wake_up(ctx_o, ow);
		break;
	case CASE_FULL:
		fo = t32_csc_get_full_range(ctx_o);
		break;
	}
	memcpy(sec_ao, sec_o, SECB);
	memcpy(api_ao, api_o, APIB);
	memcpy(full_ao, full_o, 40);

	compared++;
	switch (which) {
	case CASE_SYNC1:
	case CASE_SYNC0:
		if (rn || memcmp(sec_ar, sec_ao, SECB) || memcmp(api_ar, api_ao, APIB))
			fail(what, "sync bytes", rn, 0, rw, ow);
		break;
	case CASE_GET:
		if (len_r != len_o || memcmp(getbuf_r, getbuf_o, SECB))
			fail(what, "get", rn, 0, rw, ow);
		break;
	case CASE_FULL:
		if (fr != fo)
			fail(what, "full range", rn, 0, rw, ow);
		break;
	default:
		if (on < 0 || (unsigned int)on != rn)
			fail(what, "write count", rn, on, rw, ow);
		for (i = 0; i < rn; i++)
			if (rw[i].reg != ow[i].reg || rw[i].val != ow[i].val) {
				fail(what, "write", rn, on, rw, ow);
				return;
			}
		if (memcmp(full_ar, full_ao, 40)) {
			for (i = 0; i < 40; i++)
				if (full_ar[i] != full_ao[i]) {
					printf("MISMATCH %s: working block +%u vendor %02x ours %02x\n",
					       what, i, full_ar[i], full_ao[i]);
					break;
				}
			fail(what, "working block", rn, on, rw, ow);
		}
		break;
	}
}

/* mask: 1 slot empty, 2 no section, 4 no working block, 8 no API image */
static void run_deinit(u32 ch, unsigned int mask)
{
	u8 *ps, *pf, *pa, *ctx_r;
	struct t32_csc_ctx *ctx_o, *slot;
	void *exp[4];
	unsigned int ne = 0, i;
	u32 r0, r4, r8;

	t32o_reset();
	ps = (mask & 2) ? NULL : t32o_alloc(SECB);
	pf = (mask & 4) ? NULL : t32o_alloc(BLK);
	pa = (mask & 8) ? NULL : t32o_alloc(BLK);
	ctx_r = t32o_alloc(12);
	if (!ctx_r)
		exit(2);
	*(u32 *)(ctx_r + 0) = T32O_PTR(ps);
	*(u32 *)(ctx_r + 4) = T32O_PTR(pf);
	*(u32 *)(ctx_r + 8) = T32O_PTR(pa);
	t32_data_e3110[ch] = (mask & 1) ? 0 : T32O_PTR(ctx_r);
	t32o_call((t32o_fn)(void (*)(void))tisp_csc_deinit, ch, 0, 0, 0, 0);
	r0 = *(u32 *)(ctx_r + 0);
	r4 = *(u32 *)(ctx_r + 4);
	r8 = *(u32 *)(ctx_r + 8);

	t32o_reset();
	ps = (mask & 2) ? NULL : t32o_alloc(SECB);
	pf = (mask & 4) ? NULL : t32o_alloc(BLK);
	pa = (mask & 8) ? NULL : t32o_alloc(BLK);
	ctx_o = t32o_alloc(sizeof(*ctx_o));
	if (!ctx_o)
		exit(2);
	ctx_o->sec = ps;
	ctx_o->full = pf;
	ctx_o->api = pa;
	slot = (mask & 1) ? NULL : ctx_o;
	if (slot) {
		if (pf)
			exp[ne++] = pf;
		if (pa)
			exp[ne++] = pa;
		if (ps)
			exp[ne++] = ps;
		exp[ne++] = ctx_o;
	}
	nvlog = 0;
	t32_csc_deinit(vfree_log, &slot);
	compared++;
	if (nvlog != ne || slot != NULL ||
	    (r0 == 0) != (ctx_o->sec == NULL) || (r4 == 0) != (ctx_o->full == NULL) ||
	    (r8 == 0) != (ctx_o->api == NULL)) {
		printf("MISMATCH deinit mask %u: vfree %u expected %u\n", mask, nvlog, ne);
		printf("t32_cscx_oracle: %u compared, 1 mismatches\n", compared);
		exit(1);
	}
	for (i = 0; i < ne; i++)
		if (vlog[i] != exp[i]) {
			printf("MISMATCH deinit mask %u: vfree order at %u\n", mask, i);
			printf("t32_cscx_oracle: %u compared, 1 mismatches\n", compared);
			exit(1);
		}
}

static void run_boundaries(void)
{
	u8 sec[SECB], api[APIB];
	unsigned int a, b, m;

	for (a = 0; a < 3; a++)
		for (b = 0; b < 3; b++) {
			gen_section(sec);
			gen_api(api, 0);
			sec[100] = (u8)a;
			sec[101] = (u8)b;
			run_case("sync1-bound", CASE_SYNC1, 0, sec, api, 0);
			run_case("sync0-bound", CASE_SYNC0, 0, sec, api, 0);
			gen_api(api, 1);
			run_case("set-bound", CASE_SET, 1, sec, api, 0);
			run_case("api-bound", CASE_API, 1, sec, api, 0);
		}
	for (m = 0; m < 6; m++) {
		memset(sec, 0, sizeof(sec));
		memset(api, 0, sizeof(api));
		sec[0x5b] = (u8)m;
		api[0] = (u8)m;
		run_case("set-mode", CASE_SET, 0, sec, api, 0);
		run_case("api-mode", CASE_API, 0, sec, api, 0);
		run_case("wake-mode", CASE_WAKE, 0, sec, api, 0);
	}
	/* full-range byte after the second range group: section +98, API +90 */
	for (m = 0; m < 6; m++)
		for (a = 0; a < 3; a++) {
			gen_section(sec);
			gen_api(api, 1);
			sec[0x5b] = (u8)m;
			sec[100] = 1;
			sec[101] = 1;
			sec[98] = (u8)a;
			api[0] = (u8)m;
			api[90] = (u8)a;
			run_case("set-fr", CASE_SET, 0, sec, api, 0);
			run_case("api-fr", CASE_API, 0, sec, api, 0);
			run_case("wake-fr", CASE_WAKE, 0, sec, api, 0);
		}
	memset(sec, 0, sizeof(sec));
	memset(api, 0, sizeof(api));
	run_case("get-zero", CASE_GET, 0, sec, api, 0);
	run_case("full-zero", CASE_FULL, 0, sec, api, 0);
	run_case("full-max", CASE_FULL, 0, sec, api, 0xffffffffu);
	for (m = 0; m < 16; m++)
		run_deinit(m & 3, m);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		u8 sec[SECB], api[APIB];
		u32 ch = rnd() & 3;
		u32 fv = rnd();

		gen_section(sec); gen_api(api, 0);
		run_case("sync1", CASE_SYNC1, ch, sec, api, fv);
		gen_section(sec); gen_api(api, 0);
		run_case("sync0", CASE_SYNC0, ch, sec, api, fv);
		gen_section(sec); gen_api(api, 0);
		run_case("get", CASE_GET, ch, sec, api, fv);
		gen_section(sec); gen_api(api, 0);
		run_case("set", CASE_SET, ch, sec, api, fv);
		gen_section(sec); gen_api(api, 1);
		run_case("api", CASE_API, ch, sec, api, fv);
		gen_section(sec); gen_api(api, 0);
		run_case("wake", CASE_WAKE, ch, sec, api, fv);
		gen_section(sec); gen_api(api, 0);
		run_case("full", CASE_FULL, ch, sec, api, fv);
		run_deinit(ch, rnd() & 15);
	}
	run_boundaries();

	printf("t32_cscx_oracle: random %u compared, %u mismatches\n", compared, nfail);
	return nfail ? 1 : 0;
}
