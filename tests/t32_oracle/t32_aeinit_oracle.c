// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_aeinit.h (t32_ae_ctx_init: the state part of
 * tisp_ae_init with tisp_ae_sensor_par_ctrls_update, tisp_ae_init_par_calc,
 * tisp_ae_par_calc, tisp_ae_fps_calc_max_exp, tisp_ae_clac_deflicker_cfg)
 * against the whole vendor tisp_ae_init chain (Matt Davis' recovered C, PR #15
 * @ 2e2e06bc, with the vendor fixed point refs).  Our side runs on the low
 * stack too and its maths hooks call the same vendor fix_point_* /
 * tisp_math_exp2 refs (the maths are not under test here).
 * Per case: random or IQ-bin AE section (3288 B at tuning base + 104), random
 * sensor entry (dd2d4[0], 232 B: zero ratio words, mode / limit switch bytes,
 * fraction bits 0..31 forced), random sensor info (WDR byte 0), pipeline
 * count 0..3.  Compared: every byte of T (section), B (12848), R (2184,
 * d3a40), D (11768), C (16), the 2020 B context (pointer words as "points
 * to T / B"), the printk count and the 8 register writes of the tail (the
 * last 8 vendor writes).  Our R template is compared once with the .data
 * image of d3a40.  Channel 0 only (d3a40 holds one channel here).
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_aeinit.h"

extern u32 t32_data_d3a40[600], t32_data_e15a8[4], t32_data_e15c8[4], t32_data_e15d8[4];
extern u8 t32_data_dd2d4[0x400];
u64 fix_point_div_32(u32, u32, u32, u32), fix_point_mult2_32(u32, u32, u32, u32), fix_point_mult3_32(u32, u32, u32, u32);
u64 fix_point_mult2(u32, u32, u32, u32, u32, u32), fix_point_mult3_64(u32, u32, u32, u32, u32, u32, u32, u32);
u64 tisp_math_exp2(u32, u32, u32, u32);

static u32 rs = 0x6a09e667u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 o_div(u32 s, u32 a, u32 b) { return (u32)fix_point_div_32(s, a, b, 0); }
static u32 o_m2(u32 s, u32 a, u32 b) { return (u32)fix_point_mult2_32(s, a, b, 0); }
static u32 o_m3(u32 s, u32 a, u32 b, u32 c) { return (u32)fix_point_mult3_32(s, a, b, c); }
static t32_u64 o_m2l(u32 s, t32_u64 a, t32_u64 b) { return fix_point_mult2(s, 0, (u32)a, (u32)(a >> 32), (u32)b, (u32)(b >> 32)); }
static t32_u64 o_m3l(u32 s, t32_u64 a, t32_u64 b, t32_u64 c)
{
	return fix_point_mult3_64(s, 0, (u32)a, (u32)(a >> 32), (u32)b, (u32)(b >> 32), (u32)c, (u32)(c >> 32));
}
static u32 o_exp2(u32 v, u32 a, u32 b) { return (u32)tisp_math_exp2(v, a, b, 0); }
static unsigned int warns;
static void o_warn(u32 line) { (void)line; warns++; }
static const struct t32_ae_init_ops ops = { o_div, o_m2, o_m3, o_m2l, o_m3l, o_exp2, o_warn };

static struct t32_ae_blk ob;
static const u8 *o_S, *o_info;
static u32 o_pipes;
static struct t32_stats_write ow[16];
static int o_n;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	o_n = t32_ae_ctx_init(0, &ob, o_S, o_info, o_pipes, &ops, ow);
	return 0;
}

/* isp_printf of the register side (tisp_ae_roi_point's window message) is not part of ours: kept apart */
static unsigned int isp_prints;
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	isp_prints++;
	return 0;
}

static u32 d3a40_init[600];
static unsigned int compared, failed, idx_hist[16];

static int cmpb(const char *what, const u8 *v, const u8 *o, u32 n, const char *tag)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (v[i] != o[i]) {
			if (failed++ < 5)
				printf("t32_aeinit_oracle: FAIL %s: %s byte %u vendor %02x ours %02x\n", tag, what, i, v[i], o[i]);
			return 1;
		}
	return 0;
}

