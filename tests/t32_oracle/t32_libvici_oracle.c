// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our VIC interrupt handler (driver/t32/tx_isp_t32_libvici.h: lib_tisp_vic_intp_handle) against
 * Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every callee call in order (register reads with address, MDMA enable arguments, completions,
 * printk source lines, isp_printf), every register write (address, value, order against the calls), the whole
 * .bss block from t32_data_e3f00 (random start values, incl. e3f40 / e3f48 / e3f49 / e3f58), both VIC configs
 * (all counters), the 20-byte result.  REFFIX libvici_fix.sh (recording printk and private_complete).
 * Inputs: random status / DMA status (with forced 0, single bits, frame bit 4, error bits), frame status & 7 = 0 /
 * not 0, re-arm pending 0 / random with par byte 10 = counter + 1 or not, frames left 1 / random, DMA flag 0 / 1.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libvici.h"

u64 lib_tisp_vic_intp_handle(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

u8 t32o_vicblk[0x60] __attribute__((aligned(16)));
__asm__(".globl t32_data_e3f20\n.set t32_data_e3f20, t32o_vicblk + 0x20\n"
	".globl t32_data_e3f28\n.set t32_data_e3f28, t32o_vicblk + 0x28\n"
	".globl t32_data_e3f2c\n.set t32_data_e3f2c, t32o_vicblk + 0x2c\n"
	".globl t32_data_e3f30\n.set t32_data_e3f30, t32o_vicblk + 0x30\n"
	".globl t32_data_e3f34\n.set t32_data_e3f34, t32o_vicblk + 0x34\n"
	".globl t32_data_e3f40\n.set t32_data_e3f40, t32o_vicblk + 0x40\n"
	".globl t32_data_e3f48\n.set t32_data_e3f48, t32o_vicblk + 0x48\n"
	".globl t32_data_e3f49\n.set t32_data_e3f49, t32o_vicblk + 0x49\n"
	".globl t32_data_e3f4c\n.set t32_data_e3f4c, t32o_vicblk + 0x4c\n"
	".globl t32_data_e3f58\n.set t32_data_e3f58, t32o_vicblk + 0x58\n");
char t32_data_c2ea8[4] = "f", t32_data_cb038[4] = "f", t32_data_cb068[4] = "f";

static u32 rs = 0x3e9a5c11u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_RD = 1, C_WR, C_MDMA, C_COMPLETE, C_TRACE, C_LOG };
struct call { u32 id, a[4], nw; };
#define NC 48
static struct call vc[NC], oc[NC];
static unsigned int vn, on, o_nw;
static u32 regval[2][3];
static struct t32_vic_glob og;

static void vrec(u32 id, u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (vn < NC)
		vc[vn] = (struct call){ id, { a0, a1, a2, a3 }, t32o_log.n };
	vn++;
}
static void orec(u32 id, u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (on < NC)
		oc[on] = (struct call){ id, { a0, a1, a2, a3 }, o_nw };
	on++;
}
static u32 rdval(u32 reg)
{
	u32 vic = (reg >> 12) & 1, k = (reg & 0xfff) == T32_VICI_STATUS ? 0 : (reg & 0xfff) == T32_VICI_DMA_STATUS ? 1 : 2;

	return regval[vic][k];
}

/* vendor side callees */
static u32 v_rd(u32 reg) { vrec(C_RD, reg, 0, 0, 0); return rdval(reg); }
u64 tisp_vic_mdma_enable(u32 a0, u32 a1, u32 a2, u32 a3) { vrec(C_MDMA, a0 == T32O_PTR(t32o_vicblk + 0x34), a1, a2, a3); return rnd(); }
u64 t32o7_complete(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_COMPLETE, a0 == T32O_PTR(t32o_vicblk + 0x4c), 0, 0, 0); return rnd(); }
u64 t32o7_printk(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a3; vrec(C_TRACE, a2, 0, 0, 0); return rnd(); }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; vrec(C_LOG, a0, a3, 0, 0); return 0; }

/* our hooks */
static u32 o_rd(void *c, u32 reg) { (void)c; orec(C_RD, reg, 0, 0, 0); return rdval(reg); }
static u32 o_wr(void *c, u32 reg, u32 val) { (void)c; orec(C_WR, reg, val, 0, 0); o_nw++; return 0; }
static u32 o_mdma(void *c, u8 *p, u32 a1, u32 en, u32 d) { (void)c; orec(C_MDMA, p == og.par, a1, en, d); return 0; }
static void o_complete(void *c, void *x) { (void)c; orec(C_COMPLETE, x == (void *)&og.frame_cnt, 0, 0, 0); }
static void o_trace(void *c, u32 line, u32 a, u32 b, u32 d) { (void)c; (void)a; (void)b; (void)d; orec(C_TRACE, line, 0, 0, 0); }
static void o_log(void *c, u32 level, u32 line) { (void)c; orec(C_LOG, level, line, 0, 0); }

