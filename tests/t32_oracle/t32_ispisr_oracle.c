// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_interrupt_service_routine
 * (driver/t32/tx_isp_t32_ispisr.h) against the vendor function from Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).  Random worlds
 * built twice (status / error / stream-off bits, sensor mode 0..4, stop and stream-off flags,
 * pipe 0..5 and sequence table, all bss flags, frame wait table, time stamp state, channel
 * states / queues / drop lists, sensor present or not, hook return values); compared: return
 * value, the ordered call log of every hook (arguments, the 28 B stop event, the sensor event
 * argument block, completion count at each call), completions, printf count, all bss words,
 * every byte of the world and the register windows.  Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispisr.h"

u64 ispcore_interrupt_service_routine(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_complete(u32 a0, u32 a1, u32 a2, u32 a3);
extern u8 t32_data_f335f[], t32_data_f335e[], t32_data_f335d[], t32_data_f335c[];
extern u32 t32_data_f334c[], t32_data_f3344[], t32_data_f3350[], t32_data_f3354[], t32_data_f3358[];
extern u32 t32_data_f331c[], t32_data_f3314[], t32_data_d9014[], system_wq;
extern u32 t32_data_d8ca0[], t32_data_d8cb0[], t32_data_d8cc0[], t32_data_d8cd0[];
extern u32 t32_data_c34dc[], t32_data_ccda0[], t32_data_ccdd8[];

#define WSZ	0x8000u
#define RSZ	0x42000u
#define MAXL	64
static u32 rs = 0x15b734u;
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
struct lg {
	unsigned int n; u32 e[MAXL][5];
	u32 bss[10], printfs, cn, carg;
	u8 win[3][64];
};
static struct lg lg, lg_ours;
static u8 *world, *regs, snap[WSZ];
static u32 W, R, SD;
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b; lg.e[lg.n][3] = c;
		lg.e[lg.n][4] = t32o_complete_n ^ (t32o_complete_arg << 8);
	}
	lg.n++;
}
static void P32(u32 a, u32 v) { memcpy(T32O_HOST(a), &v, 4); }
static u32 G32(u32 a) { u32 v; memcpy(&v, T32O_HOST(a), 4); return v; }
static u32 hash(u32 a, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ ((u8 *)T32O_HOST(a))[i]) * 16777619u;
	return h;
}

/* the vendor callees: log, return a seeded value */
u64 lib_tisp_intp_handler(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 i, k = srnd();

	(void)a1; (void)a2; (void)a3;
	L(1, 0, 0, 0);
	P32(a0, (k & 1 ? 1 : 0) | (k & 2 ? 1U << 20 : 0) | (k & 4 ? 1U << 24 : 0) | (srnd() & 0xfefffffeU & -(k >> 31)));
	P32(a0 + 4, srnd() % 8);
	P32(a0 + 8, k & 0x18 ? 0 : (k & 0x60) == 0x60 ? 0x8000 : srnd() & (k & 0x80 ? 0x30 : ~0U));
	P32(a0 + 12, srnd());
	P32(a0 + 16, srnd());
	P32(a0 + 20, (srnd() & ~0x10000U) | (k & 0x700 ? 0 : 0x10000));
	for (i = 6; i < 8; i++)
		P32(a0 + 4 * i, srnd());
	return srnd();
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	(void)a1; (void)a2;
	t32o_log.printfs++;
	if (a0 == 1)
		L(2, a3, a4, a5);
	else
		L(3, a0, a3, 0);
	return srnd();
}
#define STUB0(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(id, 0, 0, 0); return srnd(); }
#define STUB1(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(id, a0, 0, 0); return srnd(); }
#define STUB2(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(id, a0, a1, 0); return srnd(); }
#define STUB3(name, id) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(id, a0, a1, a2); return srnd(); }
STUB1(lib_tisp_core_reset, 4)
STUB0(lib_tisp_vic_reset, 5)
STUB0(lib_tisp_vic_stop, 6)
STUB0(lib_tisp_intp_disable, 7)
STUB0(lib_tisp_func_stop_reset, 8)
STUB0(ivdc_isp_info_clear, 9)
STUB0(lib_tisp_trig, 10)
STUB0(lib_tisp_rcfg_switch_interrupt, 11)
STUB1(lib_tisp_msca_chx_reg_stream_off, 12)
STUB1(lib_tisp_msca_chx_stream_off, 13)
STUB3(tisp_msca_chx_en, 15)
STUB1(tisp_msca_addr_fifo_clear_reg_byindex, 16)
STUB1(tisp_ldc_reg_reflash, 18)
STUB1(tisp_rcfg_cfg_trig, 19)
STUB1(private_do_gettimeofday, 20)
STUB3(ispcore_channel_framedone_process_isra_16, 21)
STUB1(__ispcore_single_cam_check_frame_drop, 23)
STUB3(queue_work_on, 25)
u64 lib_tisp_intp_process(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(14, a0, 0, 0); return srnd(); }
u64 lib_tisp_get_msca_chx_en(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(24, a0, a1, 0); return srnd() % 3; }
u64 tisp_msca_addr_fifo_write(u32 a0, u32 a1, u32 a2, u32 a3) { L(17, a0, a1, a2 ^ (a3 << 1)); return srnd(); }
u64 __ispcore_get_frame_drop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(22, a0, 0, 0); return srnd() % 3 == 0; }
u64 isp_core_event_push(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(26, a0, hash(a1, 28), 0); return srnd() % 4 == 0; }
static u64 sensor_cb(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(27, a0, a1, G32(a2) ^ (G32(a2 + 4) << 3)); return srnd(); }

