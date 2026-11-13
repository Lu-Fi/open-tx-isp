// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our lib_tisp_stream_on (driver/t32/tx_isp_t32_libstream.h) against Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value, every callee call / register read in order (with the number of register
 * writes and printk traces before it), every register write (address, value, order), the trace count.
 * Inputs: configured byte 1 / 0 / random, sensor number 1 / 2 / random, poll done after 1, random, 19999,
 * 20000 reads or never (timeout).  printk is env.c's (counted; its source line is not compared).  REFFIX
 * single_lsc_trig_rec.sh points the reference at the recording tisp_single_lsc_rcfg_cfg_trig stub below.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libstream.h"

u64 lib_tisp_stream_on(u32, u32, u32, u32);
extern u8 t32_data_dd674[4];

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

char t32_data_bf264[4] = "f", t32_data_c7f30[4] = "f", t32_data_c7fa4[4] = "f", t32_data_c7f90[4] = "f";

static u32 rs = 0xbe5466cfu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct side { u32 nrd, hash, nev, ev[8][4]; };
static struct side vs, os;
static u32 done_after, o_nw, o_pr;
static struct { u32 reg, val; } ow[8];

static void ev(struct side *s, u32 id, u32 a, u32 nw, u32 pr)
{
	if (s->nev < 8) {
		s->ev[s->nev][0] = id;
		s->ev[s->nev][1] = a;
		s->ev[s->nev][2] = nw;
		s->ev[s->nev][3] = pr;
	}
	s->nev++;
}
static u32 rd(struct side *s, u32 reg, u32 nw, u32 pr)
{
	s->nrd++;
	s->hash = (s->hash ^ reg ^ (nw << 20) ^ (pr << 26)) * 0x01000193u;
	return s->nrd == done_after ? 0 : rnd() | 1;
}

static u32 v_rd(u32 reg) { return rd(&vs, reg, t32o_log.n, t32o_log.printfs); }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; ev(&vs, 1, a0 << 16 | a3, t32o_log.n, t32o_log.printfs); return 0; }
u64 tisp_ldc_reg_reflash(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(&vs, 2, a0, t32o_log.n, t32o_log.printfs); return rnd(); }
u64 tisp_rcfg_cfg_trig(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(&vs, 3, a0, t32o_log.n, t32o_log.printfs); return rnd(); }
u64 t32o7_single_lsc_rcfg_cfg_trig(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(&vs, 4, a0, t32o_log.n, t32o_log.printfs); return rnd(); }

static u32 o_rd(void *c, u32 reg) { (void)c; return rd(&os, reg, o_nw, o_pr); }
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
static u32 o_trace(void *c, u32 line) { (void)c; (void)line; o_pr++; return 0; }
static void o_log(void *c, u32 level, u32 line) { (void)c; ev(&os, 1, level << 16 | line, o_nw, o_pr); }
static u32 o_reflash(void *c, u32 a) { (void)c; ev(&os, 2, a, o_nw, o_pr); return 0; }
static u32 o_trig(void *c, u32 a) { (void)c; ev(&os, 3, a, o_nw, o_pr); return 0; }
static u32 o_strig(void *c, u32 a) { (void)c; ev(&os, 4, a, o_nw, o_pr); return 0; }

int main(void)
{
	static const u32 b[] = { 1, 2, 19999, 20000, 20001 };
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) / 20 : 1000;
	struct t32_stream_hooks h = { NULL, o_rd, o_wr, o_trace, o_log, o_reflash, o_trig, o_strig };
	static u8 conf[4], st[1808];

	t32o_rd_hook = v_rd;
	for (c = 0; c < n; c++) {
		u32 r, o, bad, seed, k = rnd() % 4, sn;

		t32o_reset();
		memset(&vs, 0, sizeof(vs));
		memset(&os, 0, sizeof(os));
		o_nw = o_pr = 0;
		t32_data_dd674[0] = conf[0] = c % 7 == 0 ? 0 : c % 7 == 1 ? (u8)rnd() : 1;
		sn = k < 2 ? 1 : k == 2 ? 2 : rnd();
		memcpy(t32_data_dc284 + 900, &sn, 4);
		memcpy(st + 900, &sn, 4);
		k = rnd() % 7;
		done_after = k < 5 ? b[k] : 1 + rnd() % 20000;
		seed = rnd() | 1;
		rs = seed;
		r = (u32)t32o_call(F(lib_tisp_stream_on), seed, 0, 0, 0, 0);
		rs = seed;
		o = t32_lib_stream_on(&h, conf, st);
		bad = r != o || memcmp(&vs, &os, sizeof(vs)) || t32o_log.n != o_nw || t32o_log.printfs != o_pr;
		for (i = 0; !bad && i < o_nw && i < 8; i++)
			bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u conf %u sn %x done %u: ret %x/%x reads %u/%u ev %u/%u writes %u/%u\n", c,
			       conf[0], sn, done_after, r, o, vs.nrd, os.nrd, vs.nev, os.nev, t32o_log.n, o_nw);
		rs = (seed * 0x2545f491u) | 1;
	}
	printf("t32_libstream_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
