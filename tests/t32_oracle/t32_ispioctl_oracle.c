// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_core_ops_ioctl (driver/t32/tx_isp_t32_ispioctl.h) against
 * the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc,
 * built for the host with env.c).  Phase 1: the ported commands (idx 0, 1, the broadcast-only
 * idx of table c31e0 and random commands outside 0x1000000..0x100001e) on random worlds: core
 * ops present / missing, 16 subdev words NULL / set, ops blocks and functions NULL / set, op
 * results 0 / -515 / random; sd NULL with idx 0 / 1.  Phases 2-4: idx 3, 5, 6, 14, 15, 20, 22, 26, 9 (pipe count 0..5, marks
 * 0 / 1 / random), 16, 21, 23..25, 27 (copy fault, magic right / wrong, AWB word 2).  Compared: return value, the ordered call
 * log (printf, every op with its arguments) and every byte of the world.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispioctl.h"

u64 ispcore_core_ops_ioctl(u32 a0, u32 a1, u32 a2, u32 a3);

/* jump tables (recovered data.c lines 6282..6298, 6299..6329, 6330..6360: OEM label addresses) */
u32 t32_data_c319c[17] = { 0xbbc40, 0xbbc28, 0xbbc8c, 0xbbc64, 0xbbc8c, 0xbbc8c, 0xbbc8c, 0xbbc64, 0xbbc64,
	0xbbc8c, 0xbbc64, 0xbbc8c, 0xbbc64, 0xbbc64, 0xbbc64, 0xbbc64, 0xbbc64 };
u32 t32_data_c31e0[31] = { 0xbbcf4, 0xbbcdc, 0xbbbe0, 0xbbd18, 0xbbbe0, 0xbbdd0, 0xbbecc, 0xbbbe0, 0xbbbe0,
	0xbbe68, 0xbbbe0, 0xbc040, 0xbbbe0, 0xbbbe0, 0xbbf30, 0xbbfa0, 0xbc028, 0xbbbe0, 0xbbbe0, 0xbbbe0,
	0xbc270, 0xbc358, 0xbc370, 0xbc490, 0xbc50c, 0xbc518, 0xbc538, 0xbc6b8, 0xbbbe0, 0xbbbe0, 0xbc758 };
u32 t32_data_c325c[31] = { 0xbcc08, 0xbcc08, 0xbbbe0, 0xbbd38, 0xbbbe0, 0xbbdec, 0xbbec8, 0xbbbe0, 0xbbbe0,
	0xbbe78, 0xbbbe0, 0xbc050, 0xbbbe0, 0xbbbe0, 0xbbf40, 0xbbfb0, 0xbc028, 0xbbbe0, 0xbbbe0, 0xbbbe0,
	0xbc280, 0xbc358, 0xbc380, 0xbc490, 0xbc50c, 0xbc518, 0xbc548, 0xbc6b8, 0xbbbe0, 0xbbbe0, 0xbc768 };
/* strings by address */
u32 t32_data_c33b0[1], t32_data_c33d0[1], t32_data_c33ec[1], t32_data_c340c[1], t32_data_c342c[1];
u32 t32_data_c344c[1], t32_data_c346c[1], t32_data_c3488[1], t32_data_c34a0[1], t32_data_cd4ec[1];
u32 t32_data_cd504[1], t32_data_cd524[1], t32_data_cd550[1], t32_data_cd570[1], t32_data_cd5a4[1];
u32 t32_data_cd5c4[1], t32_data_cd5e4[1], t32_data_cd604[1], t32_data_cd638[1], t32_data_cd6a8[1];
u32 t32_data_cd6fc[1], t32_data_cd71c[1];

