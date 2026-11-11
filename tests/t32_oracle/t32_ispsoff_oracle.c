// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_frame_channel_streamoff (driver/t32/tx_isp_t32_ispsoff.h)
 * against the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc, built for the host with env.c).  Random worlds built twice (valid core: a NULL /
 * error core makes the vendor dereference private block 0, channel and fc states 4 or not, MSCA index
 * 0..11, current pipe, no-wait flag, work state busy for 0..600 polls incl. the 502 timeout);
 * compared: return value, call log (lock / unlock with flags, stream off calls, work_state
 * arguments, msleep, isp_printf format and index, fifo clear) and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispsoff.h"

u64 ispcore_frame_channel_streamoff(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_f334c[1], t32_data_d9004[1], t32_data_cc960[1], t32_data_cc97c[1];

#define WSZ	0x2000u
#define MAXL	1100
static u32 rs = 0x50ffu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 ss, busy;
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
u64 lib_tisp_msca_chx_stream_off(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(3, a0, 0); return srnd(); }
u64 lib_tisp_msca_chx_reg_stream_off(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(4, a0, 0); return srnd(); }
u64 tisp_msca_work_state(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	L(5, a0, a1);
	if (!busy)
		return 0;
	busy--;
	return srnd() | 1;
}
u64 private_msleep(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(6, a0, 0); return 0; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	L(7, a0 << 8 | (a1 == T32O_PTR(t32_data_cc960) ? 0 : a1 == T32O_PTR(t32_data_cc97c) ? 1 : 9), a2);
	return 0;
}
u64 tisp_msca_addr_fifo_clear_byindex(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(8, a0, 0); return 0; }

static u32 o_lock(u32 l)
{
	static u32 f;

	__private_spin_lock_irqsave(l, T32O_PTR(&f), 0, 0);
	return f;
}
static void o_unlock(u32 l, u32 f) { private_spin_unlock_irqrestore(l, f, 0, 0); }
static void o_so(u32 i) { lib_tisp_msca_chx_stream_off(i, 0, 0, 0); }
static void o_rso(u32 i) { lib_tisp_msca_chx_reg_stream_off(i, 0, 0, 0); }
static int o_ws(u32 p, u32 w) { return (int)tisp_msca_work_state(p, w, 0, 0); }
static void o_ms(u32 m) { private_msleep(m, 0, 0, 0); }
static void o_log(u32 m, u32 i) { isp_printf(1, m ? T32O_PTR(t32_data_cc97c) : T32O_PTR(t32_data_cc960), i, 0); }
static void o_fc(u32 i) { tisp_msca_addr_fifo_clear_byindex(i, 0, 0, 0); }
static const struct t32_soff_ops ops = { o_lock, o_unlock, o_so, o_rso, o_ws, o_ms, o_log, o_fc };
static const struct t32_soff_glob g = { (const u8 *)t32_data_f334c, t32_data_d9004 };

static u64 ours(u32 chan, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return (u32)t32_ispcore_frame_channel_streamoff(&ops, &g, chan);
}

static u32 nbusy;
static void build(u32 seed)
{
	u32 save = rs, i, k;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	/* chan at W, core at W + 0x100 (+280 -> private W + 0x400, +376 -> mode block W + 0x900), fc at W + 0xc00 */
	t32_ic_w32(W, 0, W + 0x100);	/* a NULL / error core reads the mode block through private 0 (crash in the vendor too) */
	t32_ic_w32(W + 0x100, T32_SD_PRIV, W + 0x400);
	t32_ic_w32(W + 0x400, 376, W + 0x900);
	*(u8 *)T32O_HOST(W + T32_CHAN_STATE) = rnd() % 5 ? 4 : rnd();
	t32_ic_w32(W, T32_CHAN_FC, W + 0xc00);
	t32_ic_w32(W + 0xc00, T32_FC_STATE, rnd() % 5 ? 4 : rnd() % 8);
	t32_ic_w32(W + 0xc00, T32_FC_INDEX, rnd() % 12);
	t32_data_f334c[0] = rnd() % 5;
	t32_data_d9004[0] = rnd() % 6 ? 0 : rnd();
	k = rnd() % 8;
	nbusy = k < 4 ? 0 : k < 7 ? rnd() % 20 : 490 + rnd() % 30;
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x7f4a7c15u) | 1;
	busy = nbusy;
	rs = save;
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 6000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, W, 0, 0, 0, 0);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ispcore_frame_channel_streamoff, W, rnd(), rnd(), rnd(), 0);
		compared++;
		if (r != rr || lg.n != lg_ours.n || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispsoff_oracle: MISMATCH seed %08x: %d/%d log %u/%u\n", seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispsoff_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