static int run_case(const char *tag, const u8 *sec, int force)
{
	static u8 S[T32_AEI_S_BYTES], info[64];
	u8 *base, *vctx, *vT, *vB;
	u32 i, pipes = rnd() & 3, vn;
	unsigned int vwarn, vn0;
	int bad = 0;

	for (i = 0; i < sizeof(S); i++)
		S[i] = (u8)rnd();
	for (i = 0; i < sizeof(info); i++)
		info[i] = (u8)rnd();
	info[36] = 0;
	if (force & 1) {	/* hi halves / words zero: the vendor defaults */
		if (rnd() & 1) { S[66] = 0; S[67] = 0; }
		if (rnd() & 1) { S[62] = 0; S[63] = 0; }
		if (rnd() & 1) { S[58] = 0; S[59] = 0; }
		if (rnd() & 1) memset(S + 56, 0, 4);
	}
	/* small sensor limits / gains so that the clamps go both ways */
	if (rnd() & 1) { t32_wr32(S, 68, rnd() & 0x3ff); t32_wr32(S, 72, rnd() & 0xffff); }
	if (rnd() & 1) { t32_wr32(S, 36, rnd() & 0x7ffff); t32_wr32(S, 40, rnd() & 0x7ffff); }

	/* vendor */
	t32o_reset();
	memcpy(t32_data_d3a40, d3a40_init, sizeof(d3a40_init));
	base = t32o_alloc(0x2000);
	memcpy(base + T32_AEI_T_OFF, sec, T32_AEI_T_BYTES);
	t32_data_dd0a4[0] = T32O_PTR(base);
	memcpy(t32_data_dd2d4, S, sizeof(S));
	t32_data_dc284[900] = (u8)pipes;
	t32_data_d03a0[0] = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_ae_init, 0, T32O_PTR(info), 0, 0, 0);
	vwarn = t32o_log.printfs;
	vn = t32o_log.n;
	vctx = T32O_HOST(t32_data_e15a8[0]);
	vT = base + T32_AEI_T_OFF;
	vB = T32O_HOST(t32_ld32(vctx, 4));

	/* ours */
	ob.T = t32o_alloc(T32_AEI_T_BYTES);
	memcpy(ob.T, sec, T32_AEI_T_BYTES);
	ob.ctx = t32o_alloc(T32_AEI_CTX_BYTES); memset(ob.ctx, 0x5a, T32_AEI_CTX_BYTES);
	ob.B = t32o_alloc(T32_AEI_B_BYTES); memset(ob.B, 0x5a, T32_AEI_B_BYTES);
	ob.D = t32o_alloc(T32_AEI_D_BYTES); memset(ob.D, 0x5a, T32_AEI_D_BYTES);
	ob.C = t32o_alloc(T32_AEI_C_BYTES);
	ob.R = t32o_alloc(T32_AEI_R_BYTES);
	t32_ae_state_tmpl(ob.R);
	o_S = S; o_info = info; o_pipes = pipes; warns = 0;
	vn0 = t32o_log.printfs;	/* the vendor maths refs print too (divide by zero): ours = hook + those */
	t32o_call((t32o_fn)(void (*)(void))ours, 0, 0, 0, 0, 0);
	warns += t32o_log.printfs - vn0;

	compared++;
	idx_hist[t32_st16(ob.B, 8576) & 15]++;
	bad |= cmpb("T", vT, ob.T, T32_AEI_T_BYTES, tag);
	bad |= cmpb("B", vB, ob.B, T32_AEI_B_BYTES, tag);
	bad |= cmpb("R", (const u8 *)t32_data_d3a40, ob.R, T32_AEI_R_BYTES, tag);
	bad |= cmpb("D", T32O_HOST(t32_data_e15d8[0]), ob.D, T32_AEI_D_BYTES, tag);
	bad |= cmpb("C", T32O_HOST(t32_data_e15c8[0]), ob.C, T32_AEI_C_BYTES, tag);
	bad |= cmpb("ctx", vctx + 8, ob.ctx + 8, T32_AEI_CTX_BYTES - 8, tag);
	if (t32_ld32(vctx, 0) != T32O_PTR(vT) || t32_ld32(ob.ctx, 0) != T32O_PTR(ob.T) ||
	    t32_ld32(ob.ctx, 4) != T32O_PTR(ob.B)) {
		if (failed++ < 5)
			printf("t32_aeinit_oracle: FAIL %s: context pointers\n", tag);
		bad = 1;
	}
	if (vwarn != warns) {
		if (failed++ < 5)
			printf("t32_aeinit_oracle: FAIL %s: printk %u / %u (S+56 %08x %08x %08x)\n", tag, vwarn, warns, t32_ld32(S, 56), t32_ld32(S, 60), t32_ld32(S, 64));
		bad = 1;
	}
	if (o_n != 8 || vn < 8) {
		if (failed++ < 5)
			printf("t32_aeinit_oracle: FAIL %s: writes %d / %u\n", tag, o_n, vn);
		return 1;
	}
	for (i = 0; i < 8; i++)
		if (t32o_log.w[vn - 8 + i].reg != ow[i].reg || t32o_log.w[vn - 8 + i].val != ow[i].val) {
			if (failed++ < 5)
				printf("t32_aeinit_oracle: FAIL %s: write %u vendor %x=%x ours %x=%x\n", tag, i,
				       t32o_log.w[vn - 8 + i].reg, t32o_log.w[vn - 8 + i].val, ow[i].reg, ow[i].val);
			return 1;
		}
	return bad;
}