static void o_hd(u32 *st) { lib_tisp_intp_handler(T32O_PTR(st), 0, 0, 0); }
static u32 o_pr(u32 p) { return (u32)lib_tisp_intp_process(p, 0, 0, 0); }
static void o_pe(u32 e, u32 r) { isp_printf(1, T32O_PTR(t32_data_ccda0), T32O_PTR(t32_data_c34dc), 1122, e, r); }
static void o_pl(u32 l) { isp_printf(2, T32O_PTR(t32_data_ccdd8), T32O_PTR(t32_data_c34dc), l, 0, 0); }
static void o_cr(u32 p) { lib_tisp_core_reset(p, 0, 0, 0); }
static void o_vr(void) { lib_tisp_vic_reset(0, 0, 0, 0); }
static void o_vs(void) { lib_tisp_vic_stop(0, 0, 0, 0); }
static void o_id(void) { lib_tisp_intp_disable(0, 0, 0, 0); }
static void o_fs(void) { lib_tisp_func_stop_reset(0, 0, 0, 0); }
static void o_ic(void) { ivdc_isp_info_clear(0, 0, 0, 0); }
static void o_tr(void) { lib_tisp_trig(0, 0, 0, 0); }
static void o_rs(void) { lib_tisp_rcfg_switch_interrupt(0, 0, 0, 0); }
static void o_rso(u32 n) { lib_tisp_msca_chx_reg_stream_off(n, 0, 0, 0); }
static void o_so(u32 n) { lib_tisp_msca_chx_stream_off(n, 0, 0, 0); }
static u32 o_ge(u32 p, u32 c) { return (u32)lib_tisp_get_msca_chx_en(p, c, 0, 0); }
static void o_ce(u32 p, u32 c, u32 e) { tisp_msca_chx_en(p, c, e, 0); }
static void o_fc(u32 c) { tisp_msca_addr_fifo_clear_reg_byindex(c, 0, 0, 0); }
static void o_fw(u32 p, u32 c, u32 a, u32 b) { tisp_msca_addr_fifo_write(p, c, a, b); }
static void o_lr(u32 p) { tisp_ldc_reg_reflash(p, 0, 0, 0); }
static void o_rt(u32 p) { tisp_rcfg_cfg_trig(p, 0, 0, 0); }
static void o_tv(u32 t) { private_do_gettimeofday(t, 0, 0, 0); }
static void o_cp(u32 c) { private_complete(c, 0, 0, 0); }
static void o_fd(u32 a, u32 p, u32 c) { ispcore_channel_framedone_process_isra_16(a, p, c, 0); }
static u32 o_gd(u32 f) { return (u32)__ispcore_get_frame_drop(f, 0, 0, 0); }
static void o_cd(u32 f) { __ispcore_single_cam_check_frame_drop(f, 0, 0, 0); }
static int o_ep(u32 c, u32 *ev) { return (int)(u32)isp_core_event_push(c, T32O_PTR(ev), 0, 0); }
static void o_qw(u32 c, u32 q, u32 w) { queue_work_on(c, q, w, 0); }
static void o_call(u32 fn, u32 a, u32 b, u32 c) { ((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a, b, c, 0); }
static struct t32_is_glob glob;
static const struct t32_is_ops ops = {
	o_hd, o_pr, o_pe, o_pl, o_cr, o_vr, o_vs, o_id, o_fs, o_ic, o_tr, o_rs, o_rso, o_so, o_ge, o_ce, o_fc, o_fw,
	o_lr, o_rt, o_tv, o_cp, o_fd, o_gd, o_cd, o_ep, o_qw, o_call, &glob,
};

static u8 b01(void) { u32 k = rnd() % 8; return k < 3 ? 0 : k < 7 ? 1 : (u8)rnd(); }

static void list(u32 head, u32 pool)
{
	u32 k = rnd() % 4, e = pool + 0x40 * (rnd() % 16);

	if (k == 0) {
		P32(head, head);
		return;
	}
	P32(head, e);
	P32(e, W + 0x6000 + 0x10 * (rnd() % 64));
	P32(e + 4, W + 0x6400 + 0x10 * (rnd() % 64));
}

static void build(u32 seed)
{
	u32 save = rs, i, priv, fch, fc, sen;

	t32o_reset();
	world = t32o_alloc(WSZ);
	regs = t32o_alloc(RSZ);
	W = T32O_PTR(world);
	R = T32O_PTR(regs);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4)
		P32(W + i, rnd());
	for (i = 0; i < 64; i += 4) {
		P32(R + 0x100 + i, rnd());
		P32(R + 0x40000 + i, rnd());
		P32(R + 0x41000 + i, rnd());
	}
	SD = W;
	priv = W + 0x200;
	fch = W + 0x1400;
	P32(SD + T32_IS_PRIV, priv);
	P32(SD + T32_IS_REGS, R - 0x40000);
	P32(priv + T32_FD_MODE, rnd() % 2 ? 1 : rnd() % 5);
	P32(priv + T32_IS_STOP, rnd() % 4 ? b01() : rnd());
	P32(priv + T32_IS_STREAMOFF, b01());
	P32(priv + T32_IS_WAIT_ON, b01());
	P32(priv + T32_IS_WAIT_IDX, rnd() % 6);
	P32(priv + T32_IS_WAIT_LIMIT, rnd() & 0x1ffff);
	for (i = 0; i < 8; i++) {
		P32(priv + T32_IS_WAIT + 4 * i, rnd() % 2 ? rnd() | 0x80000000U : rnd());
		P32(priv + T32_IS_EV3 + 4 * i, b01());
	}
	if (rnd() % 3 == 0)	/* limit reached exactly */
		P32(priv + T32_IS_WAIT_LIMIT, G32(priv + T32_IS_WAIT + 4 * (rnd() % 6)) & 0xffff);
	*T32_IC_P(priv + T32_IS_TS_STATE) = rnd() % 6;
	P32(priv + T32_FD_FCH, fch);
	for (i = 0; i < 18; i++) {
		fc = fch + i * T32_FD_FCH_SIZE;
		P32(fc + T32_FC_STATE, rnd() % 3 ? rnd() : 0);
		P32(fc + T32_FC_FRAMES, rnd() % 4 ? rnd() % 18 : rnd());
		if (G32(fc + T32_FC_STATE) && G32(fc + T32_FC_FRAMES) >= 18)
			P32(fc + T32_FC_FRAMES, rnd() % 18);
		list(fc + T32_FC_QUEUE, W + 0x4000);
		list(fc + T32_FC_DROPQ, W + 0x4800);
		P32(fc + T32_FC_QUEUE + 4, W + 0x6800 + 0x10 * (rnd() % 64));
	}
	sen = rnd() % 4 ? W + 0x7800 : 0;
	P32(priv + T32_IS_SENSOR, sen);
	P32(W + 0x7800 + T32_IS_SENSOR_CB, T32O_PTR(sensor_cb));
	for (i = 0; i < 12; i++)
		world[0x7c00 + i] = rnd() % 8 ? rnd() % 4 : rnd() % 6;
	t32_data_f3314[0] = W + 0x7c00;
	t32_data_f334c[0] = rnd() % 8 ? rnd() % 4 : rnd() % 6;
	t32_data_f335f[0] = b01();
	t32_data_f335e[0] = b01();
	t32_data_f335d[0] = b01();
	t32_data_f335c[0] = rnd() % 4 ? 0 : b01();
	t32_data_f3344[0] = rnd() & ~0xffU;
	t32_data_f3344[0] |= b01();
	t32_data_f3350[0] = rnd();
	t32_data_f3354[0] = rnd() % 2 ? rnd() % 24 : rnd();
	t32_data_f3358[0] = rnd() & 0x01010101U & (rnd() | rnd());
	t32_data_d9014[0] = b01();
	system_wq = rnd();
	t32o_complete_n = 0;
	t32o_complete_arg = 0;
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}