#define WSZ	0x10000u
#define MAXL	48
struct lg { unsigned int n; u32 e[MAXL][4]; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W, SD, CMD, ARG, PRIV, rv[8], cf_fail, r_dt, r_wd, r_gi, ct_fail[2], k_pos;
static unsigned int nct;
extern u32 t32_data_f3354[];
static unsigned int nrv;
static u32 rs = 0x6d2b79f5u;
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
static u32 pick(u32 n) { return rnd() % n; }
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b; lg.e[lg.n][3] = c;
	}
	lg.n++;
}
static void P32(u32 a, u32 v) { memcpy(T32O_HOST(a), &v, 4); }

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 s16)
{
	/* the waitdone / mode prints: a2 / a3 are leftovers; only the no-sensor print has a stack word */
	if (a1 == T32O_PTR(t32_data_cd5a4) || a1 == T32O_PTR(t32_data_cd5e4) || a1 == T32O_PTR(t32_data_cd638))
		a2 = a3 = 0;
	if (a1 != T32O_PTR(t32_data_cd6a8))
		s16 = 0;
	L(1, a0, a1, a2 ^ a3 * 3 ^ s16 * 5);
	return 0;
}
/* the subdev ops: ops[0] + 4 (sd, arg), ops[3] + 4 (sd, arg), ops[0] + 32 (sd, cmd, arg) */
static u64 op_core(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(2, a0, a1, 0); return rv[nrv++ % 8]; }
static u64 op_video(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(3, a0, a1, 0); return rv[nrv++ % 8]; }
static u64 op_ioctl(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(4, a0, a1, a2); return rv[nrv++ % 8]; }
static u32 hash(u32 a, u32 n)
{
	const u8 *p = T32O_HOST(a);
	u32 i, h = 2166136261u;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}
/* phase 2 callees */
u64 private_copy_from_user(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	L(5, a1, a2, 0);
	if (cf_fail)
		return cf_fail;
	memcpy(T32O_HOST(a0), T32O_HOST(a1), a2);
	return 0;
}
u64 mbus_to_bayer_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(6, a0, a1, 0); return srnd(); }
u64 tx_isp_core_oneshot_tick_isra_18(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(7, a0, 0, 0); return rv[nrv++ % 8]; }
u64 tisp_ae_algo_deinit(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(8, a0, 0, 0); return srnd(); }
u64 tisp_ae_algo_handle(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(9, a0, hash(a1, 64), 0); return srnd(); }
u64 tisp_awb_algo_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(10, a0, a1, a2); return srnd(); }
u64 tisp_awb_algo_handle(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(11, a0, hash(a1, 24), 0); return srnd(); }
/* phase 3 callees */
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(12, a0, 0, 0); if (srnd() & 1) P32(a1, srnd()); return srnd(); }
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(13, a0, a1, 0); return srnd(); }
u64 lib_tisp_aov_wake_up(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(14, a0, 0, 0); return srnd(); }
u64 lib_tisp_stream_on(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(15, t32_data_f3354[0], 0, 0); return srnd(); }
u64 down_timeout(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(16, a0, a1, 0); return r_dt; }
u64 tisp_core_reset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(17, a0, a1, 0); return srnd(); }
/* phase 4 callees */
u64 tisp_ae_algo_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(19, a0, a1, hash(a2, 112)); return srnd(); }
u64 tisp_math_exp2(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(20, a0, a1, a2); return hash(T32O_PTR(&a0), 4) ^ a1 ^ a2 * 7; }
u64 tisp_ae_algo_waitdone(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(21, a0, 0, 0); return r_wd; }
u64 tisp_ae_algo_getinfo(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(22, a0, 0, 0); return W + 0x5000; }
u64 tisp_awb_algo_waitdone(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(23, a0, 0, 0); return r_wd; }
u64 tisp_awb_algo_getinfo(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	L(24, hash(a0, 36), 0, 0);
	P32(a0 + 8, srnd());
	P32(a0 + 32, srnd());
	return r_gi;
}
u64 private_copy_to_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(25, a0, hash(a1, a2), a2); return ct_fail[nct++ % 2]; }
/* phase 5 callees */
u64 tisp_msca_addr_fifo_clear_all(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(26, 0, 0, 0); return srnd(); }
u64 tisp_msca_addr_fifo_write(u32 a0, u32 a1, u32 a2, u32 a3) { L(27, a0, a1, a2 ^ a3 * 3); return srnd(); }
u64 tisp_msca_chx_en(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(28, a0, a1, a2); return srnd(); }
u64 lib_tisp_msca_chx_stream_on(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(29, a0, 0, 0); return srnd(); }
u64 lib_tisp_msca_chx_stream_off(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(30, a0, 0, 0); return srnd(); }
/* phase 6 callees */
static u64 op_video8(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 *m = T32O_HOST(a2);

	(void)a3;
	L(31, a0, a1, m[0] ^ ((u32 *)T32O_HOST(m[1]))[1] * 3 ^ m[2] * 5 ^ m[3] * 7);
	return srnd();
}
static u64 sen_ioctl(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 *m = T32O_HOST(a2);

	(void)a3;
	L(32, a0, a1, m[0] ^ m[1] * 3);
	return srnd();
}
u64 lib_tisp_restart(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(33, 0, 0, 0); return srnd(); }
u64 lib_tisp_refresh_para(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(34, hash(a0, 20), 0, 0); return srnd(); }
u64 lib_tisp_set_csccr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(35, a0, a1, 0); return srnd(); }
u64 lib_tisp_set_wdr_setting(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(36, hash(a0, 20), 0, 0); return srnd(); }
u64 lib_tisp_vic_ctrl(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(37, a0, a1, 0); return srnd(); }
u64 lib_tisp_get_ipc_output_pos(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(38, 0, 0, 0); return k_pos ? k_pos-- : 0; }
u64 private_msleep(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(39, a0, 0, 0); return 0; }

