// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our scaler level setter (driver/t32/tx_isp_t32_api_scl.h)
 * against the vendor tisp_set_scaler_level_control_set from Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * tisp_reg_write and system_reg_read (t32o_rd_hook) are recorded.  Compared:
 * register reads and writes (order, channel, address, value) with d03a0 = 0,
 * 1, 2, error prints, the return value, every byte of the MSCA blocks of all
 * channels, the e2898 marks and the user block.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_scl.h"

static u32 rs = 0x15ca1eu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define NREC 8
static struct { u32 fn, a[3]; } lg[NREC];
static unsigned int nlg;
static u32 rd_val;

static void rec(u32 fn, u32 a0, u32 a1, u32 a2)
{
	if (nlg < NREC) {
		lg[nlg].fn = fn; lg[nlg].a[0] = a0; lg[nlg].a[1] = a1; lg[nlg].a[2] = a2;
	}
	nlg++;
}
static u32 rd_hook(u32 reg) { rec(1, reg, 0, 0); return rd_val; }
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; rec(2, a0, a1, a2); return 0; }

static u32 o_rd(u32 r) { return rd_hook(r); }
static void o_wr(u32 c, u32 r, u32 v) { tisp_reg_write(c, r, v, 0); }
static void o_warn(void) { t32o_log.printfs++; }
static const struct t32_api_scl_ops ops = { .sys_reg_read = o_rd, .reg_write = o_wr, .warn = o_warn };

u64 tisp_set_scaler_level_control_set(u32, u32, u32, u32);

#define BLKB 256
static unsigned int failed, compared;

static void run(u32 ch, u8 mode, unsigned int c)
{
	static u8 bl0[4][BLKB], vbl[4][BLKB], obl[4][BLKB], b0[16], vb[16], vm[16];
	u8 *blk[4], *ptr[4], *buf;
	unsigned int i, j, vn, vp;
	u32 vr, vl[NREC][4], w;
	int or_;

	t32o_reset();
	for (j = 0; j < 4; j++) {
		blk[j] = t32o_alloc(BLKB);
		ptr[j] = t32o_alloc(4);
		w = T32O_PTR(blk[j]);
		memcpy(ptr[j], &w, 4);
		t32_data_e28ac[j] = T32O_PTR(ptr[j]);
		for (i = 0; i < BLKB; i++)
			bl0[j][i] = (u8)rnd();
	}
	buf = t32o_alloc(16);
	for (i = 0; i < 16; i++)
		b0[i] = (u8)rnd();
	b0[0] = (c & 1) ? (u8)(rnd() % 4) : (u8)rnd();
	w = (c % 3) ? rnd() % 3 : rnd();
	memcpy(b0 + 4, &w, 4);
	b0[8] = (c & 2) ? (u8)(126 + rnd() % 5) : (u8)rnd();
	rd_val = rnd();
	t32o_rd_hook = rd_hook;
	t32_data_d03a0[0] = mode;
	for (i = 0; i < 16; i++)
		t32_data_e2898[i] = (u8)(0x40 + i);

	for (j = 0; j < 4; j++) memcpy(blk[j], bl0[j], BLKB);
	memcpy(buf, b0, 16);
	nlg = 0;
	vr = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_set_scaler_level_control_set, ch, T32O_PTR(buf), 0, 0, 0);
	vn = nlg; vp = t32o_log.printfs;
	for (i = 0; i < NREC; i++)
		memcpy(vl[i], &lg[i], sizeof(vl[i]));
	for (j = 0; j < 4; j++) memcpy(vbl[j], blk[j], BLKB);
	memcpy(vb, buf, 16); memcpy(vm, t32_data_e2898, 16);

	for (j = 0; j < 4; j++) memcpy(blk[j], bl0[j], BLKB);
	memcpy(buf, b0, 16);
	for (i = 0; i < 16; i++)
		t32_data_e2898[i] = (u8)(0x40 + i);
	nlg = 0; t32o_log.printfs = 0;
	or_ = t32_set_scaler_level_control_set(&ops, mode, blk[ch], &t32_data_e2898[ch], ch, buf);
	for (j = 0; j < 4; j++) memcpy(obl[j], blk[j], BLKB);
	compared++;
	t32o_rd_hook = 0;
	if (vn != nlg || vp != t32o_log.printfs || vr != (u32)or_ || memcmp(vbl, obl, sizeof(vbl)) ||
	    memcmp(vb, buf, 16) || memcmp(vm, t32_data_e2898, 16))
		goto bad;
	for (i = 0; i < vn && i < NREC; i++)
		if (memcmp(vl[i], &lg[i], sizeof(vl[i])))
			goto bad;
	return;
bad:
	if (failed++ < 8)
		printf("MISMATCH ch=%u d03a0=%u set=%u: calls %u/%u prints %u/%u ret %08x/%08x\n", ch, mode, b0[0], vn, nlg,
		       vp, t32o_log.printfs, vr, (u32)or_);
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;

	for (c = 0; c < n; c++)
		run(rnd() & 3, (u8)(c / 3 % 3), c);
	printf("t32_apiscl_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
