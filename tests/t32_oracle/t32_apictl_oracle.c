// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our module enable and raw setters
 * (driver/t32/tx_isp_t32_api_ctl.h) against the vendor tisp_s_module_control,
 * tisp_s_raw_rw_control and tisp_s_raw_row_control from Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * tisp_reg_read/_write and the MDNS/LCE callees are recording stubs.
 * Compared: the calls (order, arguments) with d03a0 = 0, 1, 2, the number of
 * error prints, the return value and every byte of the user block and the
 * channel state t32_data_dc284.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_ctl.h"

static u32 rs = 0x4d21c5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define NREC 8
static struct { u32 fn, a[3]; } lg[NREC];
static unsigned int nlg;
static u32 stub_ret, top_word;

static u64 rec(u32 fn, u32 a0, u32 a1, u32 a2)
{
	if (nlg < NREC) {
		lg[nlg].fn = fn; lg[nlg].a[0] = a0; lg[nlg].a[1] = a1; lg[nlg].a[2] = a2;
	}
	nlg++;
	return stub_ret + fn * 0x10001 + nlg;
}
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rec(1, a0, a1, 0); return top_word; }
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(2, a0, a1, a2); }
u64 tisp_mdns_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return rec(3, a0, 0, 0); }
u64 tisp_lce_top_change_state(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return rec(4, a0, 0, 0); }
u64 tisp_mdns_reg_trig(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return rec(5, a0, 0, 0); }

static u32 o_rd(u32 c, u32 r) { return (u32)tisp_reg_read(c, r, 0, 0); }
static int o_wr(u32 c, u32 r, u32 v) { return (int)tisp_reg_write(c, r, v, 0); }
static void o_warn(void) { t32o_log.printfs++; }
static int o_ms(u32 c) { return (int)tisp_mdns_start(c, 0, 0, 0); }
static int o_lce(u32 c) { return (int)tisp_lce_top_change_state(c, 0, 0, 0); }
static int o_trig(u32 c) { return (int)tisp_mdns_reg_trig(c, 0, 0, 0); }
static const struct t32_api_ctl_ops ops = {
	.reg_read = o_rd, .reg_write = o_wr, .warn = o_warn, .mdns_start = o_ms,
	.lce_top_change_state = o_lce, .mdns_reg_trig = o_trig,
};

u64 tisp_s_module_control(u32, u32, u32, u32);
u64 tisp_s_raw_rw_control(u32, u32, u32, u32);
u64 tisp_s_raw_row_control(u32, u32, u32, u32);

#define BUFB 64
static unsigned int failed, compared;

static void run(unsigned int k, u32 a0, u32 a1, u8 mode)
{
	static u8 st0[1808], vst[1808], b0[BUFB], vb[BUFB];
	unsigned int i, vn, vp;
	u32 vr, vl[NREC][4], w;
	u8 *buf;
	int or_;

	t32o_reset();
	buf = t32o_alloc(BUFB);
	stub_ret = rnd();
	top_word = (rnd() & 1) ? rnd() : a1 ^ (rnd() & (T32_TOP_EN_MDNS | T32_TOP_EN_LCE | 0x10));
	for (i = 0; i < 1808; i++) st0[i] = (u8)rnd();
	for (i = 0; i < BUFB; i++) b0[i] = (u8)rnd();
	for (i = 8; i <= 32; i += 8) {	/* the checked words: 0, 1, 2, other */
		w = (rnd() & 1) ? rnd() % 3 : rnd();
		memcpy(b0 + i, &w, 4);
	}
	t32_data_d03a0[0] = mode;
	memcpy(t32_data_dc284, st0, 1808); memcpy(buf, b0, BUFB);
	nlg = 0;
	vr = (u32)t32o_call((t32o_fn)(void (*)(void))(k == 0 ? tisp_s_module_control : k == 1 ? tisp_s_raw_rw_control :
						      tisp_s_raw_row_control), a0, k ? T32O_PTR(buf) : a1, 0, 0, 0);
	vn = nlg; vp = t32o_log.printfs;
	for (i = 0; i < NREC; i++)
		memcpy(vl[i], &lg[i], sizeof(vl[i]));
	memcpy(vst, t32_data_dc284, 1808); memcpy(vb, buf, BUFB);

	memcpy(t32_data_dc284, st0, 1808); memcpy(buf, b0, BUFB);
	nlg = 0; t32o_log.printfs = 0;
	if (k == 0)
		or_ = t32_s_module_control(&ops, mode, a0, a1);
	else if (k == 1)
		or_ = t32_s_raw_rw_control(&ops, a0, buf);
	else
		or_ = t32_s_raw_row_control(&ops, a0, buf);
	compared++;
	if (vn != nlg || vp != t32o_log.printfs || vr != (u32)or_ || memcmp(vst, t32_data_dc284, 1808) ||
	    memcmp(vb, buf, BUFB))
		goto bad;
	for (i = 0; i < vn && i < NREC; i++)
		if (memcmp(vl[i], &lg[i], sizeof(vl[i])))
			goto bad;
	return;
bad:
	if (failed++ < 8)
		printf("MISMATCH k=%u a0=%08x a1=%08x d03a0=%u: calls %u/%u prints %u/%u ret %08x/%08x\n", k, a0, a1,
		       mode, vn, nlg, vp, t32o_log.printfs, vr, (u32)or_);
}

int main(void)
{
	unsigned int c, k, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 10000;

	for (k = 0; k < 3; k++)
		for (c = 0; c < n; c++)
			run(k, k ? rnd() & 1 : rnd(), rnd(), (u8)(c % 3));	/* raw: dc284 + ch * 904 (2 channels) */
	printf("t32_apictl_oracle: 3 entry points, %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
