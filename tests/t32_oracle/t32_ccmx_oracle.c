// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: CCM per-frame / API paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_ccm_{irq_frame_done,deinit,param_array_get,param_array_set,api_set,
 * api_get,wake_up} from driver/t32/tx_isp_t32_ccm.h and compares every
 * register write, the return values and every context/state field.
 *
 * Vendor context (e28f0[ch]): { sec, state, cp, mat, api } at 0,4,8,12,16;
 * state = { ev lo, ev hi, ct, ct code }; payload pointer at dc274[ch] + 24,
 * its capacity at dc1a4[ch * 28 + 8].  t32_data_e28e0 / t32_data_da93e are
 * defined here (env.c does not provide them).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ccm.h"

#define SECB		T32_CCM_SEC_BYTES
#define SEC_OFF		0x7e40u
#define MAXW		96
#define LUTMAX		400

__attribute__((weak)) u32 t32_data_e28e0[4];
__attribute__((weak)) u16 t32_data_da93e[1];

/* env.c already defines a tisp_ccm_irq_frame_done, so the ref file is not linked: the
 * irq case checks against the ref's semantics (ref 8af90: ++ and mask 17 bits, ret 1) */
extern u64 tisp_ccm_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ccm_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ccm_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ccm_api_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_ccm_api_get(u32 a0, u32 a1, u32 a2, u32 a3);
/* tisp_ccm_wake_up (ref 8d688) is just tisp_ccm_write_reg(ch, 0); its file does not
 * build as C23 (unprototyped extern), so the oracle calls tisp_ccm_write_reg directly */

static u32 rs = 0x51c3a7u;
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

static unsigned int compared, failed;

enum { CASE_IRQ, CASE_DEINIT, CASE_GET, CASE_SET, CASE_APISET, CASE_APIGET, CASE_WAKE };

struct cs {
	u8 sec[SECB], lut[LUTMAX], sec2[SECB], lut2[LUTMAX];
	u32 cap;
	u8 cp[0x68], mat[36], blob[40], inp[40];
	u32 state[4], frame, nullmask;
};

/* our side */
static struct t32_ccm ourst;
static u8 osec[SECB], olut[LUTMAX], obuf[2048], newlut[512];
static const void *freed[8];
static unsigned int nfreed, alloc_n, lfree_n;
static void *lfree_last;

static void rec_free(void *p)
{
	if (nfreed < 8)
		freed[nfreed] = p;
	nfreed++;
}

static void *lut_alloc_cb(u32 n)
{
	(void)n;
	alloc_n++;
	return newlut;
}

static void lut_free_cb(void *p)
{
	lfree_n++;
	lfree_last = p;
}

static int gen_section(u8 *sec, u8 *lut)
{
	unsigned int i, k, r;
	int knots = (int)(rnd() % 8) + 1;
	u32 t;

	for (i = 0; i < SECB; i++)
		sec[i] = (u8)rnd();
	for (i = 0; i < LUTMAX; i++)
		lut[i] = (u8)rnd();
	put32(sec, T32_CCM_ID);
	sec[0x35f] = rnd() % 16 ? (u8)(1 + rnd() % 255) : 0;
	sec[0x360] = (u8)(rnd() % 3);
	sec[0x361] = (u8)(rnd() % 4);
	sec[0x362] = (u8)(rnd() % 2);
	sec[0x363] = (u8)(rnd() % 2);
	sec[0x364] = (u8)knots;
	sec[0x365] = (u8)(rnd() % 8);
	r = rnd() % 4;
	put32(sec + 0x38, r == 0 ? 0 : r == 1 ? 1 : rnd());
	if (rnd() & 1) {
		t = rnd() % 0x100000;
		for (i = 0; i < 12; i++) {
			put32(sec + 4 + 4 * i, t);
			t += 1 + rnd() % 0x300000;
		}
	}
	t = 1500;
	for (k = 0; k < (unsigned int)knots; k++) {
		t += 200 + rnd() % 1200;
		put32(lut + 40 * k, t);
		for (i = 0; i < 9; i++)
			put32(lut + 40 * k + 4 + 4 * i, rnd() % 0x4000);
	}
	return knots;
}

