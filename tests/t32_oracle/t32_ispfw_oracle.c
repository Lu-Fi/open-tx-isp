// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable firmware threads and AOV sleep event (driver/t32/tx_isp_t32_ispfw.h)
 * against the vendor isp_fw_process / 1 / 2 / 3 and isp_event_sleep_process from Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 * Recorder stubs (kthread_should_stop true after a scripted number of polls, lib_tisp_fw_process,
 * sensor_event_streamoff_process, handle_aov_ispsleep); compared: return value, call log, every
 * byte of the world block.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispfw.h"

u64 isp_fw_process(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_fw_process1(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_fw_process2(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_fw_process3(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_event_sleep_process(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);

#define WSZ	0x2000u
#define MAXL	64
static u32 rs = 0xf1e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
struct lg { unsigned int n; u32 e[MAXL][6]; };
static struct lg lg, lg_ours;
static unsigned int polls_left, polls_total, runaway;
static void L(u32 id, u32 a, u32 b, u32 c, u32 d, u32 e)
{
	if (lg.n < MAXL) {
		u32 *p = lg.e[lg.n];

		p[0] = id; p[1] = a; p[2] = b; p[3] = c; p[4] = d; p[5] = e;
	}
	lg.n++;
}
u64 private_kthread_should_stop(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	L(1, 0, 0, 0, 0, 0);
	if (++polls_total > 200) {	/* the scripted loops poll <= 41 times: a runaway loop ends here by compare, not by the time limit */
		runaway++;
		return polls_total & 1;	/* alternate, so a loop of either polarity ends */
	}
	if (!polls_left)
		return 0xbad00000001ull;	/* v1 garbage */
	polls_left--;
	return 0xbad0000000000000ull;
}
u64 lib_tisp_fw_process(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(2, a0, 0, 0, 0, 0); return rnd(); }
u64 sensor_event_streamoff_process(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { L(3, a0, a1, a2, a3, a4); return rnd(); }
u64 handle_aov_ispsleep(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(4, a0, 0, 0, 0, 0); return rnd(); }

static int o_stop(void) { return (int)(u32)private_kthread_should_stop(0, 0, 0, 0); }
static void o_fw(u32 ch) { lib_tisp_fw_process(ch, 0, 0, 0); }
static void o_so(u32 a0, u32 sd, u32 a2, u32 at, u32 a4) { sensor_event_streamoff_process(a0, sd, a2, at, a4); }
static void o_aov(u32 p) { handle_aov_ispsleep(p, 0, 0, 0); }
static const struct t32_fw_ops ops = { o_stop, o_fw, o_so, o_aov };

static u8 *world, snap[WSZ];
static u32 W;
static void build(u32 seed)
{
	u32 save = rs, i;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	t32_ic_w32(W, T32_SD_PRIV, W + 0x400 + (rnd() % 0x40) * 4);
	memset(&lg, 0, sizeof(lg));
	polls_total = 0;
	rs = save;
}

#define REF5(fn, a0, a1, a2, a3, a4)	((int)(u32)t32o_call((t32o_fn)(void (*)(void))(fn), (a0), (a1), (a2), (a3), (a4)))

int main(void)
{
	static u64 (*const fw[4])(u32, u32, u32, u32) = { isp_fw_process, isp_fw_process1, isp_fw_process2, isp_fw_process3 };
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 12000; it++) {
		u32 seed = rnd(), ch = it % 4, n = it % 7 == 0 ? 0 : rnd() % 40, a0 = rnd(), a2 = rnd(), a3 = rnd(), a4 = rnd();
		int r, rr;
		unsigned int ro;

		build(seed);
		polls_left = n;
		r = t32_isp_fw_process(&ops, ch);
		lg_ours = lg;
		ro = runaway;
		build(seed);
		polls_left = n;
		rr = REF5(fw[ch], rnd(), rnd(), rnd(), rnd(), 0);
		compared++;
		if (ro || runaway || r != rr || lg.n != lg_ours.n || memcmp(&lg, &lg_ours, sizeof(lg))) {
			if (failed++ < 5)
				printf("t32_ispfw_oracle: MISMATCH fw_process%u seed %08x\n", ch, seed);
		}

		build(seed);
		r = t32_isp_event_sleep_process(&ops, a0, W, a2, a4);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = REF5(isp_event_sleep_process, a0, W, a2, a3, a4);
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispfw_oracle: MISMATCH sleep_process seed %08x\n", seed);
		}
	}
	printf("t32_ispfw_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
