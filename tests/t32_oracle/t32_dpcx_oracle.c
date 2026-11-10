// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: DPC per-frame paths.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc) tisp_dpc_l_gain_update,
 * tisp_dpc_s_gain_update and tisp_dpc_wdr_en and our readable
 * t32_dpc_{l_gain_update,s_gain_update,wdr_en} from
 * driver/t32/tx_isp_t32_dpc.h from identical random state.
 *
 * Compared: return value, every register write (count, reg, val, order), the
 * whole 0x80 byte vendor context image (pointers, +8 word, gains, argument,
 * the four tables, unused bytes), the 0xb4 value block plus its 16 pad bytes,
 * the whole tuning base buffer (section included), all of it inside canary
 * frames, and the three decoy channels (contexts, value blocks, tuning bases,
 * t32_data_e35f0 / t32_data_dd0a4) that must stay untouched (ch is used only
 * as the context index by the vendor).  Initial gain, gain1, argument and WDR
 * byte are random, so a wrong write or a wdr dependency shows.  Sections and
 * context tables come in several modes (random, small values, equal runs,
 * constant, 0xff) and gains cover every table index 0..11+ with fp 0/max.
 *
 * Not closable: the vendor tests the full 32 bit argument of tisp_dpc_wdr_en
 * against 1 while the port takes a u8; only en <= 0xff is compared.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_dpc.h"

#define SECB		T32_DPC_SEC_BYTES
#define SEC_OFF		0x59c0u
#define BASEB		(SEC_OFF + SECB + 64)
#define OB		(T32_DPC_VALUES + 16)
#define CTXB		0x80u
#define CAN		32u
#define MAXW		T32_DPC_MAX_WRITES

extern u32 t32_data_e35f0[4];

