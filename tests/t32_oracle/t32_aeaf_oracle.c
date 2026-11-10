// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AE / AWB info blocks (tisp_set_ae_info, tisp_get_awb_info,
 * tisp_set_awb_info).  Runs the unmodified vendor functions (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc) and our t32_aea_set_ae_info /
 * t32_awb_api_get_info / t32_awb_api_set_info from driver/t32/tx_isp_t32_stats.h
 * from the same random state and compares the info block, the AWB tune / sec /
 * mode / tuning blocks of channel 0 (the vendor hard-wires channel 0) and of a
 * decoy channel, the 41 words printed by set_ae_info (recording isp_printf on
 * both sides), the register and event log.  tisp_get_ae_info is "return 0".
 * Exit 0 = all equal, 1 = first mismatch, 2 = setup failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define SECB	0x1e000u
#define TUNEB	0x2000u
#define TBB	0x8000u
#define MODEB	64u
#define BLKB	256u

extern u32 t32_data_e16b4[4];
u8 t32_data_c7b50[4] __asm__("t32_data_c7b50");
u8 t32_data_bf448[4] __asm__("t32_data_bf448");
u8 t32_data_c9430[4] __asm__("t32_data_c9430");
u32 t32_data_bf420[10] __asm__("t32_data_bf420") = {
	0x3f858, 0x3f860, 0x3f868, 0x3f880, 0x3f898,
	0x3f8b0, 0x3f8c8, 0x3f8e0, 0x3f8f8, 0x3f910
};

extern u64 tisp_set_ae_info(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_get_awb_info(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_set_awb_info(u32 a0, u32 a1, u32 a2, u32 a3);

struct prec {
	u32 n, w[41];
};
static struct prec pr;

/* the vendor passes a0 = 0, a1 = format, then the 41 payload words (2 in registers, 39 on the stack) */
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, u32 a8, u32 a9,
	       u32 a10, u32 a11, u32 a12, u32 a13, u32 a14, u32 a15, u32 a16, u32 a17, u32 a18, u32 a19,
	       u32 a20, u32 a21, u32 a22, u32 a23, u32 a24, u32 a25, u32 a26, u32 a27, u32 a28, u32 a29,
	       u32 a30, u32 a31, u32 a32, u32 a33, u32 a34, u32 a35, u32 a36, u32 a37, u32 a38, u32 a39,
	       u32 a40, u32 a41, u32 a42)
{
	(void)a0;
	if (a1 != (u32)(uintptr_t)t32_data_c7b50)
		return 0;	/* the printk of tisp_awb_set_mode is not ported */
	pr.n++;
	pr.w[0] = a2; pr.w[1] = a3; pr.w[2] = a4; pr.w[3] = a5; pr.w[4] = a6; pr.w[5] = a7; pr.w[6] = a8;
	pr.w[7] = a9; pr.w[8] = a10; pr.w[9] = a11; pr.w[10] = a12; pr.w[11] = a13; pr.w[12] = a14;
	pr.w[13] = a15; pr.w[14] = a16; pr.w[15] = a17; pr.w[16] = a18; pr.w[17] = a19; pr.w[18] = a20;
	pr.w[19] = a21; pr.w[20] = a22; pr.w[21] = a23; pr.w[22] = a24; pr.w[23] = a25; pr.w[24] = a26;
	pr.w[25] = a27; pr.w[26] = a28; pr.w[27] = a29; pr.w[28] = a30; pr.w[29] = a31; pr.w[30] = a32;
	pr.w[31] = a33; pr.w[32] = a34; pr.w[33] = a35; pr.w[34] = a36; pr.w[35] = a37; pr.w[36] = a38;
	pr.w[37] = a39; pr.w[38] = a40; pr.w[39] = a41; pr.w[40] = a42;
	return 0;
}

static void print_hook(void *priv, const u32 *w)
{
	(void)priv;
	isp_printf(0, (u32)(uintptr_t)t32_data_c7b50, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8],
		   w[9], w[10], w[11], w[12], w[13], w[14], w[15], w[16], w[17], w[18], w[19], w[20], w[21],
		   w[22], w[23], w[24], w[25], w[26], w[27], w[28], w[29], w[30], w[31], w[32], w[33], w[34],
		   w[35], w[36], w[37], w[38], w[39], w[40]);
}

static u32 rs = 0x7a3c19d5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_SAE, C_GAWB, C_SAWB, C_N };
static const char *const names[C_N] = { "set_ae_info", "get_awb_info", "set_awb_info" };

struct env {
	u8 *tune[2], *sec[2], *tb[2], *mode[2], *blk;
};
static u8 d_tune[2][TUNEB], d_sec[2][SECB], d_tb[2][TBB], d_mode[2][MODEB], d_blk[BLKB];
struct snap {
	u8 tune[2][TUNEB], sec[2][SECB], tb[2][TBB], mode[2][MODEB], blk[BLKB];
	u32 evlog[T32O_EVLOG][9], evn, logn;
	struct prec pr;
};
static struct snap sr, so;

