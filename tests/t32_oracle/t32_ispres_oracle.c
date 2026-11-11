// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_resume_module (driver/t32/tx_isp_t32_ispres.h) against
 * the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc,
 * built for the host with env.c).  Random worlds built twice (core / private NULL, error or
 * valid; channel states, AOV and sleep flags, 0..4 pipes, frame channels with a queued buffer,
 * a dropped buffer or none, autozoom enable words, sensors with or without a resume op and
 * scripted results); compared: return value, call log (isp_printf level / line / defined
 * argument, FIFO clear / write, AOV wake-up, chx_en, autozoom, channel start, stream on, sensor
 * ops), the bss word f3354 and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispres.h"

u64 ispcore_resume_module(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_f3354[];

#define WSZ	0x5000u
#define MAXL	96
static u32 rs = 0x7e5b3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 ss;
static u32 srnd(void)
{
	ss ^= ss << 13; ss ^= ss >> 17; ss ^= ss << 5;
	return ss;
}
struct lg { unsigned int n; u32 e[MAXL][4]; u32 f3354; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W, SD;
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b; lg.e[lg.n][3] = c;
	}
	lg.n++;
}
static void P32(u32 a, u32 v) { memcpy(T32O_HOST(a), &v, 4); }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a1; (void)a2;
	L(1, a0, a3, a3 == 4104 || a3 == 4143 ? a4 : 0);	/* 4073 / 4082: a4 is an unset stack word */
	return 0;
}
u64 tisp_msca_addr_fifo_clear_all(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(2, 0, 0, 0); return srnd(); }
u64 lib_tisp_aov_wake_up(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(3, a0, 0, 0); return srnd(); }
u64 tisp_msca_chx_en(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(4, a0, a1, a2); return srnd(); }
u64 tisp_msca_addr_fifo_write(u32 a0, u32 a1, u32 a2, u32 a3) { L(5, a0, a1, a2 ^ (a3 * 3)); return srnd(); }
u64 tisp_s_autozoom_control(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(6, a0, a1, 0); return srnd(); }
u64 tisp_msca_channel_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(7, a0, 0, 0); return srnd(); }
u64 lib_tisp_stream_on(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(8, t32_data_f3354[0], 0, 0); return srnd(); }
static u64 r_res(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 k = srnd() % 10;

	(void)a1; (void)a2; (void)a3;
	L(9, a0, 0, 0);
	return k < 7 ? 0 : k < 9 ? (u32)T32_ENOIOCTLCMD : srnd();
}

static int o_call(u32 fn, u32 a0, u32 a1, u32 a2) { return (int)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, 0); }
static void o_log(u32 l, u32 line, u32 a4) { isp_printf(l, 0, 0, line, a4); }
static void o_fc(void) { tisp_msca_addr_fifo_clear_all(0, 0, 0, 0); }
static void o_aw(u32 p) { lib_tisp_aov_wake_up(p, 0, 0, 0); }
static void o_ce(u32 p, u32 c, u32 on) { tisp_msca_chx_en(p, c, on, 0); }
static void o_fw(u32 p, u32 c, u32 a, u32 b) { tisp_msca_addr_fifo_write(p, c, a, b); }
static void o_az(u32 p, u32 z) { tisp_s_autozoom_control(p, z, 0, 0); }
static void o_cs(u32 n) { tisp_msca_channel_start(n, 0, 0, 0); }
static void o_so(void) { lib_tisp_stream_on(0, 0, 0, 0); }
static const struct t32_res_ops ops = { o_call, o_log, o_fc, o_aw, o_ce, o_fw, o_az, o_cs, o_so, t32_data_f3354 };

static u32 badp(void) { return rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff; }
static void build(u32 seed)
{
	u32 save = rs, i, P, k;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4)
		P32(W + i, rnd());
	SD = rnd() % 16 ? W : badp();
	P = W + 0x100;
	t32_ic_w32(W, T32_SD_PRIV, rnd() % 12 ? P : badp());
	for (i = 0; i < 4; i++)
		t32_ic_w32(P, T32_PM_CH_STATE + 4 * i, rnd() % 12 ? 2 + rnd() % 3 : rnd() % 2 ? rnd() % 2 : rnd());
	t32_ic_w32(P, T32_PM_AOV, rnd() % 2 ? 0 : rnd());
	t32_ic_w32(P, T32_PM_SLEEP, rnd() % 2 ? 0 : rnd());
	t32_ic_w32(P, T32_RES_NPIPES, rnd() % 5);
	for (i = 0; i < 4 * 15; i++)
		t32_ic_w32(P, T32_RES_ZOOM + 4 * i, rnd() % 4 ? rnd() % 3 : rnd());
	t32_ic_w32(P, T32_FD_FCH, W + 0x1400);
	for (i = 0; i < 12; i++) {
		u32 fc = W + 0x1400 + i * T32_FD_FCH_SIZE, q = W + 0x2400 + 0x40 * i, d = W + 0x2800 + 0x40 * i;

		t32_ic_w32(fc, T32_FC_HASBUF, rnd() % 2 ? 0 : rnd());
		k = rnd() % 4;
		t32_ic_w32(fc, T32_FC_QUEUE, k == 0 ? fc + T32_FC_QUEUE : k == 1 ? 0 : q);
		t32_ic_w32(fc, T32_FC_QUEUE + 4, W + 0x2c00 + 4 * (rnd() % 256));
		t32_ic_w32(fc, T32_FC_DROPQ, rnd() % 2 ? fc + T32_FC_DROPQ : d);
		t32_ic_w32(q, 0, W + 0x3000 + 4 * (rnd() % 256));
		t32_ic_w32(q, 4, W + 0x3400 + 4 * (rnd() % 256));
		t32_ic_w32(d, 0, W + 0x3000 + 4 * (rnd() % 256));
		t32_ic_w32(d, 4, W + 0x3400 + 4 * (rnd() % 256));
	}
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		u32 s = W + 0x3800 + 0x110 * i, o = W + 0x4900 + 0x20 * i, pm = W + 0x4b00 + 0x20 * i;

		k = rnd() % 8;
		t32_ic_w32(P, T32_SD_SENSORS + 4 * i, k < 4 ? s : k < 6 ? 0 : 0xfffff001u + rnd() % 0xfff);
		t32_ic_w32(s, T32_SD_OPS, o);
		t32_ic_w32(o, T32_OPS_PM, rnd() % 5 ? pm : 0);
		t32_ic_w32(pm, T32_PM_RESUME, rnd() % 4 ? T32O_PTR(r_res) : 0);
	}
	t32_data_f3354[0] = rnd();
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}

static u64 ours(u32 sd, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return (u32)t32_ispcore_resume_module(&ops, sd);
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 20000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, SD, 0, 0, 0, 0);
		lg.f3354 = t32_data_f3354[0];
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ispcore_resume_module, SD, rnd(), rnd(), rnd(), 0);
		lg.f3354 = t32_data_f3354[0];
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispres_oracle: MISMATCH seed %08x: %d/%d log %u/%u\n", seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispres_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
