// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_reset of driver/t32/tx_isp_t32_treset.h against
 * Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc, built through
 * fix_op8_regio.sh).  Compared: every register read and write (address,
 * value) and driver call (MDNS per pipeline, udelay, isp_printf) in one
 * sequence.  Inputs: d03a0 0 / 1 / 2, pipeline counts 0..4 and big, the
 * stop / reset bits clearing after 0..10001 reads (timeouts included).
 * Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_treset.h"

u64 tisp_reset(u32, u32, u32, u32);

enum { F_RD = 1, F_WR, F_CLC, F_START, F_TRIG, F_UDELAY, F_PRINTF };
char t32_data_bf278[] = "tisp_reset";	/* data.c strings */
char t32_data_c7d5c[] = "[ISP][%s, %d]: isp stop timeout !!!\n";
char t32_data_c7ed4[] = "[ISP][%s, %d]: isp reset timeout !!!\n";
char t32_data_c7efc[] = "[ISP][%s, %d]: isp reset timeout after udelay !!!\n";

static struct t32o8_log *cur;
static u32 n204, n24, k204, k24;
static u32 rd(u32 reg)
{
	t32o8_rec(cur, F_RD, reg, 0, 0, 0);
	if (reg == 0x204)
		return ++n204 > k204 ? 1 : 0x10 | (n204 & 7);
	if (reg == 0x24)
		return ++n24 > k24 ? 0x40000000u : 0x40000000u | (1 + n24 % 15);
	return 0xdead0000u | reg;
}
u64 t32o8_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return rd(a0); }
u64 t32o8_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; t32o8_rec(cur, F_WR, a0, a1, 0, 0); return 0; }
u64 tisp_mdns_buf_clc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_CLC, a0, 0, 0, 0); return 0; }
u64 tisp_mdns_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_START, a0, 0, 0, 0); return 0; }
u64 tisp_mdns_reg_trig(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_TRIG, a0, 0, 0, 0); return 0; }
u64 __udelay(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_UDELAY, a0, 0, 0, 0); return 0; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	u32 w[5] = { a2, a3, a4, a5, a6 };

	t32o8_rec_fmt(cur, F_PRINTF, a0, T32O_HOST(a1), w, 5);
	return 0;
}

static void o_wr(u32 r, u32 v) { t32o8_rec(cur, F_WR, r, v, 0, 0); }
static void o_clc(u32 p) { t32o8_rec(cur, F_CLC, p, 0, 0, 0); }
static void o_start(u32 p) { t32o8_rec(cur, F_START, p, 0, 0, 0); }
static void o_trig(u32 p) { t32o8_rec(cur, F_TRIG, p, 0, 0, 0); }
static void o_udelay(u32 us) { t32o8_rec(cur, F_UDELAY, us, 0, 0, 0); }
static int o_printf(int level, const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(cur, F_PRINTF, (u32)level, fmt, w, n);
	return 0;
}
static const struct t32_treset_ops ops = { rd, o_wr, o_clc, o_start, o_trig, o_udelay, o_printf };

static u32 rs = 0x7e5e7e5eu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 polls(void)
{
	switch (rnd() % 5) {
	case 0: return 0;
	case 1: return rnd() % 5;
	case 2: return 9998 + rnd() % 4;
	case 3: return 19998 + rnd() % 4;
	default: return rnd() % 30000;
	}
}

int main(void)
{
	unsigned int c, failed = 0;
	for (c = 0; c < 600; c++) {
		u32 mode = rnd() % 3, pipes = (rnd() & 3) ? rnd() % 5 : 300;	/* >= 0x10000 never ends (16-bit counter, vendor) */
		u32 *pc = (u32 *)(t32_data_dc284 + 900);

		t32o_reset();
		*pc = pipes;
		k204 = polls(); k24 = polls();
		t32o8_clear();
		t32_data_d03a0[0] = (u8)mode;
		cur = &t32o8_v; n204 = n24 = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_reset, 0, 0, 0, 0, 0);
		cur = &t32o8_o; n204 = n24 = 0;
		t32_tisp_reset(&ops, mode == 1, *pc);
		if ((t32o8_cmp("treset", failed < 8) || t32o8_v.n != t32o8_o.n) && failed++ < 8)
			printf("MISMATCH d03a0 %u pipes %u k204 %u k24 %u\n", mode, pipes, k204, k24);
	}
	t32_data_d03a0[0] = 0;
	printf("t32_treset_oracle: 600 compared, %u mismatches\n", failed);
	return failed ? 1 : 0;
}
