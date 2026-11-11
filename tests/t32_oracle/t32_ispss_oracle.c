// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_video_s_stream (driver/t32/tx_isp_t32_ispss.h) against the
 * vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built
 * for the host with env.c).  Random worlds built twice (channel states 0..4, channel count 1..5,
 * d9008, +4012, core of t32_data_f3310 with or without private block, 12 frame channels with
 * states, sensors with or without video s_stream and scripted results); compared: return value,
 * call log (lock / unlock, isp_printf, interrupt and irq calls, stream_on, frame channel
 * streamoff, sensor calls), the globals f3314 / f3354 and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispss.h"

u64 ispcore_video_s_stream(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_f3310[1], t32_data_d9008[1], t32_data_f3314[1], t32_data_f3354[1];
extern u32 t32_data_d8e6c[3], t32_data_d8e78[3], t32_data_d8e84[3], t32_data_f3360[3];

#define WSZ	0x4000u
#define MAXL	48
static u32 rs = 0x55e4u;
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
struct lg { unsigned int n; u32 e[MAXL][3]; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W;
static void L(u32 id, u32 a, u32 b)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b;
	}
	lg.n++;
}
u64 __private_spin_lock_irqsave(u32 l, u32 fp, u32 a2, u32 a3)
{
	u32 f = srnd();

	(void)a2; (void)a3;
	L(1, l, 0);
	memcpy(T32O_HOST(fp), &f, 4);
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 l, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; L(2, l, f); return 0; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; L(3, a0, a3); return 0; }
u64 lib_tisp_intp_enable(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(4, 0, 0); return srnd(); }
u64 lib_tisp_intp_disable(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(5, 0, 0); return srnd(); }
u64 tx_isp_enable_irq_b977c(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(6, a0, a1); return srnd(); }
u64 tx_isp_disable_irq_b9800(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(7, a0, a1); return srnd(); }
u64 lib_tisp_stream_on(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(8, 0, 0); return srnd(); }
u64 ispcore_frame_channel_streamoff(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(9, a0, 0); return srnd(); }
static u64 r_ss(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 k = srnd() % 8;

	(void)a2; (void)a3;
	L(10, a0, a1);
	return k < 4 ? 0 : k < 6 ? (u32)T32_ENOIOCTLCMD : srnd();
}

static int o_call(u32 fn, u32 a0, u32 a1, u32 a2) { return (int)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, 0); }
static u32 o_lock(u32 l)
{
	static u32 f;

	__private_spin_lock_irqsave(l, T32O_PTR(&f), 0, 0);
	return f;
}
static void o_unlock(u32 l, u32 f) { private_spin_unlock_irqrestore(l, f, 0, 0); }
static void o_log(u32 l, u32 line) { isp_printf(l, 0, 0, line); }
static void o_ie(void) { lib_tisp_intp_enable(0, 0, 0, 0); }
static void o_id(void) { lib_tisp_intp_disable(0, 0, 0, 0); }
static void o_eq(u32 sd, u32 ch) { tx_isp_enable_irq_b977c(sd, ch, 0, 0); }
static void o_dq(u32 sd, u32 ch) { tx_isp_disable_irq_b9800(sd, ch, 0, 0); }
static void o_so(void) { lib_tisp_stream_on(0, 0, 0, 0); }
static void o_fs(u32 c) { ispcore_frame_channel_streamoff(c, 0, 0, 0); }
static const struct t32_ss_ops ops = { o_call, o_lock, o_unlock, o_log, o_ie, o_id, o_eq, o_dq, o_so, o_fs };
static struct t32_ss_glob g = { t32_data_f3310, t32_data_d9008, t32_data_f3354, t32_data_f3314, 0, 0, 0, 0 };

static void build(u32 seed)
{
	u32 save = rs, i, P;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	P = W + 0x400;
	t32_ic_w32(W, T32_SD_PRIV, P);
	for (i = 0; i < 4; i++)
		t32_ic_w32(P, T32_PM_CH_STATE + 4 * i, rnd() % 6 ? 2 + rnd() % 3 : rnd() % 2);
	t32_ic_w32(P, T32_PRIV_NCH, rnd() % 5 + (rnd() % 8 == 0));
	t32_ic_w32(P, T32_PM_SLEEP, rnd() % 3 ? 0 : 1);
	t32_ic_w32(P, T32_PRIV_FCH, W + 0x1000);
	for (i = 0; i < T32_NFCH_FIXED; i++)
		t32_ic_w32(W + 0x1000 + i * T32_FCH_SIZE, T32_FCH_STATE, rnd() % 3 ? 4 : rnd() % 5);
	/* the core of t32_data_f3310: W + 0x2400, private block or none */
	t32_data_f3310[0] = W + 0x2400;
	t32_ic_w32(W + 0x2400, T32_SD_PRIV, rnd() % 4 ? P : 0);
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		u32 s = W + 0x2600 + 0x80 * i, o = W + 0x3000 + 0x40 * i, v = W + 0x3400 + 0x40 * i, k = rnd() % 8;

		t32_ic_w32(W, T32_SD_SENSORS + 4 * i, k < 4 ? s : 0);
		t32_ic_w32(s, T32_SD_OPS, o);
		t32_ic_w32(o, T32_OPS_VIDEO, rnd() % 5 ? v : 0);
		t32_ic_w32(v, 0, rnd() % 4 ? T32O_PTR(r_ss) : 0);
	}
	t32_ic_w32(W + 0x3c00, 0, rnd() % 2 ? 0 : rnd() | 1);	/* req: enable, channel */
	t32_ic_w32(W + 0x3c00, 4, rnd() % 4);
	t32_data_d9008[0] = rnd() % 3 ? 0 : 1;
	t32_data_f3314[0] = rnd();
	t32_data_f3354[0] = rnd();
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x1b873593u) | 1;
	rs = save;
}

static u64 ours(u32 sd, u32 req, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	return (u32)t32_ispcore_video_s_stream(&ops, &g, sd, req);
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	g.tab3 = T32O_PTR(t32_data_d8e78);
	g.tab4 = T32O_PTR(t32_data_d8e6c);
	g.tab2 = T32O_PTR(t32_data_d8e84);
	g.tab_other = T32O_PTR(t32_data_f3360);
	for (it = 0; it < 20000; it++) {
		u32 seed = rnd(), t14, t54;
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, W, W + 0x3c00, 0, 0, 0);
		lg_ours = lg;
		t14 = t32_data_f3314[0];
		t54 = t32_data_f3354[0];
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ispcore_video_s_stream, W, W + 0x3c00, rnd(), rnd(), 0);
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ) ||
		    t14 != t32_data_f3314[0] || t54 != t32_data_f3354[0]) {
			if (failed++ < 5)
				printf("t32_ispss_oracle: MISMATCH seed %08x: %d/%d log %u/%u\n", seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispss_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