static void o_pr(u32 l, u32 f, u32 fn, u32 line) { isp_printf(l, f, fn, line, 0); }
static void o_pr5(u32 l, u32 f, u32 fn, u32 line, u32 a) { isp_printf(l, f, fn, line, a); }
static u32 o_call(u32 fn, u32 a0, u32 a1, u32 a2)
{
	return (u32)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, 0);
}
static u32 o_cf(u32 to, u32 from, u32 n) { return (u32)private_copy_from_user(to, from, n, 0); }
static void o_by(u32 p, u32 c) { mbus_to_bayer_write(p, c, 0, 0); }
static u32 o_tk(u32 sd) { return (u32)tx_isp_core_oneshot_tick_isra_18(sd, 0, 0, 0); }
static void o_aed(u32 h) { tisp_ae_algo_deinit(h, 0, 0, 0); }
static void o_aeh(u32 h, u32 b) { tisp_ae_algo_handle(h, b, 0, 0); }
static void o_awi(u32 h, u32 on, u32 p) { tisp_awb_algo_init(h, on, p, 0); }
static void o_awh(u32 h, u32 b) { tisp_awb_algo_handle(h, b, 0, 0); }
static void o_lk(u32 l, u32 *f) { __private_spin_lock_irqsave(l, T32O_PTR(f), 0, 0); }
static void o_ul(u32 l, u32 f) { private_spin_unlock_irqrestore(l, f, 0, 0); }
static void o_aov(u32 p) { lib_tisp_aov_wake_up(p, 0, 0, 0); }
static void o_son(void) { lib_tisp_stream_on(0, 0, 0, 0); }
static u32 o_dt(u32 s, u32 ms) { return (u32)down_timeout(s, ms, 0, 0); }
static void o_cr(u32 p, u32 on) { tisp_core_reset(p, on, 0, 0); }
static void o_aei(u32 p, u32 on, u32 a) { tisp_ae_algo_init(p, on, a, 0); }
static u32 o_ex(u32 v, u32 i, u32 o) { return (u32)tisp_math_exp2(v, i, o, 0); }
static u32 o_aew(u32 p) { return (u32)tisp_ae_algo_waitdone(p, 0, 0, 0); }
static u32 o_aeg(u32 p) { return (u32)tisp_ae_algo_getinfo(p, 0, 0, 0); }
static u32 o_aww(u32 h) { return (u32)tisp_awb_algo_waitdone(h, 0, 0, 0); }
static u32 o_awg(u32 b) { return (u32)tisp_awb_algo_getinfo(b, 0, 0, 0); }
static u32 o_ct(u32 t, u32 f, u32 n) { return (u32)private_copy_to_user(t, f, n, 0); }
static void o_fc(void) { tisp_msca_addr_fifo_clear_all(0, 0, 0, 0); }
static void o_fw(u32 c, u32 s, u32 a, u32 b) { tisp_msca_addr_fifo_write(c, s, a, b); }
static void o_ce(u32 c, u32 s, u32 on) { tisp_msca_chx_en(c, s, on, 0); }
static void o_son2(u32 u) { lib_tisp_msca_chx_stream_on(u, 0, 0, 0); }
static void o_sof(u32 u) { lib_tisp_msca_chx_stream_off(u, 0, 0, 0); }
static void o_rs(void) { lib_tisp_restart(0, 0, 0, 0); }
static void o_rp(u32 b) { lib_tisp_refresh_para(b, 0, 0, 0); }
static void o_cc(u32 p, u32 v) { lib_tisp_set_csccr(p, v, 0, 0); }
static void o_wd(u32 b) { lib_tisp_set_wdr_setting(b, 0, 0, 0); }
static void o_vc(u32 v, u32 x) { lib_tisp_vic_ctrl(v, x, 0, 0); }
static u32 o_ip(void) { return (u32)lib_tisp_get_ipc_output_pos(0, 0, 0, 0); }
static void o_ms(u32 ms) { private_msleep(ms, 0, 0, 0); }
static struct t32_io_ops ops = { o_pr, o_call, 0, 0, o_cf, o_by, o_tk, o_aed, o_aeh, o_awi, o_awh, 0, 0, 0, 0, 0,
				 o_lk, o_ul, o_aov, o_son, o_dt, o_cr, t32_data_f3354, 0, 0, 0, 0, 0,
				 o_aei, o_ex, o_aew, o_aeg, o_aww, o_awg, o_ct, 0, 0, 0, 0, 0,
				 o_fc, o_fw, o_ce, o_son2, o_sof,
				 o_pr5, o_rs, o_rp, o_cc, o_wd, o_vc, o_ip, o_ms, 0, 0, 0, 0, 0 };

