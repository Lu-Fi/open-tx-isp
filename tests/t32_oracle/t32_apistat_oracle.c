// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our statistics configuration setter
 * (driver/t32/tx_isp_t32_api_stat.h) against the vendor
 * tisp_s_statis_config_attr from Matt Davis' mechanical MIPS-to-C recovery
 * (ref/, PR #15 @ 2e2e06bc).
 *
 * vmalloc/vfree and the AE/GSM/AWB/AF callees are recording stubs.  The
 * scratch buffer is filled with a pattern on allocation; the GSM call logs
 * its bytes +1044..1047 and the AF call the 16 bytes +8..+23 the vendor
 * sets.  Compared: calls (order, arguments; the scratch pointer as "the
 * block vmalloc returned", freed at the end), return value, every byte of the
 * attribute block of all channels and of the user block.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_stat.h"

extern u32 t32_data_f0904[4];

static u32 rs = 0x57a715u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define NREC 8
static struct { u32 fn, a[2]; u8 b[16]; } lg[NREC];
static unsigned int nlg;
static u8 *scr;
static u32 stub_ret;

static u64 rec(u32 fn, u32 a0, u32 a1, const u8 *b, unsigned int bl)
{
	if (nlg < NREC) {
		memset(&lg[nlg], 0, sizeof(lg[nlg]));
		lg[nlg].fn = fn; lg[nlg].a[0] = a0; lg[nlg].a[1] = a1;
		if (b)
			memcpy(lg[nlg].b, b, bl);
	}
	nlg++;
	return stub_ret + fn + nlg;
}
u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	unsigned int i;

	(void)a1; (void)a2; (void)a3;
	scr = t32o_alloc(a0);
	for (i = 0; i < a0; i++)
		scr[i] = (u8)(i * 7 + 3);
	rec(1, a0, 0, 0, 0);
	return T32O_PTR(scr);
}
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return rec(2, a0 == T32O_PTR(scr), 0, 0, 0);
}
u64 api_ae_set_hist_bin(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(3, a0, a1, 0, 0); }
u64 tisp_gsm_api_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	return rec(4, a0, a1 == T32O_PTR(scr), (u8 *)T32O_HOST(a1) + 1044, 4);
}
u64 tisp_awb_set_statis_localtion(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(5, a0, a1, 0, 0); }
u64 tisp_af_api_set_attr(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	return rec(6, a0, 0, (u8 *)T32O_HOST(a1) + 8, 16);
}

static void *o_vm(u32 n) { return T32O_HOST((u32)private_vmalloc(n, 0, 0, 0)); }
static void o_vf(void *p) { private_vfree(T32O_PTR(p), 0, 0, 0); }
static int o_hb(u32 c, const u8 *b) { return (int)api_ae_set_hist_bin(c, T32O_PTR(b), 0, 0); }
static int o_gsm(u32 c, const u8 *s) { return (int)tisp_gsm_api_set(c, T32O_PTR(s), 0, 0); }
static int o_loc(u32 c, const u8 *l) { return (int)tisp_awb_set_statis_localtion(c, T32O_PTR(l), 0, 0); }
static int o_af(u32 c, const u8 *af)
{
	static u8 low[24];	/* ours keeps the AF block on the host stack */

	memcpy(low, af, 24);
	return (int)tisp_af_api_set_attr(c, T32O_PTR(low), 0, 0);
}
static const struct t32_api_stat_ops ops = {
	.vmalloc = o_vm, .vfree = o_vf, .ae_set_hist_bin = o_hb, .gsm_api_set = o_gsm,
	.awb_set_statis_location = o_loc, .af_api_set_attr = o_af,
};

u64 tisp_s_statis_config_attr(u32, u32, u32, u32);

#define ATB (4 * 72 + 1600)
static unsigned int failed, compared;

static void run(u32 ch, unsigned int c)
{
	static u8 at0[ATB], vat[ATB], b0[72], vb[72];
	static struct { u32 fn, a[2]; u8 b[16]; } vl[NREC];
	unsigned int i, vn;
	u32 vr, w;
	u8 *attr, *buf;
	int or_;

	t32o_reset();
	attr = t32o_alloc(ATB);
	buf = t32o_alloc(72);
	stub_ret = rnd();
	for (i = 0; i < ATB; i++) at0[i] = (u8)rnd();
	for (i = 0; i < 72; i++) b0[i] = (u8)rnd();
	for (i = 0; i < 3; i++) {
		w = (c >> i & 1) ? 1 : ((c >> (i + 3) & 1) ? rnd() : 0);
		memcpy(b0 + (u32[]){ 0, 24, 44 }[i], &w, 4);
	}
	t32_data_f0904[0] = T32O_PTR(attr);
	memcpy(attr, at0, ATB); memcpy(buf, b0, 72);
	nlg = 0;
	vr = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_s_statis_config_attr, ch, T32O_PTR(buf), 0, 0, 0);
	vn = nlg;
	memcpy(vl, lg, sizeof(vl));
	memcpy(vat, attr, ATB); memcpy(vb, buf, 72);

	memcpy(attr, at0, ATB); memcpy(buf, b0, 72);
	nlg = 0;
	or_ = t32_s_statis_config_attr(&ops, attr, ch, buf);
	compared++;
	if (vn != nlg || vr != (u32)or_ || memcmp(vat, attr, ATB) || memcmp(vb, buf, 72) || memcmp(vl, lg, sizeof(vl)))
		goto bad;
	return;
bad:
	if (failed++ < 8) {
		printf("MISMATCH ch=%u c=%u: calls %u/%u ret %08x/%08x attr %d\n", ch, c, vn, nlg, vr, (u32)or_,
		       !!memcmp(vat, attr, ATB));
		for (i = 0; i < vn && i < NREC; i++)
			printf("  %u: %u/%u %08x/%08x %08x/%08x b %d\n", i, vl[i].fn, lg[i].fn, vl[i].a[0], lg[i].a[0],
			       vl[i].a[1], lg[i].a[1], memcmp(vl[i].b, lg[i].b, 16));
	}
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;

	for (c = 0; c < n; c++)
		run(rnd() & 3, c);
	printf("t32_apistat_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