extern u64 tisp_dpc_l_gain_update(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_dpc_s_gain_update(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_dpc_wdr_en(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x7a19c3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

enum { CASE_L, CASE_S, CASE_WDR };

static void gen_bytes(u8 *b, unsigned int len, unsigned int mode)
{
	unsigned int i;
	u8 c = (u8)rnd();

	for (i = 0; i < len; i++) {
		switch (mode) {
		case 0: b[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 7); break;
		case 1: b[i] = (u8)(rnd() & 7); break;
		case 2: b[i] = (u8)rnd(); break;
		case 3: b[i] = (rnd() & 3) ? 0x10 : (u8)(rnd() & 0x11); break;
		case 4: b[i] = c; break;
		case 5: b[i] = 0xff; break;
		default: b[i] = (u8)(rnd() & 1); break;
		}
	}
}

static u8 *frame(u32 size)
{
	u8 *p = t32o_alloc(size + 2 * CAN);
	u32 i;

	if (!p)
		return 0;
	for (i = 0; i < CAN; i++) {
		p[i] = (u8)(0xa5 ^ i);
		p[CAN + size + i] = (u8)(0x5a ^ i);
	}
	return p + CAN;
}

static int canary_ok(const u8 *q, u32 size)
{
	u32 i;

	for (i = 0; i < CAN; i++)
		if (q[-(int)CAN + (int)i] != (u8)(0xa5 ^ i) || q[size + i] != (u8)(0x5a ^ i))
			return 0;
	return 1;
}

static u8 i_base[4][BASEB], i_o[4][OB], i_ctx[4][CTXB];

static u32 rd32(const u8 *p)
{
	u32 v;

	memcpy(&v, p, 4);
	return v;
}

static void wr32(u8 *p, u32 v)
{
	memcpy(p, &v, 4);
}

static int run_case(const char *what, int which, u32 ch, u32 gain, u32 en,
		    unsigned int mode)
{
	u8 *rb[4], *ro[4], *rc[4];
	u8 *ob, *oo;
	struct t32_dpc_ctx *oc;
	u8 exp_ctx[CTXB];
	u8 a_ctx[CTXB], a_o[OB], a_base[BASEB];
	struct t32_front_write refw[MAXW], ow[MAXW];
	u32 save_e35[4], save_dd[4];
	unsigned int ref_n, i, c, printfs;
	int our_n = 0;
	u32 ref_rc;
	const char *why = "";

	ch &= 3;
	t32o_reset();
	for (c = 0; c < 4; c++) {
		rb[c] = frame(BASEB);
		ro[c] = frame(OB);
		rc[c] = frame(CTXB);
		if (!rb[c] || !ro[c] || !rc[c])
			return 2;
		for (i = 0; i < BASEB; i++)
			rb[c][i] = (u8)rnd();
		gen_bytes(rb[c] + SEC_OFF, SECB, (c == ch) ? mode : (rnd() % 7));
		for (i = 0; i < OB; i++)
			ro[c][i] = (u8)rnd();
		for (i = 0; i < CTXB; i++)
			rc[c][i] = (u8)rnd();
		gen_bytes(rc[c] + 24, 88, mode);
		wr32(rc[c] + 0, T32O_PTR(rb[c] + SEC_OFF));
		wr32(rc[c] + 4, T32O_PTR(ro[c]));
		memcpy(i_base[c], rb[c], BASEB);
		memcpy(i_o[c], ro[c], OB);
		memcpy(i_ctx[c], rc[c], CTXB);
		t32_data_e35f0[c] = T32O_PTR(rc[c]);
		t32_data_dd0a4[c] = T32O_PTR(rb[c]);
	}
	memcpy(save_e35, t32_data_e35f0, sizeof(save_e35));
	memcpy(save_dd, t32_data_dd0a4, sizeof(save_dd));

	switch (which) {
	case CASE_L:
		ref_rc = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_dpc_l_gain_update,
					ch, 0, gain, 0, 0);
		break;
	case CASE_S:
		ref_rc = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_dpc_s_gain_update,
					ch, 0, gain, 0, 0);
		break;
	default:
		ref_rc = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_dpc_wdr_en,
					ch, en, 0, 0, 0);
		break;
	}
	ref_n = t32o_log.n;
	printfs = t32o_log.printfs;
	if (ref_n > MAXW) {
		fprintf(stderr, "ref_n %u > MAXW\n", ref_n);
		return 2;
	}
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));

	/* decoys, canaries, globals untouched; target buffers snapshot */
	for (c = 0; c < 4; c++) {
		if (!canary_ok(rb[c], BASEB) || !canary_ok(ro[c], OB) || !canary_ok(rc[c], CTXB)) {
			why = "ref canary";
			goto bad_early;
		}
		if (memcmp(rb[c], i_base[c], BASEB)) { why = "ref base written"; goto bad_early; }
		if (c != ch && (memcmp(ro[c], i_o[c], OB) || memcmp(rc[c], i_ctx[c], CTXB))) {
			why = "decoy written";
			goto bad_early;
		}
	}
	if (memcmp(save_e35, t32_data_e35f0, sizeof(save_e35)) ||
	    memcmp(save_dd, t32_data_dd0a4, sizeof(save_dd))) {
		why = "ref globals";
		goto bad_early;
	}
	if (printfs || (u32)ref_rc != 0) {
		why = "ref printf/rc";
		goto bad_early;
	}
	memcpy(a_ctx, rc[ch], CTXB);
	memcpy(a_o, ro[ch], OB);
	memcpy(a_base, rb[ch], BASEB);

	/* our side from the same initial state */
	t32o_reset();
	ob = frame(BASEB);
	oo = frame(OB);
	oc = (struct t32_dpc_ctx *)frame(sizeof(*oc));
	if (!ob || !oo || !oc)
		return 2;
	memcpy(ob, i_base[ch], BASEB);
	memcpy(oo, i_o[ch], OB);
	oc->sec = ob + SEC_OFF;
	oc->o = oo;
	oc->rsvd0 = rd32(i_ctx[ch] + 8);
	oc->gain = rd32(i_ctx[ch] + 12);
	oc->gain1 = rd32(i_ctx[ch] + 16);
	oc->arg = rd32(i_ctx[ch] + 20);
	oc->wdr = i_ctx[ch][8];
	oc->wide = i_ctx[ch][9];
	memcpy(oc->tbl, i_ctx[ch] + 24, 88);

	switch (which) {
	case CASE_L:
		our_n = t32_dpc_l_gain_update(oc, (u8)ch, gain, ow);
		break;
	case CASE_S:
		our_n = t32_dpc_s_gain_update(oc, (u8)ch, gain, ow);
		break;
	default:
		our_n = t32_dpc_wdr_en(oc, (u8)ch, (u8)en, ow);
		break;
	}

	memcpy(exp_ctx, i_ctx[ch], CTXB);
	exp_ctx[8] = oc->wdr;
	wr32(exp_ctx + 12, oc->gain);
	wr32(exp_ctx + 16, oc->gain1);
	wr32(exp_ctx + 20, oc->arg);
	memcpy(exp_ctx + 24, oc->tbl, 88);

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n) { why = "count"; goto bad; }
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val) { why = "write"; goto bad; }
	if (oc->sec != ob + SEC_OFF || oc->o != oo ||
	    oc->rsvd0 != rd32(i_ctx[ch] + 8)) { why = "ours ptr/rsvd"; goto bad; }
	if (memcmp(a_ctx, exp_ctx, CTXB)) { why = "ctx image"; goto bad; }
	if (memcmp(a_o, oo, OB)) { why = "o block"; goto bad; }
	if (memcmp(a_base, ob, BASEB)) { why = "base"; goto bad; }
	if (!canary_ok(ob, BASEB) || !canary_ok(oo, OB) ||
	    !canary_ok((u8 *)oc, sizeof(*oc))) { why = "our canary"; goto bad; }
	return 0;