static u32 res(void)
{
	u32 k = pick(8);

	return k < 3 ? 0 : k < 6 ? T32_IO_NOCMD : k == 6 ? (u32)-22 : rnd();
}
/* an ops pointer block at a: ops[0] / ops[3] with the three functions, each NULL now and then */
static void opsblk(u32 a, u32 blk)
{
	P32(a, pick(6) ? blk : 0);
	P32(a + 12, pick(6) ? blk + 64 : 0);
	P32(blk + 4, pick(5) ? T32O_PTR(op_core) : 0);
	P32(blk + 32, pick(5) ? T32O_PTR(op_ioctl) : 0);
	P32(blk + 64 + 4, pick(5) ? T32O_PTR(op_video) : 0);
	P32(blk + 64 + 8, pick(5) ? T32O_PTR(op_video8) : 0);
}
static void build(u32 seed)
{
	static const u8 bc[] = { 0, 1, 2, 4, 7, 8, 10, 12, 13, 17, 18, 19, 28, 29, 9, 9, 16, 16, 3, 3, 3, 5, 5, 6, 6, 6, 14, 14, 15, 15, 20, 20, 22, 22, 22, 26, 26, 26, 11, 11, 11, 11, 30, 30, 30, 30, 30, 30, 30, 30, 21, 23, 23, 23, 24, 25, 27, 27, 27 };
	u32 save = rs, i, s, dense;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4)
		P32(W + i, rnd());
	SD = pick(16) ? W + 0x100 : 0;
	ARG = pick(8) ? W + 0x40 : rnd();
	i = pick(8);
	CMD = i < 6 ? T32_IO_CMD0 + bc[pick(sizeof(bc))] : i == 6 ? T32_IO_CMD0 + 31 + pick(4) : rnd();
	if (!SD)
		CMD = T32_IO_CMD0 + pick(2);
	i = CMD - T32_IO_CMD0;
	if (i == 11 || i >= 20)	/* arg is dereferenced */
		ARG = W + 0x40;
	for (i = 0; i < 8; i++)
		rv[i] = res();
	nrv = 0;
	cf_fail = pick(6) ? 0 : 1 + pick(64);
	if (ARG == W + 0x40) {
		P32(ARG, pick(4) ? T32_IO_MAGIC : rnd() | 1);
		P32(ARG + 8, pick(2) ? 1 : pick(3));
	}
	PRIV = W + 0x2000;
	P32(W + 0x100 + T32_IS_PRIV, PRIV);
	P32(PRIV + T32_CI_SATTR, pick(6));
	for (i = 0; i < 5; i++)
		P32(PRIV + T32_CI_PIPE_ON + T32_CI_PIPE_SIZE * i, pick(3) ? pick(2) : rnd());
	for (i = 0; i < 4; i++)
		P32(PRIV + T32_CI_STATE + 4 * i, pick(8) ? 2 + pick(3) : pick(2) ? pick(2) : rnd());
	P32(PRIV + T32_CI_STREAMING, pick(2) ? 0 : pick(2) ? 1 : rnd());
	P32(PRIV + T32_CI_USERS_ON, pick(3) ? pick(2) : rnd());
	P32(PRIV + T32_CI_USERS, W + 0x4000);
	P32(PRIV + T32_CI_NUSERS, pick(5));
	t32_data_f3354[0] = rnd();
	i = pick(6);
	r_dt = i < 2 ? 0 : i == 2 ? T32_IO_TIMEDOUT : i == 3 ? T32_IO_NOCMD : rnd();
	i = CMD - T32_IO_CMD0;
	if ((i == 3 || i == 5) && !pick(4))	/* no private block: NULL or an error pointer */
		P32(W + 0x100 + T32_IS_PRIV, pick(2) ? 0 : 0xfffff001u + pick(4095));
	if (i == 6)
		ARG = pick(3) ? pick(4) : rnd();
	if (i == 20 || i == 22)	/* pipe 0..3 */
		P32(ARG + 4, pick(4));
	for (i = 0; i < 4; i++)
		P32(PRIV + T32_CI_SENSOR + T32_CI_PIPE_SIZE * i, W + 0x7000 + 0x100 * i);
	i = pick(6);
	r_wd = i < 3 ? 0 : i == 3 ? T32_IO_NOCMD : rnd();
	r_gi = pick(5) ? W + 0x6000 : 0;
	ct_fail[0] = pick(5) ? 0 : 1 + pick(36);
	ct_fail[1] = pick(5) ? 0 : 1 + pick(36);
	nct = 0;
	for (i = 0; i < 4; i++) {	/* users: enable, active / queued buffer lists */
		u32 u = W + 0x4000 + T32_CI_USER_SIZE * i, h, k, nd, n, prev;

		P32(u + T32_CU_ON, pick(4) ? pick(2) : rnd());
		for (h = 0; h < 2; h++) {
			u32 head = u + (h ? T32_CU_PEND : T32_CU_ACT);

			n = h ? pick(4) : pick(8);
			prev = head;
			for (k = 0; k < n; k++) {
				nd = W + 0x9000 + 0x100 * i + 0x80 * h + 16 * k;
				P32(prev, nd);
				P32(nd + 4, prev);
				prev = nd;
			}
			P32(prev, head);
			P32(head + 4, prev);
			if (h && !pick(8))
				P32(head, 0);
		}
	}
	P32(W + 0x100 + T32_IO_SD_SENSOR, pick(4) ? T32O_PTR(sen_ioctl) : 0);
	k_pos = pick(3) ? pick(4) : 1000;
	if (CMD == T32_IO_CMD0 + 30) {
		u32 pipe = pick(4), mode;

		P32(ARG + 8, pick(3) ? pick(2) : rnd());
		mode = *(u32 *)T32O_HOST(ARG + 8) == 1;
		P32(ARG + 12, pick(3) ? pick(2) : rnd());
		P32(ARG + 16, (pick(2) ? 0 : pick(256)) | pipe << 8 | (rnd() & 0xffff0000u));
		P32(PRIV + T32_CI_BUS + 4 * pipe, pick(2) ? mode : pick(2) ? !mode : pick(3) ? T32_IO_WDR : rnd());
		P32(PRIV + T32_CI_SATTR, pick(3) ? 1 : pick(6));
		if (!pick(5))
			P32(PRIV + T32_CI_SENSOR + T32_CI_PIPE_SIZE * pipe, 0);
	}
	if (CMD == T32_IO_CMD0 + 11)
		P32(ARG + 4, pick(3) ? 1 : pick(3));
	P32(W + 0x100 + T32_IO_SD_OPS, W + 0x600);
	opsblk(W + 0x600, W + 0x700);
	dense = pick(4);
	for (i = 0; i < T32_IO_NSUBDEV; i++) {
		s = W + 0x1000 + 0x200 * i;
		P32(W + 0x100 + T32_IO_SD_SUBDEV + 4 * i, pick(4) <= dense ? s : 0);
		P32(s + T32_IO_SD_OPS, s + 0x180);
		opsblk(s + 0x180, s + 0x40);
	}
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}
static u64 ours(u32 sd, u32 cmd, u32 arg, u32 a3)
{
	(void)a3;
	return t32_ispcore_core_ops_ioctl(&ops, sd, cmd, arg);
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	ops.fmt_err = T32O_PTR(t32_data_cd71c);
	ops.func = T32O_PTR(t32_data_c34a0);
	ops.fmt_copy = T32O_PTR(t32_data_cd550);
	ops.fmt_magic = T32O_PTR(t32_data_cd570);
	ops.fmt_inval = T32O_PTR(t32_data_cd604);
	ops.func_ae = T32O_PTR(t32_data_c342c);
	ops.func_awb = T32O_PTR(t32_data_c33ec);
	ops.fmt_nodev = T32O_PTR(t32_data_cd4ec);
	ops.fmt_on = T32O_PTR(t32_data_cd504);
	ops.fmt_timeout = T32O_PTR(t32_data_cd524);
	ops.func_on = T32O_PTR(t32_data_c3488);
	ops.func_wait = T32O_PTR(t32_data_c346c);
	ops.fmt_ae_wait = T32O_PTR(t32_data_cd5a4);
	ops.fmt_awb_wait = T32O_PTR(t32_data_cd5e4);
	ops.fmt_copy_to = T32O_PTR(t32_data_cd5c4);
	ops.func_aeinfo = T32O_PTR(t32_data_c344c);
	ops.func_awbinfo = T32O_PTR(t32_data_c340c);
	ops.fmt_mode = T32O_PTR(t32_data_cd638);
	ops.fmt_nosensor = T32O_PTR(t32_data_cd6a8);
	ops.fmt_ipc = T32O_PTR(t32_data_cd6fc);
	ops.func_mode = T32O_PTR(t32_data_c33b0);
	ops.func_ipc = T32O_PTR(t32_data_c33d0);

	for (it = 0; it < 40000; it++) {
		u32 seed = rnd(), r, rr;

		build(seed);
		r = (u32)t32o_call((t32o_fn)(void (*)(void))ours, SD, CMD, ARG, 0, 0);
		L(18, t32_data_f3354[0], 0, 0);
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (u32)t32o_call((t32o_fn)(void (*)(void))ispcore_core_ops_ioctl, SD, CMD, ARG, 0, 0);
		L(18, t32_data_f3354[0], 0, 0);
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispioctl_oracle: MISMATCH seed %08x cmd %x sd %x: %x/%x log %u/%u\n", seed, CMD, SD,
				       r, rr, lg_ours.n, lg.n);
			if (getenv("T32O_DBG") && failed < 6) {
				unsigned int j;

				for (j = 0; j < MAXL && j < lg.n; j++)
					if (memcmp(lg.e[j], lg_ours.e[j], 16))
						printf("  log %u: id %u/%u %x/%x %x/%x %x/%x\n", j, lg_ours.e[j][0], lg.e[j][0],
						       lg_ours.e[j][1], lg.e[j][1], lg_ours.e[j][2], lg.e[j][2], lg_ours.e[j][3], lg.e[j][3]);
			}
		}
	}
	printf("t32_ispioctl_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
