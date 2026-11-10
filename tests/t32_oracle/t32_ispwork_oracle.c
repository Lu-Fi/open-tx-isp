// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable frame-done / frame-start sensor work items
 * (driver/t32/tx_isp_t32_ispwork.h) against the vendor ispcore_irq_fd_work0..3 and
 * ispcore_irq_fs_work0..3 from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc,
 * built for the host with env.c; the fd jump tables are the OEM code addresses of data.c).
 * Each case builds a random world (core subdev with ops tables, private block with 4 x 12 pending
 * updates, per-channel mode words 0 / 1 / other, reset byte) twice and runs one work on each side.
 * Recorder stubs: ispcore_sensor_ops_ioctl (core, cmd, both argument words), the sensor and core
 * ops entries, tx_isp_core_shvflip, ispcoer_debug_info_fs_trige, tisp_core_reset.  Compared: the
 * call log and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispwork.h"

u64 ispcore_irq_fd_work0(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_irq_fd_work1(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_irq_fd_work2(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_irq_fd_work3(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_irq_fs_work0(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_irq_fs_work1(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_irq_fs_work2(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_irq_fs_work3(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_f3310[1];

#define WSZ	0x8000u
#define MAXL	64
static u32 rs = 0x30f5d0u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
struct lg { unsigned int n; u32 e[MAXL][5]; };
static struct lg lg, lg_ours;
static void L(u32 id, u32 a, u32 b, u32 c, u32 d)
{
	if (lg.n < MAXL) {
		u32 *p = lg.e[lg.n];

		p[0] = id; p[1] = a; p[2] = b; p[3] = c; p[4] = d;
	}
	lg.n++;
}
static u32 W32(u32 a) { u32 v; memcpy(&v, T32O_HOST(a), 4); return v; }
u64 ispcore_sensor_ops_ioctl(u32 core, u32 cmd, u32 arg, u32 a3) { (void)a3; L(1, core, cmd, W32(arg), W32(arg + 4)); return rnd(); }
u64 tx_isp_core_shvflip(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(2, a0, 0, 0, 0); return 0; }
u64 ispcoer_debug_info_fs_trige(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(3, a0, a1, 0, 0); return 0; }
u64 tisp_core_reset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(4, a0, a1, 0, 0); return 0; }
static u64 rec_sops(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(5, a0, a1, W32(a2), W32(a2 + 4)); return rnd(); }
static u64 rec_cev(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(6, a0, a1, a2, 0); return rnd(); }

static int o_call(u32 fn, u32 a0, u32 a1, u32 a2) { return (int)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, 0); }
static void o_si(u32 core, u32 cmd, u32 *arg) { ispcore_sensor_ops_ioctl(core, cmd, T32O_PTR(arg), 0); }
static void o_shv(u32 ch) { tx_isp_core_shvflip(ch, 0, 0, 0); }
static void o_tr(u32 w, u32 ch) { ispcoer_debug_info_fs_trige(w, ch, 0, 0); }
static void o_rst(u32 p, u32 a) { tisp_core_reset(p, a, 0, 0); }
static const struct t32_work_ops ops = { o_call, o_si, o_shv, o_tr, o_rst };

/* ours on the low stack (its argument words must have 32-bit addresses) */
static u64 t32_ispcore_irq_fd_work_tramp(u32 ch, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32_ispcore_irq_fd_work(&ops, t32_data_f3310[0], ch);
	return 0;
}
static u64 t32_ispcore_irq_fs_work_tramp(u32 ch, u32 work, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	(void)work;
	t32_ispcore_irq_fs_work(&ops, t32_data_f3310[0], ch);
	return 0;
}

static u8 *world, snap[WSZ];
static u32 W;
static void build(u32 seed, int nullpriv_ok)
{
	u32 save = rs, i, ch, P, o;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	P = rnd() % 10 || !nullpriv_ok ? W + 0x1000 : 0;	/* fs_work0 reads the reset byte through it: never NULL there */
	t32_ic_w32(W, T32_SD_PRIV, P);
	o = W + 0x400;
	t32_ic_w32(W, T32_SD_OPS, o);
	t32_ic_w32(o, T32_OPS_SENSOR, rnd() % 6 ? W + 0x500 : 0);
	t32_ic_w32(o, T32_OPS_CORE, rnd() % 6 ? W + 0x580 : 0);
	t32_ic_w32(W + 0x500, T32_SENSOR_IOCTL, rnd() % 5 ? T32O_PTR(rec_sops) : 0);
	t32_ic_w32(W + 0x580, T32_CORE_OPS_EVENT, rnd() % 5 ? T32O_PTR(rec_cev) : 0);
	if (!P)
		P = W + 0x1000;	/* NULL private block: the tables below are filler then */
	*(u8 *)T32O_HOST(P + T32_PRIV_RESET) = rnd() % 3 ? 0 : rnd();
	for (ch = 0; ch < 4; ch++) {
		u32 m = rnd() % 4;

		t32_ic_w32(P, T32_PRIV_MODE + 104 * ch, W + 0x3000 + 0x200 * ch);
		t32_ic_w32(W + 0x3000 + 0x200 * ch, T32_MODE_WORD, m < 3 ? m : rnd());
		for (i = 0; i < T32_WORK_N; i++)
			t32_ic_w32(P + T32_PRIV_FS_WORK + 96 * ch + 8 * i, 0, rnd() % 2 ? 0 : rnd() % 4 ? 1 : rnd());
	}
	t32_data_f3310[0] = W;
	memset(&lg, 0, sizeof(lg));
	rs = save;
}

int main(void)
{
	static u64 (*const fd[4])(u32, u32, u32, u32) = { ispcore_irq_fd_work0, ispcore_irq_fd_work1, ispcore_irq_fd_work2, ispcore_irq_fd_work3 };
	static u64 (*const fs[4])(u32, u32, u32, u32) = { ispcore_irq_fs_work0, ispcore_irq_fs_work1, ispcore_irq_fs_work2, ispcore_irq_fs_work3 };
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 16000; it++) {
		u32 seed = rnd(), ch = it % 4, isfs = (it >> 2) & 1, work = rnd();

		build(seed, !(isfs && !ch));
		if (isfs)
			t32o_call((t32o_fn)(void (*)(void))t32_ispcore_irq_fs_work_tramp, ch, work, 0, 0, 0);
		else
			t32o_call((t32o_fn)(void (*)(void))t32_ispcore_irq_fd_work_tramp, ch, 0, 0, 0, 0);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed, !(isfs && !ch));
		t32o_call((t32o_fn)(void (*)(void))(isfs ? fs[ch] : fd[ch]), work, rnd(), rnd(), rnd(), 0);
		compared++;
		if (lg.n != lg_ours.n || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispwork_oracle: MISMATCH %s_work%u seed %08x log %u/%u\n", isfs ? "fs" : "fd", ch, seed, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispwork_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
