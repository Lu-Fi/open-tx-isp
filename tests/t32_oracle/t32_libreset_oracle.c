// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our lib_tisp_all_reset (driver/t32/tx_isp_t32_libreset.h) against Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value, every register read (address, order against the writes), write (address,
 * value, order) and log call (level, line).  Inputs: reset acknowledge / core idle after 1, random, 9999,
 * 10000 (last poll) and 10001 (timeout) reads, random non-ready values.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libreset.h"

u64 lib_tisp_all_reset(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

char t32_data_bf2bc[4] = "f", t32_data_c7d5c[4] = "f", t32_data_c7d84[4] = "f", t32_data_c7d38[4] = "f";

static u32 rs = 0xa4093822u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* read history as a hash (20000 reads possible): reg, number of writes before it */
struct side { u32 nrd, hash, nlog, logs[4][2], ack_n, core_n; };
static struct side vs, os;
static u32 ack_after, core_after, log_ret, o_nw;
static struct { u32 reg, val; } ow[8];

static u32 rdval(struct side *s, u32 reg, u32 nw)
{
	s->nrd++;
	s->hash = (s->hash ^ reg ^ (nw << 24)) * 0x01000193u;
	if (reg == T32_RST_ACK) {
		s->ack_n++;
		return s->ack_n == ack_after ? 1 : (rnd() | 2);
	}
	if (reg == T32_RST_CORE) {
		s->core_n++;
		return s->core_n == core_after ? rnd() & ~0xfu : (rnd() & ~0xfu) | (1 + rnd() % 15);
	}
	return rnd();
}
static void addlog(struct side *s, u32 level, u32 line)
{
	if (s->nlog < 4) {
		s->logs[s->nlog][0] = level;
		s->logs[s->nlog][1] = line;
	}
	s->nlog++;
}

static u32 v_rd(u32 reg) { return rdval(&vs, reg, t32o_log.n); }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; addlog(&vs, a0, a3); return log_ret + vs.nlog; }

static u32 o_rd(void *c, u32 reg) { (void)c; return rdval(&os, reg, o_nw); }
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
static u32 o_log(void *c, u32 level, u32 line) { (void)c; addlog(&os, level, line); return log_ret + os.nlog; }

static u32 pick(void)
{
	static const u32 b[] = { 1, 2, 9999, 10000, 10001, 0 };
	u32 k = rnd() % 8;

	return k < 6 ? b[k] : 1 + rnd() % 10000;
}

int main(void)
{
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) / 20 : 1000;
	struct t32_reset_hooks h = { NULL, o_rd, o_wr, o_log };

	t32o_rd_hook = v_rd;
	for (c = 0; c < n; c++) {
		u32 r, o, bad, seed;

		t32o_reset();
		memset(&vs, 0, sizeof(vs));
		memset(&os, 0, sizeof(os));
		o_nw = 0;
		ack_after = pick();
		core_after = pick();
		log_ret = rnd();
		seed = rnd();
		rs = seed;
		r = (u32)t32o_call(F(lib_tisp_all_reset), seed, 0, 0, 0, 0);
		rs = seed;
		o = t32_lib_all_reset(&h);
		bad = r != o || memcmp(&vs, &os, sizeof(vs)) || t32o_log.n != o_nw;
		for (i = 0; !bad && i < o_nw && i < 8; i++)
			bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u ack %u core %u: ret %x/%x reads %u/%u logs %u/%u writes %u/%u\n", c,
			       ack_after, core_after, r, o, vs.nrd, os.nrd, vs.nlog, os.nlog, t32o_log.n, o_nw);
		rs = (seed * 0x2545f491u) | 1;
	}
	printf("t32_libreset_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
