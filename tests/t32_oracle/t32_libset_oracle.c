// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our lib_tisp_set_csccr and lib_tisp_set_wdr_setting (driver/t32/tx_isp_t32_libset.h) against
 * Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value, every register read / system write / tisp_reg_write / log call in order
 * (addresses, values, channel, log level and line), the channel state blocks (both channels), the per-channel
 * words f32d8 / f32e8 / f32f8 and bytes e4018 / dc060.  Inputs: channel 0 / 1 (random upper bits for csccr),
 * running byte 0 / 1 / random, mode 0 / 1 / random, d03a0 = 0 and 1, random register values.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libset.h"

u64 lib_tisp_set_csccr(u32, u32, u32, u32);
u64 lib_tisp_set_wdr_setting(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

u8 t32_data_dc060[4], t32_data_e4018[4];
u32 t32_data_f32d8[4], t32_data_f32e8[4], t32_data_f32f8[4];
char t32_data_c2fd0[4] = "f", t32_data_cb748[4] = "f";

static u32 rs = 0x13198a2eu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { E_RD = 1, E_WR, E_RW, E_LOG };
struct ev { u32 id, a[3]; };
#define NE 16
static struct ev ve[NE], oe[NE];
static unsigned int vn, on;
static u32 rdseed, rdn_v, rdn_o;

static void vadd(u32 id, u32 a0, u32 a1, u32 a2) { if (vn < NE) ve[vn] = (struct ev){ id, { a0, a1, a2 } }; vn++; }
static void oadd(u32 id, u32 a0, u32 a1, u32 a2) { if (on < NE) oe[on] = (struct ev){ id, { a0, a1, a2 } }; on++; }
static u32 rdval(u32 reg, u32 k) { return (reg * 0x9e3779b1u + k * 0x85ebca6bu) ^ rdseed; }

/* system_reg_write is env.c's (t32o_log): the read hook records the write count so the order is compared */
static u32 v_rd(u32 reg) { vadd(E_RD, reg, t32o_log.n, 0); return rdval(reg, rdn_v++); }
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; vadd(E_RW, a0, a1, a2); return 0; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; vadd(E_LOG, a0, a3, 0); return 0; }

static u32 o_nw;
static struct { u32 reg, val; } ow[8];
static u32 o_rd(void *c, u32 reg) { (void)c; oadd(E_RD, reg, o_nw, 0); return rdval(reg, rdn_o++); }
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
static u32 o_rw(void *c, u32 ch, u32 reg, u32 val) { (void)c; oadd(E_RW, ch, reg, val); return 0; }
static void o_log(void *c, u32 level, u32 line, u32 val) { (void)c; (void)val; oadd(E_LOG, level, line, 0); }

int main(void)
{
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_set_hooks h = { NULL, o_rd, o_wr, o_rw, o_log };
	static u8 st[1808], run[4], start[4], sh[4];
	static u32 fr[4], m1[4], m2[4];
	struct t32_set_data d = { run, st, start, fr, m1, m2, sh };

	t32o_rd_hook = v_rd;
	for (c = 0; c < n; c++) {
		u32 r, o, bad, ch = rnd() & 1, mode, k;
		u8 *attr;

		t32o_reset();
		vn = on = o_nw = rdn_v = rdn_o = 0;
		memset(ve, 0, sizeof(ve));
		memset(oe, 0, sizeof(oe));
		rdseed = rnd();
		for (i = 0; i < sizeof(st); i++)
			t32_data_dc284[i] = st[i] = (u8)rnd();
		for (i = 0; i < 4; i++) {
			k = rnd() % 4;
			t32_data_dc060[i] = run[i] = k < 2 ? 1 : k == 2 ? 0 : (u8)rnd();
			t32_data_e4018[i] = start[i] = (u8)rnd();
			t32_data_f32d8[i] = fr[i] = rnd() % 5 == 0 ? 0xffffff + (rnd() & 1) : rnd();
			t32_data_f32e8[i] = m2[i] = rnd();
			t32_data_f32f8[i] = m1[i] = rnd();
		}
		t32_data_d03a0[0] = sh[0] = (u8)(c / 2 % 2);
		k = rnd() % 4;
		mode = k == 0 ? 0 : k == 1 ? 1 : k == 2 ? (rnd() & ~0xffu) | (rnd() % 3) : rnd();
		if (c % 2) {
			u32 vch = ch | (rnd() & ~0xffu);

			r = (u32)t32o_call(F(lib_tisp_set_csccr), vch, mode, rnd(), rnd(), 0);
			o = t32_lib_set_csccr(&h, &d, vch, mode);
		} else {
			attr = t32o_alloc(32);
			for (i = 0; i < 32; i++)
				attr[i] = (u8)rnd();
			attr[17] = (u8)ch;
			memcpy(attr + 8, &mode, 4);
			if (rnd() % 3 == 0)
				memset(attr + 8, 0, 4), attr[8] = 1;
			r = (u32)t32o_call(F(lib_tisp_set_wdr_setting), T32O_PTR(attr), rnd(), rnd(), rnd(), 0);
			o = t32_lib_set_wdr_setting(&h, &d, attr);
		}
		bad = r != o || vn != on || memcmp(ve, oe, sizeof(ve)) || t32o_log.n != o_nw ||
		      memcmp(t32_data_dc284, st, sizeof(st)) || memcmp(t32_data_dc060, run, 4) ||
		      memcmp(t32_data_e4018, start, 4) || memcmp(t32_data_f32d8, fr, 16) ||
		      memcmp(t32_data_f32e8, m2, 16) || memcmp(t32_data_f32f8, m1, 16) || vn > NE;
		for (i = 0; !bad && i < o_nw && i < 8; i++)
			bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH %s case %u ch %u mode %x sh %u: ret %x/%x calls %u/%u writes %u/%u\n",
			       c % 2 ? "csccr" : "wdr", c, ch, mode, sh[0], r, o, vn, on, t32o_log.n, o_nw);
	}
	printf("t32_libset_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
