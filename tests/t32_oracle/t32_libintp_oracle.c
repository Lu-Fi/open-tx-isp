// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our ISP interrupt top half / dispatch (driver/t32/tx_isp_t32_libintp.h) against Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: register reads (address, order against the writes) and writes (address, value, order),
 * the whole .bss block e3cd0 .. e3efc (random counters), the output block, the handler calls (which handler,
 * argument, order) and the return value.  Inputs: random status words (also 0, single bits, bits 0 / 4 / 5
 * forced), handler tables with random empty slots, handler results 1 / 0 / random, channel with random upper bits.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libintp.h"

u64 lib_tisp_intp_handler(u32, u32, u32, u32);
u64 lib_tisp_intp_process(u32, u32, u32, u32);
u64 tisp_intp_static_clear(u32, u32, u32, u32);

/* recorder of the vendor's memcpy(dst, NULL, len) in tisp_intp_static_clear (fix_281_intpclr.sh) */
static u32 mc[8][3];
static unsigned int mcn;
u64 t32o281_memcpy(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	if (mcn < 8) {
		mc[mcn][0] = a0;
		mc[mcn][1] = a1;
		mc[mcn][2] = a2;
	}
	mcn++;
	return a0;
}

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define BLK	0x22c

u8 t32o_intpblk[BLK] __attribute__((aligned(16)));
u32 t32_data_e3b50[96];
__asm__(".globl t32_data_e3cd0\n.set t32_data_e3cd0, t32o_intpblk\n"
	".globl t32_data_e3cd8\n.set t32_data_e3cd8, t32o_intpblk + 0x8\n"
	".globl t32_data_e3cf0\n.set t32_data_e3cf0, t32o_intpblk + 0x20\n"
	".globl t32_data_e3d70\n.set t32_data_e3d70, t32o_intpblk + 0xa0\n"
	".globl t32_data_e3df0\n.set t32_data_e3df0, t32o_intpblk + 0x120\n"
	".globl t32_data_e3df8\n.set t32_data_e3df8, t32o_intpblk + 0x128\n"
	".globl t32_data_e3e78\n.set t32_data_e3e78, t32o_intpblk + 0x1a8\n"
	".globl t32_data_e3ef8\n.set t32_data_e3ef8, t32o_intpblk + 0x228\n");

static u32 rs = 0x7f4a7c15u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct ev { u32 id, a, nw; };
#define NE 128
static struct ev ve[NE], oe[NE];
static unsigned int vn, on, o_nw;
static u32 regs[0x100], hret[NE], vh, oh;

static void vrec(u32 id, u32 a) { if (vn < NE) ve[vn] = (struct ev){ id, a, t32o_log.n }; vn++; }
static void orec(u32 id, u32 a) { if (on < NE) oe[on] = (struct ev){ id, a, o_nw }; on++; }
static u32 rdval(u32 reg) { return regs[(reg >> 2) & 0xff]; }

static u32 v_rd(u32 reg) { vrec(1, reg); return rdval(reg); }
static u32 o_rd(void *c, u32 reg) { (void)c; orec(1, reg); return rdval(reg); }
static struct { u32 reg, val; } ow[16];
static u32 o_wr(void *c, u32 reg, u32 val)
{
	(void)c;
	if (o_nw < 16) {
		ow[o_nw].reg = reg;
		ow[o_nw].val = val;
	}
	o_nw++;
	return 0;
}

#define VH(k) u64 vh##k(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(10 + k, a0); return hret[vh++ % NE]; }
#define OH(k) static u32 oh##k(u32 ch) { orec(10 + k, ch); return hret[oh++ % NE]; }
VH(0) VH(1) VH(2) VH(3)
OH(0) OH(1) OH(2) OH(3)
static u64 (*const vtab[4])(u32, u32, u32, u32) = { vh0, vh1, vh2, vh3 };
static const t32_intp_fn otab[4] = { oh0, oh1, oh2, oh3 };

static u32 pick(unsigned int c)
{
	switch (c % 6) {
	case 0: return 0;
	case 1: return 1u << (rnd() & 31);
	case 2: return rnd() | 0x31;
	default: return rnd();
	}
}

