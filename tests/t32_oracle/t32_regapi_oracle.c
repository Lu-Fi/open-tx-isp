// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our tisp_reg_lut_read, line-space API and WDR-enable read (driver/t32/tx_isp_t32_bayer.h)
 * against the vendor tisp_reg_lut_read, lib_tisp_api_get_line_space / _set_line_space and
 * lib_tisp_get_wdr_enable from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value (low word), every register write (address, value, order), the stored
 * line space and the canary bytes around it, the shadow images and the channel state (unchanged).  Inputs:
 * random register contents, shadow images, channel state; mode byte 0 / 1 / 2; channels 0..3 (LUT read) and
 * 0..1 with high garbage bytes (WDR; the vendor does not range-check, ch >= 2 reads past t32_data_dc284).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_bayer.h"

u64 tisp_reg_lut_read(u32, u32, u32, u32);
u64 lib_tisp_api_get_line_space(u32, u32, u32, u32);
u64 lib_tisp_api_set_line_space(u32, u32, u32, u32);
u64 lib_tisp_get_wdr_enable(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define WORDS	128

static u32 rs = 0x51ce77u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 regs[0x100], *img[4], mine[4][WORDS];
static u8 state[sizeof(t32_data_dc284)];

static u32 rd(u32 reg)
{
	return regs[reg & 0xff];
}

/* our hooks */
static struct { u32 reg, val; } ow[8];
static unsigned int on;
static u32 h_read(void *ctx, u32 reg) { (void)ctx; return rd(reg); }
static u32 *h_shadow(void *ctx, u32 ch) { (void)ctx; return mine[ch]; }
static u32 h_swrite(void *ctx, u32 reg, u32 val)
{
	(void)ctx;
	ow[on].reg = reg;
	ow[on++].val = val;
	return 0;
}

static unsigned int compared, failed;

static void arm(unsigned char mode)
{
	u8 *blk;
	unsigned int ch, i;
	u32 p;

	t32o_reset();
	blk = t32o_alloc(4 * 256 + 256);
	for (ch = 0; ch < 4; ch++) {
		img[ch] = t32o_alloc(WORDS * 4);
		for (i = 0; i < WORDS; i++)
			img[ch][i] = mine[ch][i] = rnd();
		p = T32O_PTR(img[ch]);
		memcpy(blk + (ch << 8) + 200, &p, 4);
	}
	t32_data_da844[0] = T32O_PTR(blk);
	t32_data_d03a0[0] = mode;
	for (i = 0; i < sizeof(state); i++)
		t32_data_dc284[i] = state[i] = (u8)rnd();
	on = 0;
}

static void cmp(const char *what, u32 vr, u32 or_, u32 a0, u32 a1, const u8 *vb, const u8 *ob, unsigned int nb)
{
	unsigned int i, bad = vr != or_ || t32o_log.n != on;

	for (i = 0; !bad && i < on; i++)
		bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
	for (i = 0; !bad && i < 4; i++)
		bad = memcmp(img[i], mine[i], WORDS * 4) != 0;
	if (!bad)
		bad = memcmp(t32_data_dc284, state, sizeof(state)) != 0 || (nb && memcmp(vb, ob, nb) != 0);
	compared++;
	if (bad && failed++ < 10)
		printf("MISMATCH %s(%x, %x) mode %u: ret %x/%x writes %u/%u\n", what, a0, a1,
		       t32_data_d03a0[0], vr, or_, t32o_log.n, on);
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	unsigned char mode;
	struct t32_regio io = { &mode, 0, h_read, h_shadow, 0, 0, h_swrite };

	t32o_rd_hook = rd;
	for (c = 0; c < n; c++) {
		u32 ch = rnd() % 4, reg = rnd() % (WORDS / 2), idx = rnd() % (WORDS / 2), sp = rnd(), wch, r;
		u8 *vb, ob[8];

		if (c % 7 == 0)
			sp = c % 2 ? 0xffffu : 0x10000u;
		wch = (rnd() & 1) | (c % 3 ? rnd() & 0xffffff00u : 0);
		mode = c % 4 == 3 ? 2 : c % 2;
		io.mode = mode;
		for (i = 0; i < 0x100; i++)
			regs[i] = rnd();
		if (c % 5 == 0)
			regs[0x90] = c % 2 ? 0xffffffffu : 0;

		arm(mode);
		r = (u32)t32o_call(F(tisp_reg_lut_read), ch, reg, idx, 0, 0);
		cmp("tisp_reg_lut_read", r, t32_tisp_reg_lut_read(&io, ch, reg, idx), ch, reg, 0, 0, 0);

		arm(mode);
		vb = t32o_alloc(8);
		for (i = 0; i < 8; i++)
			vb[i] = ob[i] = (u8)rnd();
		r = (u32)t32o_call(F(lib_tisp_api_get_line_space), T32O_PTR(vb + 2), 0, 0, 0, 0);
		cmp("get_line_space", r, t32_api_get_line_space(&io, (unsigned short *)(ob + 2)), 0, 0, vb, ob, 8);

		arm(mode);
		r = (u32)t32o_call(F(lib_tisp_api_set_line_space), sp, 0, 0, 0, 0);
		cmp("set_line_space", r, t32_api_set_line_space(&io, sp), sp, 0, 0, 0, 0);

		arm(mode);
		r = (u32)t32o_call(F(lib_tisp_get_wdr_enable), wch, 0, 0, 0, 0);
		cmp("get_wdr_enable", r, t32_get_wdr_enable(state, wch), wch, 0, 0, 0, 0);
	}
	t32_data_d03a0[0] = 0;
	printf("t32_regapi_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