static u32 pick_bits(u32 c)
{
	switch (c % 6) {
	case 0: return 0;
	case 1: return 1u << (rnd() % 32);
	case 2: return rnd() & 0x1ff;
	case 3: return T32_VICI_BIT_FRAME | (rnd() & 0x3f);
	default: return rnd();
	}
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;
	unsigned int compared = 0, failed = 0;
	struct t32_vici_hooks h = { { { NULL, o_rd, o_wr, NULL, NULL, NULL, NULL, NULL, NULL, o_mdma, NULL, o_log, NULL },
				      o_trace, NULL, NULL, NULL, NULL, NULL, NULL }, o_complete };
	static u8 cfg_o[2][120];

	t32o_rd_hook = v_rd;
	for (c = 0; c < n; c++) {
		u32 k, bad, vic, w;
		u8 *cfg_v[2], *out_v;
		unsigned int vi, oi;
		struct t32_vici_result out_o;

		t32o_reset();
		vn = on = o_nw = 0;
		memset(vc, 0, sizeof(vc));
		memset(oc, 0, sizeof(oc));
		for (i = 0; i < sizeof(t32o_vicblk); i++)
			t32o_vicblk[i] = (u8)rnd();
		for (vic = 0; vic < 2; vic++) {
			cfg_v[vic] = t32o_alloc(120);
			for (i = 0; i < 120; i++)
				cfg_v[vic][i] = cfg_o[vic][i] = (u8)rnd();
			w = T32O_PTR(cfg_v[vic]);
			memcpy(t32o_vicblk + 0x20 + 4 * vic, &w, 4);
			og.cfg[vic] = cfg_o[vic];
			regval[vic][0] = pick_bits(c + 3 * vic);
			regval[vic][1] = c % 4 == 0 ? 0 : (c % 4 == 1 ? rnd() & 3 : rnd());
			regval[vic][2] = rnd() % 3 ? rnd() : rnd() & ~7u;
		}
		t32o_vicblk[0x40] = c % 3 == 0 ? 0 : (u8)rnd();
		if (c % 2) {	/* re-arm frame = VIC 0 counter + 1 after the count (+1 when status bit 0 is set) */
			w = rnd() % 250;	/* the vendor compares a byte with the 32-bit counter + 1 */
			memcpy(cfg_v[0] + 8, &w, 4);
			memcpy(cfg_o[0] + 8, &w, 4);
			t32o_vicblk[0x34 + 10] = (u8)(w + 1 + (regval[0][0] & 1));
		}
		t32o_vicblk[0x48] = c % 5 == 0 ? 1 : (c % 5 == 1 ? 2 : (u8)rnd());
		t32o_vicblk[0x34 + 4] = (u8)(c % 3 == 2 ? rnd() : rnd() & 1);
		memcpy(&og.mdma_a1, t32o_vicblk + 0x28, 4);
		memcpy(&og.raw_dma, t32o_vicblk + 0x2c, 4);
		memcpy(og.par, t32o_vicblk + 0x34, 20);
		og.b48 = t32o_vicblk[0x48];
		og.b49 = t32o_vicblk[0x49];
		memcpy(&og.frame_cnt, t32o_vicblk + 0x4c, 4);
		memcpy(og.wq, t32o_vicblk + 0x50, 16);
		out_v = t32o_alloc(20);
		memset(out_v, 0xa5, 20);
		memset(&out_o, 0xa5, sizeof(out_o));

		k = (u32)t32o_call(F(lib_tisp_vic_intp_handle), T32O_PTR(out_v), rnd(), rnd(), rnd(), 0);
		t32_lib_vic_intp_handle(&h, &og, &out_o);
		bad = k != T32O_PTR(out_v);
		for (vi = oi = 0; !bad && (vi < vn || oi < on);) {
			if (oi < on && oc[oi].id == C_WR) {
				oi++;
				continue;
			}
			if (vi >= vn || oi >= on || memcmp(&vc[vi], &oc[oi], sizeof(vc[0])))
				bad = 1;
			vi++;
			oi++;
		}
		for (i = 0, k = 0; !bad && i < on && i < NC; i++)
			if (oc[i].id == C_WR) {
				bad = k >= t32o_log.n || t32o_log.w[k].reg != oc[i].a[0] || t32o_log.w[k].val != oc[i].a[1];
				k++;
			}
		bad |= k != t32o_log.n || vn > NC || on > NC || memcmp(out_v, &out_o, 20) ||
		       memcmp(cfg_v[0], cfg_o[0], 120) || memcmp(cfg_v[1], cfg_o[1], 120) ||
		       memcmp(t32o_vicblk + 0x28, &og.mdma_a1, 4) || memcmp(t32o_vicblk + 0x2c, &og.raw_dma, 4) ||
		       memcmp(t32o_vicblk + 0x34, og.par, 20) || t32o_vicblk[0x48] != og.b48 ||
		       t32o_vicblk[0x49] != og.b49 || memcmp(t32o_vicblk + 0x4c, &og.frame_cnt, 4) ||
		       memcmp(t32o_vicblk + 0x50, og.wq, 16);
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u: st %x/%x dma %x/%x calls %u/%u writes %u\n", c, regval[0][0],
			       regval[1][0], regval[0][1], regval[1][1], vn, on, t32o_log.n);
	}
	printf("t32_libvici_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
