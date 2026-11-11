// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_channel_framedone_process.isra.16
 * (driver/t32/tx_isp_t32_ispfd.h) against the vendor function from Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).  Random worlds
 * built twice (pipe 0..3, channel 0..2, sensor mode 1 or other, drop pattern 0..5, frame counter
 * 0..11 / random, queue empty / NULL / one buffer whose address matches the FIFO or not);
 * compared: return value, call log (FIFO read / write, fps, CSC output and full range, the
 * 52 B event block and pad, printk count), the bss words d9014 / f3348 and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispfd.h"

u64 ispcore_channel_framedone_process_isra_16(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_d9014[], t32_data_f3348[];
u64 printk(u32 a0, u32 a1, u32 a2, u32 a3);

#define WSZ	0x4000u
#define MAXL	16
static u32 rs = 0xfd16eu;
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
struct lg { unsigned int n; u32 e[MAXL][4]; u32 d9014, f3348, printfs; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W, PRIV, PIPE, CH, FADDR;
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b; lg.e[lg.n][3] = c;
	}
	lg.n++;
}
static void P32(u32 a, u32 v) { memcpy(T32O_HOST(a), &v, 4); }
u64 tisp_msca_addr_fifo_read(u32 a0, u32 a1, u32 a2, u32 a3) { L(1, a0, a1, 0); P32(a2, FADDR); P32(a3, srnd()); return srnd(); }
u64 tisp_msca_addr_fifo_write(u32 a0, u32 a1, u32 a2, u32 a3) { L(2, a0, a1, a2 ^ (a3 << 1)); return srnd(); }
u64 tisp_get_fps(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(3, a0, 0, 0); P32(a1, srnd()); P32(a1 + 4, srnd()); return srnd(); }
u64 lib_tisp_api_get_csccr_output(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(4, a0, 0, 0); return srnd(); }
u64 tisp_csc_get_full_range(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(5, a0, 0, 0); return srnd(); }
u64 tx_isp_send_event_to_remote(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 h = 2166136261u, i;

	(void)a3;
	for (i = 0; i < T32_FD_EV_SIZE; i++)
		h = (h ^ ((u8 *)T32O_HOST(a2))[i]) * 16777619u;
	L(6, a0, a1, h);
	return srnd();
}

static void o_fr(u32 p, u32 c, u8 *a, u8 *i) { tisp_msca_addr_fifo_read(p, c, T32O_PTR(a), T32O_PTR(i)); }
static void o_fw(u32 p, u32 s, u32 a, u32 b) { tisp_msca_addr_fifo_write(p, s, a, b); }
static void o_fps(u32 p, u8 *o) { tisp_get_fps(p, T32O_PTR(o), 0, 0); }
static u32 o_cc(u32 p) { return (u32)lib_tisp_api_get_csccr_output(p, 0, 0, 0); }
static u32 o_fu(u32 p) { return (u32)tisp_csc_get_full_range(p, 0, 0, 0); }
static void o_ev(u32 pad, u32 e, u8 *ev) { tx_isp_send_event_to_remote(pad, e, T32O_PTR(ev), 0); }
static void o_em(void) { printk(0, 0, 869, 0); }
static const struct t32_fd_ops ops = { o_fr, o_fw, o_fps, o_cc, o_fu, o_ev, o_em, t32_data_d9014, t32_data_f3348 };

static void build(u32 seed)
{
	u32 save = rs, i, fc, e, k;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4)
		P32(W + i, rnd());
	PRIV = W;
	PIPE = rnd() % 4;
	CH = rnd() % 3;
	t32_ic_w32(PRIV, T32_FD_MODE, rnd() % 2 ? 1 : rnd() % 3);
	t32_ic_w32(PRIV, T32_FD_FCH, W + 0x600);
	fc = W + 0x600 + (3 * PIPE + CH) * T32_FD_FCH_SIZE;
	t32_ic_w32(fc, T32_FC_FRAMES, rnd() % 6 ? rnd() % 12 : rnd());
	k = rnd() % 8;
	e = W + 0x2000 + 0x40 * (rnd() % 16);
	t32_ic_w32(fc, T32_FC_QUEUE, k == 0 ? fc + T32_FC_QUEUE : k == 1 ? 0 : e);
	t32_ic_w32(e, 0, W + 0x2800 + 4 * (rnd() % 256));
	t32_ic_w32(e, 4, W + 0x2c00 + 4 * (rnd() % 256));
	t32_ic_w32(fc, T32_FC_DROPQ + 4, W + 0x3000 + 4 * (rnd() % 256));
	t32_ic_w32(fc, T32_FC_DONEQ + 4, W + 0x3400 + 4 * (rnd() % 256));
	FADDR = rnd() % 3 ? t32_ic_r32(e, 8) : rnd();
	t32_data_d9014[0] = rnd() % 8 ? rnd() % 6 : rnd();
	t32_data_f3348[0] = rnd();
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}

static u64 ours(u32 priv, u32 pipe, u32 ch, u32 a3)
{
	(void)a3;
	return (u32)t32_ispcore_channel_framedone_process(&ops, priv, pipe, ch);
}

static void fin(void)
{
	lg.d9014 = t32_data_d9014[0];
	lg.f3348 = t32_data_f3348[0];
	lg.printfs = t32o_log.printfs;
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 20000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, PRIV, PIPE, CH, 0, 0);
		fin();
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ispcore_channel_framedone_process_isra_16, PRIV, PIPE, CH, rnd(), 0);
		fin();
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispfd_oracle: MISMATCH seed %08x: %d/%d log %u/%u\n", seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispfd_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