int main(void)
{
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_lib_hooks h;
	static struct t32_intp_glob og;
	static t32_intp_fn ohnd[96];

	memset(&h, 0, sizeof(h));
	h.sys_read = o_rd;
	h.sys_write = o_wr;
	t32o_rd_hook = v_rd;
	for (c = 0; c < n; c++) {
		u32 r, o, ch = rnd(), *vout, oout[8], bad, k;

		t32o_reset();
		vn = on = o_nw = vh = oh = 0;
		memset(ve, 0, sizeof(ve));
		memset(oe, 0, sizeof(oe));
		vout = t32o_alloc(32);
		for (i = 0; i < 8; i++)
			vout[i] = oout[i] = rnd();
		for (i = 0; i < BLK; i++)
			t32o_intpblk[i] = (u8)rnd();
		memcpy(&og, t32o_intpblk, sizeof(og));
		for (i = 0; i < 0x100; i++)
			regs[i] = pick(rnd());
		for (i = 0; i < NE; i++)
			hret[i] = c % 10 == 2 || rnd() % 3 == 0 ? 1 : rnd() % 2 ? 0 : rnd();	/* also: all handled */
		for (i = 0; i < 96; i++) {
			k = rnd() % 5;
			t32_data_e3b50[i] = k == 4 ? 0 : (u32)(uintptr_t)vtab[k];
			ohnd[i] = k == 4 ? NULL : otab[k];
		}
		if (c & 1) {
			r = (u32)t32o_call(F(lib_tisp_intp_handler), T32O_PTR(vout), 0, 0, 0, 0) - T32O_PTR(vout);
			o = (u32)((uintptr_t)t32_lib_intp_handler(&h, &og, oout) - (uintptr_t)oout);
		} else {
			for (i = 0; i < 6; i++) {
				u32 w = c % 10 == 0 ? 0 : pick(c / 2 + i);	/* also: no bit set */

				memcpy(t32o_intpblk + 8 + 4 * i, &w, 4);
				og.st[i] = w;
			}
			r = (u32)t32o_call(F(lib_tisp_intp_process), ch, 0, 0, 0, 0);
			o = t32_lib_intp_process(&og, ohnd, ch);
		}
		bad = r != o || vn != on || memcmp(ve, oe, sizeof(ve)) || t32o_log.n != o_nw ||
		      memcmp(t32o_intpblk, &og, sizeof(og)) || memcmp(vout, oout, 32) || vn > NE;
		for (i = 0; !bad && i < o_nw && i < 16; i++)
			bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH %s case %u: ret %x/%x events %u/%u writes %u/%u\n", c & 1 ? "handler" : "process",
			       c, r, o, vn, on, t32o_log.n, o_nw);
	}
	/*
	 * BEYOND VENDOR (#281) tisp_intp_static_clear: the vendor must store n2 = 0 and call
	 * memcpy(dst, NULL, len) exactly for the five counter arrays ours zeroes (recorded, not run);
	 * every other byte of the block is unchanged on both sides.
	 */
	for (c = 0; c < 2000; c++) {
		static const u32 off[5] = { 0x1a8, 0x128, 0x120, 0xa0, 0x20 }, len[5] = { 128, 128, 8, 128, 128 };
		u8 vb[BLK];
		u32 bad = 0;

		t32o_reset();
		for (i = 0; i < BLK; i++)
			t32o_intpblk[i] = (u8)rnd();
		memcpy(&og, t32o_intpblk, sizeof(og));
		mcn = 0;
		t32o_call(F(tisp_intp_static_clear), 0, 0, 0, 0, 0);
		t32_lib_intp_static_clear(&og);
		memcpy(vb, t32o_intpblk, BLK);
		bad |= mcn != 5 || t32o_log.n != 0;
		for (i = 0; i < 5 && i < mcn; i++) {
			bad |= mc[i][0] != T32O_PTR(t32o_intpblk + off[i]) || mc[i][1] != 0 || mc[i][2] != len[i];
			memset(vb + off[i], 0, len[i]);		/* the one named difference: ours zeroes, the vendor reads 0 */
		}
		bad |= memcmp(vb, &og, sizeof(og)) != 0;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH static_clear case %u: memcpy calls %u\n", c, mcn);
	}
	printf("t32_libintp_oracle: %u compared (2000 static_clear, vendor memcpy from NULL recorded), %u mismatches\n",
	       compared, failed);
	return failed ? 1 : 0;
}
