// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_freset.h (tisp_func_stop_reset,
 * tisp_func_reset) against the vendor functions (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc).  Per case the stop poll (0x204) and the reset poll
 * (0x24) end after a scripted number of reads (0, 1, small, 9998 .. 10001,
 * random), the other values random (0x204 never 1 before the end, 0x24
 * never 0 in the low nibble before the end).  Compared: return value, every
 * register write (address, value, order), number and hash of the register
 * reads (address, value, write count at the read), every printf (level,
 * format text, function name text, line).  The vendor functions call
 * system_reg_write directly, so t32_data_d03a0 does not matter (not varied).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_freset.h"

#define WK __attribute__((weak))
WK char t32_data_bf2a4[] = "tisp_func_stop_reset";				/* data.c:2252 */
WK char t32_data_bf294[] = "tisp_func_reset";					/* data.c:2248 */
WK char t32_data_c7d5c[] = "[ISP][%s, %d]: isp stop timeout !!!\n";		/* data.c:11132 */
WK char t32_data_c7dd8[] = "[ISP][%s, %d]: isp function reset timeout !!!\n";	/* data.c:11163 */
WK char t32_data_c7db0[] = "[ISP][%s, %d]: isp function reset !!!\n";		/* data.c:11153 */
extern u64 system_reg_write(u32, u32, u32, u32), system_reg_read(u32, u32, u32, u32);
extern u64 tisp_func_stop_reset(u32, u32, u32, u32), tisp_func_reset(u32, u32, u32, u32);

static u32 rs = 0x3c6ef372u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct side {
	u32 nread, hash, nprint;
	u32 pr[4][4];
	char txt[4][2][64];
};
static struct side vs, os, *cur;
static u32 stop_after, rst_after, nstop, nrst, print_ret;

static u32 rd_script(u32 reg)
{
	u32 v = rnd();

	if (reg == T32_FRESET_STOP_DONE) {
		v = ++nstop > stop_after ? 1 : (v == 1 ? 3 : v);
	} else if (reg == T32_FRESET_CTRL) {
		if (++nrst > rst_after)
			v &= ~0xfu;
		else if (!(v & 0xf))
			v |= 1u << (v >> 30);
	}
	cur->nread++;
	cur->hash = (cur->hash * 0x01000193u) ^ reg ^ (v * 31u) ^ (t32o_log.n << 20);
	return v;
}

static void prec(u32 level, const char *fmt, const char *fn, u32 line)
{
	u32 i = cur->nprint++;

	if (i < 4) {
		cur->pr[i][0] = level;
		cur->pr[i][1] = line;
		strncpy(cur->txt[i][0], fmt, 63);
		strncpy(cur->txt[i][1], fn, 63);
	}
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	prec(a0, T32O_HOST(a1), T32O_HOST(a2), a3);
	return print_ret;
}

static u32 o_write(u32 reg, u32 val) { return (u32)system_reg_write(reg, val, 0, 0); }
static u32 o_read(u32 reg) { return (u32)system_reg_read(reg, 0, 0, 0); }
static u32 o_printf(u32 level, const char *fmt, const char *fn, u32 line)
{
	prec(level, fmt, fn, line);
	return print_ret;
}
static const struct t32_freset_ops ops = { o_write, o_read, o_printf };

static u32 pick(void)
{
	static const u32 b[] = { 0, 1, 2, 5, 9998, 9999, 10000, 10001 };
	u32 r = rnd() % 12;

	return r < 8 ? b[r] : rnd() % 10050;
}

static struct { u32 reg, val; } ow[16];

int main(void)
{
	unsigned long n, N = 6000, per[2] = { 0 }, stop_to = 0, rst_to = 0;

	for (n = 0; n < N; n++) {
		int fn = (int)(rnd() & 1);
		u32 rv, ro, s, wn, i, a[4] = { rnd(), rnd(), rnd(), rnd() };

		stop_after = pick();
		rst_after = pick();
		print_ret = rnd();
		s = rs;

		t32o_reset();
		t32o_rd_hook = rd_script;
		memset(&os, 0, sizeof(os)); cur = &os; nstop = nrst = 0;
		ro = fn ? t32_func_reset(&ops) : t32_func_stop_reset(&ops);
		wn = t32o_log.n;
		for (i = 0; i < wn && i < 16; i++) { ow[i].reg = t32o_log.w[i].reg; ow[i].val = t32o_log.w[i].val; }

		t32o_reset();
		t32o_rd_hook = rd_script;
		memset(&vs, 0, sizeof(vs)); cur = &vs; nstop = nrst = 0; rs = s;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))(fn ? tisp_func_reset : tisp_func_stop_reset),
				    a[0], a[1], a[2], a[3], 0);
		t32o_rd_hook = NULL;

		if (rv != ro || wn != t32o_log.n || vs.nread != os.nread || vs.hash != os.hash ||
		    vs.nprint != os.nprint || memcmp(vs.pr, os.pr, sizeof(vs.pr)) || memcmp(vs.txt, os.txt, sizeof(vs.txt))) {
			printf("t32_freset_oracle: FAIL case %lu fn %d stop %u rst %u: ret %#x/%#x writes %u/%u reads %u/%u hash %#x/%#x prints %u/%u\n",
			       n, fn, stop_after, rst_after, rv, ro, t32o_log.n, wn, vs.nread, os.nread,
			       vs.hash, os.hash, vs.nprint, os.nprint);
			for (i = 0; i < 4; i++)
				printf("  print %u: %u %u %s|%s / %u %u %s|%s\n", i, vs.pr[i][0], vs.pr[i][1], vs.txt[i][1],
				       vs.txt[i][0], os.pr[i][0], os.pr[i][1], os.txt[i][1], os.txt[i][0]);
			return 1;
		}
		for (i = 0; i < wn && i < 16; i++)
			if (ow[i].reg != t32o_log.w[i].reg || ow[i].val != t32o_log.w[i].val) {
				printf("t32_freset_oracle: FAIL case %lu write %u vendor %#x=%#x ours %#x=%#x\n", n, i,
				       t32o_log.w[i].reg, t32o_log.w[i].val, ow[i].reg, ow[i].val);
				return 1;
			}
		if (!fn && stop_after >= T32_FRESET_POLLS)
			stop_to++;
		else if (rst_after >= T32_FRESET_POLLS)
			rst_to++;
		per[fn]++;
	}
	printf("t32_freset_oracle: PASS %lu cases (stop_reset %lu, func_reset %lu, stop timeouts %lu, reset timeouts %lu)\n",
	       N, per[0], per[1], stop_to, rst_to);
	return 0;
}
