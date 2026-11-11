// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: lib_tisp_save_specified_frame_raw (tx_isp_t32_librest.h)
 * against the vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc;
 * printk, isp_printf, system_reg_write and tisp_vic_mdma_enable renamed to
 * recorders by fix_op20_sfraw.sh).  Per case random VIC .bss block
 * (t32_data_e3f00 .. e3f5f), VIC configs (busy word 0 or not), request
 * (mode 0 / 1 / 2 / other, NULL paddr, frame count 0, frame index around
 * 1 .. 10, format 0 / 1 / 2 / other) and channel (0 / 1, with random high
 * bits above the & 0xff).  Compared: every call in order (printk message,
 * line and the values its format prints, isp_printf, register writes,
 * mdma_enable arguments), every byte of the .bss block and both configs,
 * and the return value except where the vendor returns a left-over address
 * constant (capture only armed).  Channels >= 2 (& 0xff) are not run: the
 * vendor then reads its config pointer past e3f20 (vendor UB).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_librest.h"

extern u64 lib_tisp_save_specified_frame_raw(u32, u32, u32, u32);

/* the vendor .bss block t32_data_e3f00 .. e3f5f */
u8 op20_vicblk[0x60] __attribute__((aligned(16)));
__asm__(".globl t32_data_e3f00\n.set t32_data_e3f00, op20_vicblk\n"
	".globl t32_data_e3f20\n.set t32_data_e3f20, op20_vicblk + 0x20\n"
	".globl t32_data_e3f28\n.set t32_data_e3f28, op20_vicblk + 0x28\n"
	".globl t32_data_e3f2c\n.set t32_data_e3f2c, op20_vicblk + 0x2c\n"
	".globl t32_data_e3f30\n.set t32_data_e3f30, op20_vicblk + 0x30\n"
	".globl t32_data_e3f34\n.set t32_data_e3f34, op20_vicblk + 0x34\n"
	".globl t32_data_e3f40\n.set t32_data_e3f40, op20_vicblk + 0x40\n"
	".globl t32_data_e3f44\n.set t32_data_e3f44, op20_vicblk + 0x44\n"
	".globl t32_data_e3f48\n.set t32_data_e3f48, op20_vicblk + 0x48\n");
/* printk / isp_printf formats (data.c:14749, 14762, 14510, 14402, 14412, 14776) and the function name
 * (data.c:6063): address only */
u8 t32_data_cb5e0[4], t32_data_cb614[4], t32_data_cb224[4], t32_data_cb074[4], t32_data_cb09c[4], t32_data_cb64c[4];
u8 t32_data_c2e30[4];

static u32 rs = 0x7f4a7c15u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_PRINTK = 1, C_ISPPR, C_WRITE, C_MDMA };
#define LOGN 16
struct hlog { unsigned int n; u32 e[LOGN][7]; };
static struct hlog vl, ol, *cur;
static u32 tok[LOGN];
static u8 *ourpar;

static u32 rec(u32 k, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f)
{
	unsigned int i = cur->n;

	if (i < LOGN) { cur->e[i][0] = k; cur->e[i][1] = a; cur->e[i][2] = b; cur->e[i][3] = c;
			cur->e[i][4] = d; cur->e[i][5] = e; cur->e[i][6] = f; }
	cur->n++;
	return i < LOGN ? tok[i] : 0;
}