static int setup(struct env *e, u32 unused)
{
	u32 k;

	(void)unused;
	for (k = 0; k < 2; k++) {
		u8 *ctx = t32o_alloc(16);

		e->tune[k] = t32o_alloc(TUNEB);
		e->sec[k] = t32o_alloc(SECB);
		e->tb[k] = t32o_alloc(TBB);
		e->mode[k] = t32o_alloc(MODEB);
		if (!ctx || !e->tune[k] || !e->sec[k] || !e->tb[k] || !e->mode[k])
			return 2;
		memcpy(e->tune[k], d_tune[k], TUNEB);
		memcpy(e->sec[k], d_sec[k], SECB);
		memcpy(e->tb[k], d_tb[k], TBB);
		memcpy(e->mode[k], d_mode[k], MODEB);
		*(u32 *)(ctx + 0) = T32O_PTR(e->tune[k]);
		*(u32 *)(ctx + 4) = T32O_PTR(e->sec[k]);
		*(u32 *)(ctx + 8) = T32O_PTR(e->mode[k]);
		*(u32 *)(ctx + 12) = 0;
		t32_data_e16b4[k] = T32O_PTR(ctx);
		t32_data_dd0a4[k] = T32O_PTR(e->tb[k]);
	}
	e->blk = t32o_alloc(BLKB);
	if (!e->blk)
		return 2;
	memcpy(e->blk, d_blk, BLKB);
	memset(&pr, 0, sizeof(pr));
	return 0;
}

static void take(struct snap *s, const struct env *e)
{
	u32 k;

	for (k = 0; k < 2; k++) {
		memcpy(s->tune[k], e->tune[k], TUNEB);
		memcpy(s->sec[k], e->sec[k], SECB);
		memcpy(s->tb[k], e->tb[k], TBB);
		memcpy(s->mode[k], e->mode[k], MODEB);
	}
	memcpy(s->blk, e->blk, BLKB);
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->evn = t32o_ev_n;
	s->logn = t32o_log.n;
	s->pr = pr;
}

#define VC(fn, a0) ((void)t32o_call((t32o_fn)(void (*)(void))fn, a0, 0, 0, 0, 0))

static void fill(u8 *p, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		p[i] = (u8)(rnd() >> 11);
}

static int run_case(unsigned int which)
{
	struct env e;
	struct t32_awb_ctx c;
	const char *what = NULL;
	u32 k;

	t32o_reset();
	if (setup(&e, 0))
		return 2;
	switch (which) {
	case C_SAE: VC(tisp_set_ae_info, T32O_PTR(e.blk)); break;
	case C_GAWB: VC(tisp_get_awb_info, T32O_PTR(e.blk)); break;
	case C_SAWB: VC(tisp_set_awb_info, T32O_PTR(e.blk)); break;
	}
	take(&sr, &e);

	t32o_reset();
	if (setup(&e, 0))
		return 2;
	c.tune = e.tune[0];
	c.sec = e.sec[0];
	c.mode = (u32 *)e.mode[0];
	c.base = e.tb[0];
	switch (which) {
	case C_SAE: t32_aea_set_ae_info(e.blk, print_hook, NULL); break;
	case C_GAWB: t32_awb_api_get_info(&c, e.blk); break;
	case C_SAWB: t32_awb_api_set_info(&c, e.blk); break;
	}
	take(&so, &e);
	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	if (!what && memcmp(&so.pr, &sr.pr, sizeof(sr.pr)))
		what = "printed words";
	for (k = 0; !what && k < 2; k++) {
		if (memcmp(so.tune[k], sr.tune[k], TUNEB))
			what = k ? "decoy tune block" : "tune block";
		else if (memcmp(so.sec[k], sr.sec[k], SECB))
			what = k ? "decoy sec block" : "sec block";
		else if (memcmp(so.tb[k], sr.tb[k], TBB))
			what = k ? "decoy tuning base" : "tuning base";
		else if (memcmp(so.mode[k], sr.mode[k], MODEB))
			what = k ? "decoy mode block" : "mode block";
	}
	if (!what && memcmp(so.blk, sr.blk, BLKB))
		what = "info block";
	if (what) {
		printf("MISMATCH %s: %s\n", names[which], what);
		return 1;
	}
	return 0;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % C_N;
		u32 k, r;
		int rc;

		for (k = 0; k < 2; k++) {
			fill(d_tune[k], TUNEB);
			fill(d_sec[k], SECB);
			fill(d_tb[k], TBB);
			fill(d_mode[k], MODEB);
			r = rnd() % 4;
			d_sec[k][T32_AWB_SEC_MODE] = r == 0 ? 9 : r == 1 ? 0 : r == 2 ? (u8)(rnd() % 12) : d_sec[k][T32_AWB_SEC_MODE];
			if (!(rnd() & 7))
				d_sec[k][T32_AWB_SEC_A0] = d_sec[k][T32_AWB_SEC_A0 + 1] = 0;
			if (!(rnd() & 7))
				d_sec[k][T32_AWB_SEC_A1] = d_sec[k][T32_AWB_SEC_A1 + 1] = 0;
			if (!(rnd() & 7))
				d_sec[k][T32_AWB_SEC_B0] = d_sec[k][T32_AWB_SEC_B0 + 1] = 0;
			if (!(rnd() & 7))
				d_sec[k][T32_AWB_SEC_B1] = d_sec[k][T32_AWB_SEC_B1 + 1] = 0;
		}
		fill(d_blk, BLKB);
		if (which == C_SAWB && (rnd() & 3))
			*(u32 *)(d_blk + 27) = rnd() % 12;	/* mode word: all table entries and >= 10 */
		rc = run_case(which);
		if (rc) {
			printf("t32_aeaf_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_aeaf_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
