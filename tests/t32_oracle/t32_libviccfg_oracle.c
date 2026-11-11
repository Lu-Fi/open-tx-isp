// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our VIC input configuration (driver/t32/tx_isp_t32_libviccfg.h: lib_tisp_vic_cfg) against Matt
 * Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc; REFFIX libviccfg_fix.sh sends its register
 * writes / reads into this oracle's ordered log).
 *
 * Compared bit-exact: one ordered log of the vic_reset call (a0, attr), every register write (address, value),
 * every register read (address) and every print (level, source line); the VIC state words of all 4 channels
 * (e3f20 targets, random start), e3f00 / dc284 / attr unchanged; the return value.
 * Inputs: a0 with random upper bits and channel 0..3 (dc284 holds 2 channels: channels 2 / 3 read the bytes
 * after it, read only), interface type 0..6 (mostly 1 .. 5), sub modes / lanes / bus width /
 * path near the decoded cases, MIPI formats from every range edge, random fields elsewhere.
 * The register access is direct (system_reg_write), so d03a0 is not involved.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libviccfg.h"

u64 lib_tisp_vic_cfg(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
enum { ID_RESET = 1, ID_WR, ID_RD, ID_LOG };

u32 t32_data_e3f20[4], t32_data_e3f00[16];
char t32_data_c2ec4[4] = "f", t32_data_cad50[4] = "f", t32_data_cad70[4] = "f", t32_data_cad98[4] = "f",
     t32_data_cadc0[4] = "f", t32_data_cadf0[4] = "f", t32_data_cae0c[4] = "f", t32_data_cae44[4] = "f",
     t32_data_cae84[4] = "f", t32_data_caec0[4] = "f", t32_data_caef0[4] = "f", t32_data_caf14[4] = "f",
     t32_data_caf30[4] = "f", t32_data_caf58[4] = "f", t32_data_caf7c[4] = "f", t32_data_cafa0[4] = "f";

static u32 rs = 0x2b8e51c7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct call { u32 id, a, b; };
#define NC 128
static struct call vc[NC], oc[NC];
static unsigned int vn, on;

static void rec(struct call *t, unsigned int *n, u32 id, u32 a, u32 b)
{
	if (*n < NC)
		t[*n] = (struct call){ id, a, b };
	(*n)++;
}

u64 lib_tisp_vic_reset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rec(vc, &vn, ID_RESET, a0, a1); return rnd(); }
u64 t32o7_wr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rec(vc, &vn, ID_WR, a0, a1); return rnd(); }
u64 t32o7_rd(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; rec(vc, &vn, ID_RD, a0, 0); return rnd(); }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; rec(vc, &vn, ID_LOG, a0, a3); return 0; }

static void o_reset(void *c, u32 a0, const u8 *attr) { (void)c; rec(oc, &on, ID_RESET, a0, T32O_PTR(attr)); }
static void o_wr(void *c, u32 r, u32 v) { (void)c; rec(oc, &on, ID_WR, r, v); }
static u32 o_rd(void *c, u32 r) { (void)c; rec(oc, &on, ID_RD, r, 0); return rnd(); }
static void o_log(void *c, u32 l, u32 line) { (void)c; rec(oc, &on, ID_LOG, l, line); }

static void put32(u8 *p, u32 o, u32 v) { memcpy(p + o, &v, 4); }
static u32 pick(const u32 *t, unsigned int n) { return rnd() % 5 ? t[rnd() % n] : rnd(); }

int main(void)
{
	static const u32 fmts[] = { 20735, 20736, 20739, 20740, 20991, 20992, 20995, 20996, 20999, 21000, 21003,
				    21004, 21247, 21248, 21263, 21264, 21279, 21280, 21295, 21296 };
	static const u32 types[] = { 0, 1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5 };
	static const u32 small[] = { 0, 1, 2, 3, 4, 7 };
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;
	unsigned int compared = 0, failed = 0;
	struct t32_vc_hooks h = { NULL, o_reset, o_wr, o_rd, o_log };
	static u32 st_o[4][2], *stp_o[4];
	static u8 attr_s[256], par_s[1808];
	static u32 fmt_s[16];
	struct t32_vc_env e = { stp_o, t32_data_e3f00, t32_data_dc284 };

	for (c = 0; c < n; c++) {
		u32 a0 = (rnd() & ~0xffu) | (c & 3), r, ro, bad;
		u8 *attr;
		u32 *st_v;

		t32o_reset();
		vn = on = 0;
		memset(vc, 0, sizeof(vc));
		memset(oc, 0, sizeof(oc));
		attr = t32o_alloc(256);
		st_v = t32o_alloc(32);
		for (i = 0; i < 256; i++)
			attr[i] = (u8)rnd();
		for (i = 32; i < 132; i += 4)	/* the 1-bit .. 4-bit fields of the DVP control word: mostly small */
			if (rnd() % 4)
				put32(attr, i, rnd() % 4);
		put32(attr, 20, pick(types, 12));
		put32(attr, 24, pick(small, 6));
		put32(attr, 28, pick(small, 6));
		put32(attr, 120, pick(small, 6));
		put32(attr, 128, pick(small, 6));
		put32(attr, 48, rnd() % 4096);
		if (rnd() % 3 == 0)
			memset(attr + 32, 0, 4);	/* crop / frame halfwords 0 */
		if (rnd() % 3 == 0)
			attr[36] = 2;
		if (rnd() % 3 == 0)
			attr[37] = 2;
		for (i = 0; i < 1808; i++)
			t32_data_dc284[i] = (u8)rnd();
		for (i = 0; i < 16; i++)
			t32_data_e3f00[i] = rnd() % 4 ? rnd() % 4096 : rnd();
		for (i = 0; i < 4; i++) {
			t32_data_e3f00[4 * i + 2] = pick(fmts, 20);
			t32_data_e3f00[4 * i + 3] = pick(small, 6);
		}
		for (i = 0; i < 8; i++)
			st_v[i] = rnd();
		for (i = 0; i < 4; i++) {
			t32_data_e3f20[i] = T32O_PTR(st_v + 2 * i);
			st_o[i][0] = st_v[2 * i];
			st_o[i][1] = st_v[2 * i + 1];
			stp_o[i] = st_o[i];
		}
		memcpy(attr_s, attr, 256);
		memcpy(par_s, t32_data_dc284, 1808);
		memcpy(fmt_s, t32_data_e3f00, 64);

		r = (u32)t32o_call(F(lib_tisp_vic_cfg), a0, T32O_PTR(attr), rnd(), rnd(), 0);
		ro = t32_lib_vic_cfg(&h, &e, a0, attr);
		bad = r != ro || vn != on || vn > NC || memcmp(vc, oc, sizeof(vc)) || memcmp(attr_s, attr, 256) ||
		      memcmp(par_s, t32_data_dc284, 1808) || memcmp(fmt_s, t32_data_e3f00, 64);
		for (i = 0; !bad && i < 4; i++)
			bad = st_v[2 * i] != st_o[i][0] || st_v[2 * i + 1] != st_o[i][1] ||
			      t32_data_e3f20[i] != T32O_PTR(st_v + 2 * i);
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u a0 %x type %u: calls %u/%u ret %x/%x\n", c, a0, t32_vc_rd32(attr_s, 20),
			       vn, on, r, ro);
	}
	printf("t32_libviccfg_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