static int bad_name;
u64 op20_sfr_printk(u32 a0, u32 a1, u32 a2, u32 a3, u32 s16, u32 s20, u32 s24)
{
	u32 msg = 99;

	if (a1 != T32O_PTR(t32_data_c2e30)) bad_name = 1;
	if (a0 == T32O_PTR(t32_data_cb5e0)) { msg = T32_SFR_MSG_FRAMENUM; s16 = s20 = s24 = 0; }
	else if (a0 == T32O_PTR(t32_data_cb614)) { msg = T32_SFR_MSG_PADDR; a3 = s16 = s20 = s24 = 0; }
	else if (a0 == T32O_PTR(t32_data_cb224)) { msg = T32_SFR_MSG_INDEX; s16 = s20 = s24 = 0; }
	else if (a0 == T32O_PTR(t32_data_cb09c)) { msg = T32_SFR_MSG_STRIDE; s16 = s20 = s24 = 0; }
	else if (a0 == T32O_PTR(t32_data_cb64c)) msg = T32_SFR_MSG_INFO;
	return rec(C_PRINTK, msg, a2, a3, s16, s20, s24);
}
u64 op20_sfr_isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (a2 != T32O_PTR(t32_data_c2e30)) bad_name = 1;
	return rec(C_ISPPR, a0, a1 == T32O_PTR(t32_data_cb074) ? T32_SFR_MSG_FORMAT : 99, a3, 0, 0, 0);
}
u64 op20_sfr_wr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return rec(C_WRITE, a0, a1, 0, 0, 0, 0); }
u64 op20_sfr_mdma(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return rec(C_MDMA, a0 == T32O_PTR(op20_vicblk + 0x34) ? 1 : a0, a1, a2, a3, 0, 0);
}

static u32 o_printk(void *c, u32 msg, u32 line, u32 a, u32 b, u32 d, u32 e)
{ (void)c; return rec(C_PRINTK, msg, line, a, b, d, e); }
static void o_isppr(void *c, u32 lv, u32 msg, u32 line) { (void)c; rec(C_ISPPR, lv, msg, line, 0, 0, 0); }
static void o_wr(void *c, u32 reg, u32 val) { (void)c; rec(C_WRITE, reg, val, 0, 0, 0, 0); }
static u32 o_mdma(void *c, u8 *par, u32 stride, u32 en, u32 dma)
{ (void)c; return rec(C_MDMA, par == ourpar ? 1 : T32O_PTR(par), stride, en, dma, 0, 0); }