bad_early:
	failed++;
	printf("MISMATCH %s: %s (ref side)\n", what, why);
	return 1;
bad:
	if (failed++ < 8) {
		printf("MISMATCH %s: %s, writes ref %u ours %d, ch %u gain %08x en %u mode %u\n",
		       what, why, ref_n, our_n, ch, gain, en, mode);
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

/* gains: every table index, fp edges, clamps */
static u32 pick_gain(unsigned int c)
{
	switch (c % 12) {
	case 0: return rnd();
	case 1: return (rnd() % 12) << 16 | (rnd() & 0xffff);
	case 2: return (rnd() % 12) << 16;
	case 3: return (rnd() % 12) << 16 | 0xffff;
	case 4: return (9u << 16) | (rnd() & 0xffff);
	case 5: return (10u << 16) | (rnd() & 0xffff);
	case 6: return (11u << 16) | (rnd() & 0xffff);
	case 7: return 0xffffffffu;
	case 8: return 0;
	case 9: return 0x10000;
	case 10: return (rnd() & 0xffff) | 0x80000000u;
	default: return (rnd() % 10) << 16 | 0x8000;
	}
}

static const u32 en_set[] = { 0, 1, 2, 3, 4, 0x7f, 0x80, 0xff };

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int c, m, ch, g;

	for (c = 0; c < n; c++) {
		u32 chn = rnd() & 3;
		u32 gain = pick_gain(c);
		u32 en = (c & 1) ? en_set[rnd() & 7] : (rnd() & 0xff);

		m = c % 7;
		run_case("l", CASE_L, chn, gain, 0, m);
		run_case("s", CASE_S, chn, pick_gain(c + 5), 0, (c + 3) % 7);
		run_case("wdr", CASE_WDR, chn, 0, en, (c + 5) % 7);
	}
	/* deterministic sweep: every channel, every gain class, every mode */
	for (ch = 0; ch < 4; ch++)
		for (g = 0; g < 12; g++)
			for (m = 0; m < 7; m++) {
				run_case("l-sweep", CASE_L, ch, pick_gain(g), 0, m);
				run_case("s-sweep", CASE_S, ch, pick_gain(g), 0, m);
				run_case("wdr-sweep", CASE_WDR, ch, 0, g & 3, m);
			}
	for (m = 0; m < 7; m++)
		for (g = 0; g < 8; g++)
			run_case("wdr-en", CASE_WDR, m & 3, 0, en_set[g], m);

	printf("t32_dpcx_oracle: random %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