static u64 ours(u32 sd, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return (u32)t32_ispcore_interrupt_service_routine(&ops, sd);
}

static void fin(void)
{
	lg.bss[0] = t32_data_f334c[0];
	lg.bss[1] = t32_data_f335f[0] | t32_data_f335e[0] << 8 | t32_data_f335d[0] << 16 | (u32)t32_data_f335c[0] << 24;
	lg.bss[2] = t32_data_f3344[0];
	lg.bss[3] = t32_data_f3350[0];
	lg.bss[4] = t32_data_f3354[0];
	lg.bss[5] = t32_data_f3358[0];
	lg.bss[6] = t32_data_f3314[0];
	lg.bss[7] = t32_data_d9014[0];
	lg.bss[8] = system_wq;
	lg.printfs = t32o_log.printfs;
	lg.cn = t32o_complete_n;
	lg.carg = t32o_complete_arg;
	memcpy(lg.win[0], regs + 0x100, 64);
	memcpy(lg.win[1], regs + 0x40000, 64);
	memcpy(lg.win[2], regs + 0x41000, 64);
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;
	struct t32_is_glob g = {
		(u8 *)t32_data_f334c, t32_data_f335f, t32_data_f335e, (u8 *)t32_data_f3344, t32_data_f3354, t32_data_f3350,
		(u8 *)t32_data_f3358, t32_data_f335c, t32_data_f335d, t32_data_f3314, t32_data_d9014, &system_wq,
		T32O_PTR(t32_data_f331c),
		{ T32O_PTR(t32_data_d8cd0), T32O_PTR(t32_data_d8cc0), T32O_PTR(t32_data_d8cb0), T32O_PTR(t32_data_d8ca0) },
	};

	glob = g;
	for (it = 0; it < 40000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, SD, 0, 0, 0, 0);
		fin();
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ispcore_interrupt_service_routine, SD, rnd(), rnd(), rnd(), 0);
		fin();
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispisr_oracle: MISMATCH seed %08x: %d/%d log %u/%u\n", seed, r, rr, lg_ours.n, lg.n);
			if (getenv("T32O_DBG") && failed < 6) {
				unsigned int j;

				for (j = 0; j < MAXL && j < lg.n; j++)
					if (memcmp(lg.e[j], lg_ours.e[j], 20))
						printf("  log %u: id %u/%u %x/%x %x/%x %x/%x c %x/%x\n", j, lg_ours.e[j][0], lg.e[j][0],
						       lg_ours.e[j][1], lg.e[j][1], lg_ours.e[j][2], lg.e[j][2], lg_ours.e[j][3], lg.e[j][3],
						       lg_ours.e[j][4], lg.e[j][4]);
				for (j = 0; j < WSZ; j++)
					if (snap[j] != world[j])
						printf("  world +%x: %02x/%02x\n", j, snap[j], world[j]);
				for (j = 0; j < 10; j++)
					if (lg.bss[j] != lg_ours.bss[j])
						printf("  bss %u: %x/%x\n", j, lg_ours.bss[j], lg.bss[j]);
				if (memcmp(lg.win, lg_ours.win, sizeof(lg.win)))
					printf("  reg window\n");
				printf("  cn %u/%u printfs %u/%u\n", lg_ours.cn, lg.cn, lg_ours.printfs, lg.printfs);
			}
		}
	}
	printf("t32_ispisr_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