static u32 rd(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static void wr(u8 *p, u32 v) { memcpy(p, &v, 4); }

#define CFG 120
int main(void)
{
	static const struct t32_sfr_hooks h = { NULL, o_printk, o_isppr, o_wr, o_mdma };
	u8 *cfg[2], cinit[2][CFG], cours[2][CFG], blk[0x60];
	unsigned long n, N = 40000, ret_cmp = 0, mdma = 0, armed = 0, errs = 0;
	u32 i;

	u8 *vreq = t32o_alloc(16);

	for (i = 0; i < 2; i++) cfg[i] = t32o_alloc(CFG);
	for (n = 0; n < N; n++) {
		struct t32_vic_glob g;
		struct t32_sfr_req r;
		u8 rq[16];
		u32 ch, rv, ro, sel;

		for (i = 0; i < LOGN; i++) tok[i] = rnd();
		for (i = 0; i < sizeof(blk); i++) blk[i] = (u8)rnd();
		for (i = 0; i < 2; i++) {	/* widths / heights: small, around the 8 alignment, or large (wrap) */
			sel = rnd() & 3;
			wr(blk + 16 * i, sel == 0 ? rnd() : sel == 1 ? (rnd() & 0xfff) : (rnd() & 0x1ff) + 0x7ffffff0u * (sel == 3));
			wr(blk + 16 * i + 4, (rnd() & 1) ? rnd() & 0xfff : rnd());
			wr(blk + 0x20 + 4 * i, T32O_PTR(cfg[i]));
			for (u32 k = 0; k < CFG; k++) cinit[i][k] = (u8)rnd();
			wr(cinit[i] + T32_VIC_CFG_BUSY, (rnd() & 1) ? 0 : rnd() | 1);
		}
		if (rnd() & 3) blk[0x34 + T32_VIC_PAR_DMA] = 1;
		blk[0x34 + T32_VIC_PAR_INDEX] = (u8)rnd();

		for (i = 0; i < 16; i++) rq[i] = (u8)rnd();
		sel = rnd() % 5;
		wr(rq, sel < 4 ? sel : rnd());
		if (!(rnd() & 15)) wr(rq + 4, 0);
		if (!(rnd() & 15)) rq[12] = 0;
		sel = rnd() & 3;
		rq[14] = sel == 0 ? (u8)rnd() : sel == 1 ? 1 : (u8)(rnd() % 13);
		sel = rnd() & 3;
		rq[13] = sel == 3 ? (u8)rnd() : (u8)sel;
		memcpy(&r, rq, sizeof(r));
		ch = (rnd() & 1) | ((rnd() & 1) ? rnd() << 8 : 0);

		/* ours */
		memcpy(g.size, blk, 32);
		g.cfg[0] = cfg[0]; g.cfg[1] = cfg[1];
		g.mdma_a1 = rd(blk + 0x28); g.raw_dma = rd(blk + 0x2c); g.raw_buf = (void *)(uintptr_t)rd(blk + 0x30);
		memcpy(g.par, blk + 0x34, 20); g.b48 = blk[0x48]; g.b49 = blk[0x49];
		g.frame_cnt = rd(blk + 0x4c); memcpy(g.wq, blk + 0x50, 16);
		for (i = 0; i < 2; i++) memcpy(cfg[i], cinit[i], CFG);
		ourpar = g.par;
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		ro = t32_lib_save_frame_raw(&h, &g, ch, &r);
		for (i = 0; i < 2; i++) memcpy(cours[i], cfg[i], CFG);

		/* vendor */
		t32o_reset();
		memcpy(op20_vicblk, blk, sizeof(blk));
		for (i = 0; i < 2; i++) memcpy(cfg[i], cinit[i], CFG);
		memcpy(vreq, rq, 16);
		memset(&vl, 0, sizeof(vl)); cur = &vl; bad_name = 0;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))lib_tisp_save_specified_frame_raw, ch, T32O_PTR(vreq),
				    rnd(), rnd(), 0);

		/* the vendor returns a left-over constant when the capture is only armed (no tail call) */
		int last_tail = vl.n && vl.n <= LOGN && (vl.e[vl.n - 1][0] == C_MDMA ||
			(vl.n == 1 && vl.e[0][0] == C_PRINTK && vl.e[0][1] <= T32_SFR_MSG_INDEX));
		int bad = bad_name || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || (last_tail && rv != ro);
		if (last_tail) ret_cmp++;
		if (vl.n && vl.e[vl.n - 1][0] == C_MDMA) mdma++;
		else if (vl.n == 1) errs++;
		else armed++;
		u8 vb[0x60];
		memcpy(vb, op20_vicblk, sizeof(vb));
		bad |= memcmp(vb, g.size, 32) || rd(vb + 0x20) != T32O_PTR(g.cfg[0]) || rd(vb + 0x24) != T32O_PTR(g.cfg[1]) ||
		       rd(vb + 0x28) != g.mdma_a1 || rd(vb + 0x2c) != g.raw_dma || rd(vb + 0x30) != T32O_PTR(g.raw_buf) ||
		       memcmp(vb + 0x34, g.par, 20) || vb[0x48] != g.b48 || vb[0x49] != g.b49 ||
		       rd(vb + 0x4c) != g.frame_cnt || memcmp(vb + 0x50, g.wq, 16) ||
		       memcmp(cfg[0], cours[0], CFG) || memcmp(cfg[1], cours[1], CFG) || memcmp(vreq, rq, 16);
		if (bad) {
			printf("t32_sfraw_oracle: MISMATCH case %lu ch %x mode %08x idx %u num %u: ret %08x/%08x calls %u/%u name %d\n",
			       n, ch, r.mode, r.frame_index, r.frame_num, rv, ro, vl.n, ol.n, bad_name);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  v %u %08x %08x %08x %08x %08x %08x | o %u %08x %08x %08x %08x %08x %08x\n",
				       vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3], vl.e[i][4], vl.e[i][5], vl.e[i][6],
				       ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3], ol.e[i][4], ol.e[i][5], ol.e[i][6]);
			return 1;
		}
	}
	printf("t32_sfraw_oracle: %lu cases (%lu errors, %lu DMA started, %lu armed; %lu returns compared), 0 mismatches\n",
	       N, errs, mdma, armed, ret_cmp);
	return 0;
}
