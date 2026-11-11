// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our lib_tisp_rcfg_switch_interrupt (driver/t32/tx_isp_t32_librcfg.h) against Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every register read / tisp_reg_write in order (with the number of system writes before
 * it), every system write (address, value, order), the return value.  Inputs: channel 0 / 1 / 2 / random
 * (random upper bits), channel count 0 / 1 / 2 / random, d03a0 = 0 and 1, random register values.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_librcfg.h"

u64 lib_tisp_rcfg_switch_interrupt(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

u32 t32_data_dc608[1];

static u32 rs = 0x38d01377u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct ev { u32 id, a[3], nw; };
#define NE 16
static struct ev ve[NE], oe[NE];
static unsigned int vn, on;
static u32 o_nw, seed;
static struct { u32 reg, val; } ow[8];

static void vadd(u32 id, u32 a0, u32 a1, u32 a2) { if (vn < NE) ve[vn] = (struct ev){ id, { a0, a1, a2 }, t32o_log.n }; vn++; }
static void oadd(u32 id, u32 a0, u32 a1, u32 a2) { if (on < NE) oe[on] = (struct ev){ id, { a0, a1, a2 }, o_nw }; on++; }
static u32 rdval(u32 reg) { return reg * 0x9e3779b1u ^ seed; }

static u32 v_rd(u32 reg) { vadd(1, reg, 0, 0); return rdval(reg); }
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; vadd(2, a0, a1, a2); return rnd(); }

static u32 o_rd(void *c, u32 reg) { (void)c; oadd(1, reg, 0, 0); return rdval(reg); }
static u32 o_rw(void *c, u32 ch, u32 reg, u32 val) { (void)c; oadd(2, ch, reg, val); return 0; }
static u32 o_wr(void *c, u32 reg, u32 val)
{
	(void)c;
	if (o_nw < 8) {
		ow[o_nw].reg = reg;
		ow[o_nw].val = val;
	}
	o_nw++;
	return 0;
}

int main(void)
{
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_rcfg_hooks h = { NULL, o_rd, o_wr, o_rw };
	static u8 sh[4];

	t32o_rd_hook = v_rd;
	for (c = 0; c < n; c++) {
		u32 k = rnd() % 5, ch = (rnd() & ~0xffu) | (k < 3 ? k : k == 3 ? 255 : rnd() & 0xff), cnt, r, o, bad;

		t32o_reset();
		vn = on = o_nw = 0;
		memset(ve, 0, sizeof(ve));
		memset(oe, 0, sizeof(oe));
		seed = rnd();
		k = rnd() % 5;
		cnt = k < 3 ? k : k == 3 ? 0x100 : rnd();
		t32_data_dc608[0] = cnt;
		t32_data_d03a0[0] = sh[0] = (u8)(c & 1);
		r = (u32)t32o_call(F(lib_tisp_rcfg_switch_interrupt), ch, rnd(), rnd(), rnd(), 0);
		o = t32_lib_rcfg_switch_interrupt(&h, cnt, sh, ch);
		bad = r != o || vn != on || memcmp(ve, oe, sizeof(ve)) || t32o_log.n != o_nw;
		for (i = 0; !bad && i < o_nw && i < 8; i++)
			bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u ch %x cnt %x sh %u: ret %x/%x calls %u/%u writes %u/%u\n", c, ch, cnt, sh[0],
			       r, o, vn, on, t32o_log.n, o_nw);
	}
	printf("t32_librcfg_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