static void shape(u8 *sec)
{
	u32 k, distinct;

	t32_wr16(sec, T32_AET_FRAC, (rnd() & 7) ? 4 + rnd() % 16 : rnd() % 32);
	sec[T32_AET_FRAC + 2] = 0; sec[T32_AET_FRAC + 3] = 0;
	t32_wr32(sec, T32_AET_MODE, rnd() % 3);
	t32_wr32(sec, T32_AET_NOCLAMP, (rnd() & 3) ? 0 : rnd());
	for (k = 1250; k < 1258; k++)
		sec[k] = (rnd() & 3) ? 1 : (u8)rnd();
	for (k = 0; k < 6; k++)	/* tuned limits near the sensor ones */
		if (rnd() & 1)
			t32_wr32(sec, T32_AET_IT_MIN + 4 * k, rnd() & 0xfff);
	if (rnd() & 1)
		t32_wr32(sec, T32_AET_RATIO_B, 0);
	t32_wr16(sec, 2770, rnd() % 16);	/* zone counts <= 15 (see t32_ae_zone_sizes) */
	t32_wr16(sec, 2774, rnd() % 16);
	t32_wr16(sec, 2768, rnd() % 64);
	t32_wr16(sec, 2772, rnd() % 64);
	if (!(rnd() & 3)) {	/* all entries distinct: index 15 */
		t32_wr32(sec, T32_AET_IT_MIN, 100 + (rnd() & 0xff));
		sec[1250] = 1;
		for (k = 0; k < 16; k++) {
			t32_wr32(sec, T32_AET_EXPLIST + 4 + 16 * k + 4, 1024 + 37 * k);
			t32_wr32(sec, T32_AET_EXPLIST + 4 + 16 * k + 8, 1024);
		}
		distinct = 1;
	} else {
		distinct = 0;
	}
	t32_wr32(sec, T32_AET_FLICKER_HZ, (rnd() & 1) ? 50 + (rnd() & 15) : rnd() & 0xffff);
	for (k = 1; k < 16; k++) {
		u32 to = T32_AET_EXPLIST + 4 + 16 * k;

		if (!(rnd() & 7))
			t32_wr32(sec, to, 0);
		else if (rnd() & 1)
			t32_wr32(sec, to, 1 + (rnd() & 0xffff));
		if (!distinct && (rnd() & 1)) {	/* repeated entries for the index search */
			t32_wr32(sec, to, t32_ld32(sec, to - 16));
			t32_wr32(sec, to + 4, t32_ld32(sec, to - 12));
			t32_wr32(sec, to + 8, t32_ld32(sec, to - 8));
		}
	}
}

static int bin_cases(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	int nbins = 0;

	if (!d)
		return 0;
	while ((e = readdir(d))) {
		char path[1024];
		static u8 b[1u << 20];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);
		u32 off, len = 0, r;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(b, 1, sizeof(b), f);
		fclose(f);
		off = t32_iq_section(b, (u32)bytes, T32_IQ_SEC_AE, &len);
		if (!off || len < T32_AEI_T_BYTES)
			continue;
		nbins++;
		for (r = 0; r < 20; r++)
			run_case(e->d_name, b + off, (int)r);
	}
	closedir(d);
	return nbins;
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int c, i, rc_r, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 12000;
	static u8 tmpl[T32_AEI_R_BYTES];
	int nbins;

	memcpy(d3a40_init, t32_data_d3a40, sizeof(d3a40_init));
	t32_ae_state_tmpl(tmpl);
	if (memcmp(tmpl, d3a40_init, T32_AEI_R_BYTES)) {
		printf("t32_aeinit_oracle: FAIL state template != d3a40 .data image\n");
		return 1;
	}
	for (c = 0; c < n; c++) {
		static u8 sec[T32_AEI_T_BYTES];

		for (i = 0; i < sizeof(sec); i++)
			sec[i] = (c % 3) ? (u8)rnd() : (u8)(rnd() & 7);
		shape(sec);
		run_case("random", sec, (int)c);
	}
	rc_r = compared;
	nbins = bin_cases(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32");
	printf("t32_aeinit_oracle: random %u compared, IQ bins %d (%u compared), %u mismatches\n", rc_r, nbins,
	       compared - rc_r, failed);
	printf("t32_aeinit_oracle: exposure list index 0 / 14 / 15: %u / %u / %u\n", idx_hist[0], idx_hist[14], idx_hist[15]);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_aeinit_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