static void gen(struct cs *c)
{
	unsigned int i;
	int k1 = gen_section(c->sec, c->lut);

	gen_section(c->sec2, c->lut2);
	c->cap = 40U * (u32)(k1 + (int)(rnd() % 3));
	for (i = 0; i < 0x68; i++)
		c->cp[i] = (u8)rnd();
	for (i = 0; i < 36; i++)
		c->mat[i] = (u8)rnd();
	for (i = 0; i < 40; i++) {
		c->blob[i] = (u8)rnd();
		c->inp[i] = (u8)rnd();
	}
	c->blob[0] = (u8)(rnd() % 2);
	c->blob[1] = (u8)(rnd() % 2);
	c->inp[0] = (u8)(rnd() % 2);
	c->inp[1] = (u8)(rnd() % 2);
	c->state[0] = rnd();
	c->state[1] = rnd() % 4;
	c->state[2] = 1500 + rnd() % 6000;
	c->state[3] = rnd() % 2 ? 255 : rnd() % 20;
	c->frame = rnd() % 2 ? 0x1ffffu - rnd() % 3 : rnd();
	c->nullmask = rnd();
}

static int run_case(const char *what, int which, u32 ch, const struct cs *c)
{
	struct t32_front_write refw[MAXW], ow[MAXW];
	unsigned int ref_n = 0, i;
	int our_n = 0;
	u8 *base, *cpr, *matr, *apir, *lutr, *info, *buf, *inpb;
	u32 *str, *lenp, *vc;
	u32 ret_ref = 0, ret_our = 0, len_our = 0, need2 = (u32)c->sec2[0x364] * 40U;
	u32 fo = c->frame;
	int slot_null = (which == CASE_DEINIT || which == CASE_APIGET) && (c->nullmask & 16);
	struct t32_ccm *our = &ourst;
	const char *why = "?";
	const void *exp[5];
	unsigned int nexp = 0;

	t32o_reset();
	base = t32o_alloc(SEC_OFF + SECB + 64);
	cpr = t32o_alloc(0x68);
	matr = t32o_alloc(40);
	apir = t32o_alloc(40);
	str = t32o_alloc(16);
	lutr = t32o_alloc(LUTMAX);
	info = t32o_alloc(64);
	vc = t32o_alloc(20);
	buf = t32o_alloc(2048);
	lenp = t32o_alloc(4);
	inpb = t32o_alloc(40);
	if (!base || !cpr || !matr || !apir || !str || !lutr || !info || !vc || !buf || !lenp || !inpb)
		return 2;
	memset(buf, 0, 2048);
	*lenp = 0;
	memcpy(base + SEC_OFF, c->sec, SECB);
	memcpy(cpr, c->cp, 0x68);
	memcpy(matr, c->mat, 36);
	memcpy(apir, c->blob, 40);
	memcpy(str, c->state, 16);
	memcpy(lutr, c->lut, LUTMAX);
	memcpy(inpb, c->inp, 40);
	put32(info + 24, T32O_PTR(lutr));
	vc[0] = T32O_PTR(base + SEC_OFF);
	vc[1] = T32O_PTR(str);
	vc[2] = T32O_PTR(cpr);
	vc[3] = T32O_PTR(matr);
	vc[4] = T32O_PTR(apir);
	if (which == CASE_DEINIT) {
		if (c->nullmask & 1)
			vc[2] = 0;
		if (c->nullmask & 2)
			vc[3] = 0;
		if (c->nullmask & 4)
			vc[1] = 0;
		if (c->nullmask & 8)
			vc[4] = 0;
	}
	t32_data_e28f0[ch] = slot_null ? 0 : T32O_PTR(vc);
	t32_data_dd0a4[ch] = T32O_PTR(base);
	t32_data_dc274[ch] = T32O_PTR(info);
	put32(t32_data_dc1a4 + ch * 28 + 8, c->cap);
	t32_data_e28e0[ch] = c->frame;
	t32_data_da93e[0] = 0x5a5a;

	switch (which) {
	case CASE_IRQ:
		t32_data_e28e0[ch] = (c->frame + 1) & 0x1ffffU;
		ret_ref = 1;
		break;
	case CASE_DEINIT:
		ret_ref = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ccm_deinit, ch, 0, 0, 0, 0);
		break;
	case CASE_GET:
		ret_ref = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ccm_param_array_get, ch,
					 T32O_PTR(buf), T32O_PTR(lenp), 0, 0);
		break;
	case CASE_SET:
		memcpy(buf, c->sec2, SECB);
		memcpy(buf + 872, c->lut2, need2);
		ret_ref = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ccm_param_array_set, ch,
					 T32O_PTR(buf), 0, 0, 0);
		break;
	case CASE_APISET:
		ret_ref = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ccm_api_set, ch,
					 T32O_PTR(inpb), 0, 0, 0);
		break;
	case CASE_APIGET:
		ret_ref = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ccm_api_get, ch,
					 T32O_PTR(buf), 0, 0, 0);
		break;
	case CASE_WAKE:
		t32o_call((t32o_fn)(void (*)(void))tisp_ccm_write_reg, ch, 0, 0, 0, 0);
		break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));

	/* ---- our side ---- */
	memset(&ourst, 0, sizeof(ourst));
	memcpy(osec, c->sec, SECB);
	memcpy(olut, c->lut, LUTMAX);
	ourst.sec = osec;
	ourst.lut = olut;
	ourst.lutbytes = c->cap;
	memcpy(ourst.cp, c->cp, 0x68);
	memcpy(ourst.mat_out, c->mat, 36);
	ourst.ev_lo = c->state[0];
	ourst.ev_hi = c->state[1];
	ourst.ct = c->state[2];
	ourst.ct_code = c->state[3];
	memcpy(t32_ccm_api_blob(&ourst), c->blob, 40);
	ourst.lut_alloc = lut_alloc_cb;
	ourst.lut_free = lut_free_cb;
	ourst.lut_size = 0x5a5a;
	nfreed = alloc_n = lfree_n = 0;
	lfree_last = NULL;
	memset(obuf, 0, sizeof(obuf));

	switch (which) {
	case CASE_IRQ:
		ret_our = t32_ccm_irq_frame_done(&fo);
		break;
	case CASE_DEINIT:
		if (!slot_null) {
			if (!(c->nullmask & 1)) {
				ourst.cp_owned = (void *)0x1000;
				exp[nexp++] = ourst.cp_owned;
			}
			if (!(c->nullmask & 2)) {
				ourst.mat_owned = (void *)0x2000;
				exp[nexp++] = ourst.mat_owned;
			}
			if (!(c->nullmask & 4)) {
				ourst.state_owned = (void *)0x3000;
				exp[nexp++] = ourst.state_owned;
			}
			if (!(c->nullmask & 8)) {
				ourst.api_owned = (void *)0x4000;
				exp[nexp++] = ourst.api_owned;
			}
			exp[nexp++] = &ourst;
		} else {
			our = NULL;
		}
		ret_our = (u32)t32_ccm_deinit(rec_free, &our);
		break;
	case CASE_GET:
		ret_our = (u32)t32_ccm_param_array_get(&ourst, obuf, &len_our);
		break;
	case CASE_SET:
		memcpy(obuf, c->sec2, SECB);
		memcpy(obuf + 872, c->lut2, need2);
		our_n = t32_ccm_param_array_set(&ourst, obuf, ow);
		break;
	case CASE_APISET:
		our_n = t32_ccm_api_set(&ourst, c->inp, ow);
		break;
	case CASE_APIGET:
		ret_our = (u32)t32_ccm_api_get(slot_null ? NULL : &ourst, obuf);
		break;
	case CASE_WAKE:
		our_n = t32_ccm_wake_up(&ourst, ow);
		break;
	}

	compared++;
	if (which == CASE_IRQ) {
		if (ret_ref != ret_our || fo != t32_data_e28e0[ch])
			goto bad;
		return 0;
	}
	if (which == CASE_DEINIT) {
		if (ref_n || ret_ref != ret_our || t32_data_e28f0[ch] != 0 || our != NULL || nfreed != nexp)
			goto bad;
		for (i = 0; i < nexp; i++)
			if (freed[i] != exp[i])
				goto bad;
		return 0;
	}
	if (which == CASE_GET) {
		if (ref_n || ret_ref != ret_our || *lenp != len_our || len_our > 2048 || memcmp(buf, obuf, 2048))
			goto bad;
		if (memcmp(base + SEC_OFF, c->sec, SECB) || memcmp(cpr, c->cp, 0x68) ||
		    memcmp(matr, c->mat, 36) || memcmp(str, c->state, 16) || memcmp(apir, c->blob, 40)) {
			why = "get side effect";
			goto bad;
		}
		return 0;
	}
	if (which == CASE_APIGET) {
		if (ref_n || ret_ref != ret_our)
			goto bad;
		if (memcmp(buf, obuf, 2048))
			goto bad;
		if (memcmp(base + SEC_OFF, c->sec, SECB) || memcmp(cpr, c->cp, 0x68) ||
		    memcmp(matr, c->mat, 36) || memcmp(str, c->state, 16) || memcmp(apir, c->blob, 40)) {
			why = "apiget side effect";
			goto bad;
		}
		return 0;
	}
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	if (memcmp(base + SEC_OFF, osec, SECB)) {
		why = "sec";
		goto bad;
	}
	if (memcmp(cpr, ourst.cp, 0x68)) {
		why = "cp";
		goto bad;
	}
	{
		u32 os[4];

		os[0] = ourst.ev_lo;
		os[1] = ourst.ev_hi;
		os[2] = ourst.ct;
		os[3] = ourst.ct_code;
		if (memcmp(str, os, 16)) {
			why = "state";
			printf("  state ref %08x %08x %08x %08x ours %08x %08x %08x %08x\n",
			       str[0], str[1], str[2], str[3], os[0], os[1], os[2], os[3]);
			goto bad;
		}
	}
	if (memcmp(matr, ourst.mat_out, 36)) {
		why = "mat";
		goto bad;
	}
	if (memcmp(apir, t32_ccm_api_blob(&ourst), 40)) {
		why = "api";
		goto bad;
	}
	if (which == CASE_SET) {
		const u8 *rl = T32O_HOST(get32(info + 24));
		int grow = need2 > c->cap;

		if (get32(t32_data_dc1a4 + ch * 28 + 8) != ourst.lutbytes) {
			why = "cap";
			printf("  cap ref %u ours %u need %u init %u\n", get32(t32_data_dc1a4 + ch * 28 + 8),
			       ourst.lutbytes, need2, c->cap);
			goto bad;
		}
		if (t32_data_da93e[0] != ourst.lut_size) {
			why = "da93e";
			printf("  da93e ref %u ours %u\n", t32_data_da93e[0], ourst.lut_size);
			goto bad;
		}
		if (memcmp(rl, ourst.lut, need2)) {
			why = "lut";
			goto bad;
		}
		if (alloc_n != (unsigned int)grow || lfree_n != (unsigned int)grow || (grow && lfree_last != olut)) {
			why = "alloc/free";
			printf("  alloc %u free %u grow %d\n", alloc_n, lfree_n, grow);
			goto bad;
		}
	}
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH %s (%s): writes ref %u ours %d, ret ref %u ours %u\n",
		       what, why, ref_n, our_n, ret_ref, ret_our);
		for (i = 0; i < ref_n || (int)i < our_n; i++)
			if (i >= ref_n || (int)i >= our_n ||
			    refw[i].reg != ow[i].reg || refw[i].val != ow[i].val) {
				printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
				       i < ref_n ? refw[i].reg : 0, i < ref_n ? refw[i].val : 0,
				       (int)i < our_n ? ow[i].reg : 0, (int)i < our_n ? ow[i].val : 0);
				if (i > 8)
					break;
			}
	}
	return 1;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	static struct cs c;
	unsigned int k;

	for (k = 0; k < n; k++) {
		u32 ch = rnd() & 3;

		gen(&c);
		run_case("irq", CASE_IRQ, ch, &c);
		run_case("deinit", CASE_DEINIT, ch, &c);
		run_case("get", CASE_GET, ch, &c);
		run_case("set", CASE_SET, ch, &c);
		run_case("apiset", CASE_APISET, ch, &c);
		run_case("apiget", CASE_APIGET, ch, &c);
		run_case("wake", CASE_WAKE, ch, &c);
	}

	printf("t32_ccmx_oracle: random %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
