// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable tuning device (driver/t32/tx_isp_t32_isptun.h) against the vendor
 * functions isp_core_tuning_init, isp_core_tuning_event, isp_core_tunning_open / _release /
 * _default_ioctl / _unlocked_ioctl / _unlocked_ioctl_fast, isp_tunning_poll / _read / _write,
 * isp_frame_done_wakeup and isp_frame_done_wait from Matt Davis' mechanical MIPS-to-C recovery
 * (ref/, PR #15 @ 2e2e06bc, built for the host with env.c; isp_tunning_read with the
 * fix_op5_read.sh ABI model).
 *
 * Each case builds a random world (64 KiB block: file -> private -> device -> tuning block, core
 * subdev, user buffer, poll table) twice at the same address and runs one function on each side.
 * Every kernel call is a recorder stub with scripted results (copy faults, ctrl returns, wait
 * results, wakeups during the wait, kmalloc failure); compared are the return value, the call log
 * (kernel-stack buffers by content), every byte of the world and of the vendor globals.
 * private_raw_mutex_init (an env.c no-op) is not observable.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_isptun.h"

u64 isp_core_tuning_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_tuning_event(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_tunning_open(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_tunning_release(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_tunning_default_ioctl(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_tunning_unlocked_ioctl(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_tunning_unlocked_ioctl_fast(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_tunning_poll(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_tunning_read(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_tunning_write(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_frame_done_wakeup(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_frame_done_wait(u32 a0, u32 a1, u32 a2, u32 a3);
u64 printk(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_complete(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_raw_mutex_init(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_f3310[1], t32_data_cf12c[2], t32_data_cf134[3], t32_data_cf0c0[1];
extern u8 t32o_op5_dad[0x40];
extern u32 t32o_op5_fd[8];

#define WSZ	0x10000u
#define MAXL	96

static u32 rs = 0x7e57a11u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
/* script generator of the stubs: reseeded identically for both sides */
static u32 ss;
static u32 srnd(void)
{
	ss ^= ss << 13; ss ^= ss >> 17; ss ^= ss << 5;
	return ss;
}

static u8 *world;
static u32 W;
struct lg {
	unsigned int n;
	u32 e[MAXL][6];
};
static struct lg lg, lg_ours;
static int printf_line;		/* the case's isp_printf passes a line in a3 */
static u32 fault_pct;		/* copy fault probability (%) */
static u32 wake_pct;		/* a wakeup during the wait (%) */

static void L(u32 id, u32 a, u32 b, u32 c, u32 d, u32 e)
{
	if (lg.n < MAXL) {
		u32 *p = lg.e[lg.n];

		p[0] = id; p[1] = a; p[2] = b; p[3] = c; p[4] = d; p[5] = e;
	}
	lg.n++;
}
static u32 W32(u32 a) { u32 v; memcpy(&v, T32O_HOST(a), 4); return v; }

u64 private_copy_from_user(u32 dst, u32 src, u32 n, u32 a3)
{
	int f = srnd() % 100 < fault_pct;

	(void)a3;
	L(1, src - W < WSZ ? src : 0, n, f, W32(src), W32(src + 4) ^ (n > 8 ? W32(src + 8) * 3 + W32(src + 12) : 0));	/* a kernel-stack source (fast path) by content only */
	if (f)
		return n;
	memcpy(T32O_HOST(dst), T32O_HOST(src), n);
	return 0;
}
u64 private_copy_to_user(u32 dst, u32 src, u32 n, u32 a3)
{
	int f = srnd() % 100 < fault_pct;

	(void)a3;
	L(2, dst - W < WSZ ? dst : 0, n, f, W32(src), W32(src + 4) ^ (n > 8 ? W32(src + 8) * 3 + W32(src + 12) : 0));
	if (f)
		return n;
	memcpy(T32O_HOST(dst), T32O_HOST(src), n);
	return 0;
}
u64 __copy_user(u32 dst, u32 src, u32 n, u32 a3)
{
	u32 left = srnd() % 3 ? 0 : srnd() % (n + 1);

	(void)a3;
	L(3, dst, src, n, left, 0);
	memcpy(T32O_HOST(dst), T32O_HOST(src), n - left);
	return left;
}
static u32 ctrl_ret(void)
{
	u32 k = srnd() % 8;

	return k < 3 ? 0 : k < 5 ? (u32)T32_ENOIOCTLCMD : k == 5 ? (u32)-22 : srnd();
}
u64 tx_isp_core_ops_s_ctrl(u32 id, u32 tun, u32 val, u32 a3)
{
	(void)a3;
	L(4, id, tun, W32(val), W32(val + 4), 0);
	return ctrl_ret();
}
u64 tx_isp_core_ops_g_ctrl(u32 id, u32 tun, u32 val, u32 a3)
{
	u32 a = srnd(), b = srnd();

	(void)a3;
	L(5, id, tun, W32(val), W32(val + 4), 0);
	memcpy(T32O_HOST(val), &a, 4);
	memcpy(T32O_HOST(val + 4), &b, 4);
	return ctrl_ret();
}
u64 tisp_enable_tuning(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(6, 0, 0, 0, 0, 0); return 0; }
u64 tisp_disable_tuning(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(7, 0, 0, 0, 0, 0); return 0; }
u64 tisp_day_or_night_s_ctrl(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(8, a0, a1, a2, 0, 0); return srnd(); }
u64 private_init_completion(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(9, a0, 0, 0, 0, 0); return 0; }
u64 private_spin_lock_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(10, a0, 0, 0, 0, 0); return 0; }
static u8 *kpool;
u64 private_kmalloc(u32 n, u32 gfp, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	L(11, n, gfp, 0, 0, 0);
	return srnd() % 8 ? T32O_PTR(kpool) : 0;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2;
	L(12, a0, printf_line ? a3 : 0, 0, 0, 0);
	return 0;
}
u64 __wake_up(u32 wq, u32 mode, u32 nr, u32 key) { L(13, wq, mode, nr, key, 0); return 0; }
static u32 *wait_flag;
static void maybe_wake(void)
{
	if (wait_flag && srnd() % 100 < wake_pct)
		*wait_flag = srnd() % 4 ? 1 : 2;
}
static u32 entry_ok(u32 e)
{
	return W32(e + 12) == e + 12 && W32(e + 16) == e + 12;
}
u64 prepare_to_wait_event(u32 wq, u32 entry, u32 state, u32 a3)
{
	u32 r = srnd() % 8 ? 0 : (u32)T32_ERESTARTSYS;

	(void)a3;
	L(14, wq, state, W32(entry), W32(entry + 4) | W32(entry + 8), entry_ok(entry));
	maybe_wake();
	return r;
}
u64 finish_wait(u32 wq, u32 entry, u32 a2, u32 a3) { (void)a2; (void)a3; L(15, wq, entry_ok(entry), 0, 0, 0); return 0; }
u64 schedule_timeout(u32 t, u32 a1, u32 a2, u32 a3)
{
	u32 k = srnd() % 4, r = k == 0 ? 0 : k == 1 ? t / 2 : k == 2 ? t - 1 : t;

	(void)a1; (void)a2; (void)a3;
	L(16, t, 0, 0, 0, 0);
	maybe_wake();
	return r;
}
static u64 qproc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(17, a0, a1, a2, 0, 0); return srnd(); }

/* fake thread_info of the read reference (fix_op5_read.sh): +24 addr_limit */
u8 t32o_op5_ti[32] __attribute__((aligned(4)));

/* ---- our hooks: call the same recorders ---- */
static int o_call(u32 fn, u32 a0, u32 a1, u32 a2) { return (int)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, 0); }
static u32 o_cfu(void *d, u32 s, u32 n) { return (u32)private_copy_from_user(T32O_PTR(d), s, n, 0); }
static u32 o_ctu(u32 d, const void *s, u32 n) { return (u32)private_copy_to_user(d, T32O_PTR(s), n, 0); }
static u32 o_cu(u32 d, u32 s, u32 n) { return (u32)__copy_user(d, s, n, 0); }
static u32 o_seg(void) { return W32(T32O_PTR(t32o_op5_ti) + 24); }
static int o_s(u32 id, u32 t, void *v) { return (int)tx_isp_core_ops_s_ctrl(id, t, T32O_PTR(v), 0); }
static int o_g(u32 id, u32 t, void *v) { return (int)tx_isp_core_ops_g_ctrl(id, t, T32O_PTR(v), 0); }
static void o_en(void) { tisp_enable_tuning(0, 0, 0, 0); }
static void o_dis(void) { tisp_disable_tuning(0, 0, 0, 0); }
static void o_log(u32 lvl, u32 line)
{
	if (lvl == 0 && line == 690)
		printk(0, 0, 690, 0);
	else
		isp_printf(lvl, 0, 0, line);
}
static void o_dn(u32 a, u32 b, u32 c) { tisp_day_or_night_s_ctrl(a, b, c, 0); }
static void o_cmp(u32 c) { private_complete(c, 0, 0, 0); }
static void o_ic(u32 c) { private_init_completion(c, 0, 0, 0); }
static void *o_km(u32 n, u32 g) { return T32O_HOST((u32)private_kmalloc(n, g, 0, 0)); }
static void o_sl(u32 l) { private_spin_lock_init(l, 0, 0, 0); }
static void o_mi(u32 m) { private_raw_mutex_init(m, 0, 0, 0); }
static void o_wk(u32 wq, u32 m, u32 nr, u32 k) { __wake_up(wq, m, nr, k); }
static int o_pw(u32 wq, u32 *e, u32 st) { return (int)prepare_to_wait_event(wq, T32O_PTR(e), st, 0); }
static void o_fw(u32 wq, u32 *e) { finish_wait(wq, T32O_PTR(e), 0, 0); }
static int o_st(int t) { return (int)schedule_timeout((u32)t, 0, 0, 0); }
static const struct t32_tun_ops ops = {
	o_call, o_cfu, o_ctu, o_cu, o_seg, o_s, o_g, o_en, o_dis, o_log, o_dn, o_cmp, o_ic, o_km,
	o_sl, o_mi, o_wk, o_pw, o_fw, o_st,
};
static struct t32_tun_glob g;

/* world layout */
#define FILE_	0x100u
#define PRIV_	0x400u
#define WAIT_	0x600u
#define DEV_	0x800u
#define TUN_	0x2000u
#define CORE_	0x3000u
#define CDEV_	0x3400u
#define UBUF_	0x8000u
#define KPOOL_	0xc000u
static u8 *snap;

static u8 snap_dad[0x40];
static u32 snap_fd[8], snap_ti[8];
static u32 snap_cmpn, snap_cmpa, snap_prn;

static void build(u32 seed)
{
	u32 save = rs, i, st;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	for (i = 0; i < sizeof(t32o_op5_dad); i++)
		t32o_op5_dad[i] = rnd();
	t32o_op5_dad[0] = rnd() % 3 ? (u8)rnd() : 0;
	for (i = 0; i < 8; i++)
		t32o_op5_fd[i] = rnd() % 4 ? rnd() : 0xffffffffu;
	t32o_op5_fd[0] = rnd() % 2;
	t32o_op5_fd[1] = rnd() % 2;
	memset(t32o_op5_ti, 0, sizeof(t32o_op5_ti));
	i = rnd() % 4 ? 0x80000000u : rnd() % 2 ? 0 : rnd();
	memcpy(t32o_op5_ti + 24, &i, 4);
	t32_ic_w32(W + FILE_, T32_FILE_PRIV, W + PRIV_);
	t32_ic_w32(W + FILE_, T32_FILE_FLAGS, rnd() % 3 ? rnd() & ~T32_O_NONBLOCK : rnd());
	t32_ic_w32(W + PRIV_, T32_MISC_DEV, W + DEV_);
	t32_ic_w32(W + DEV_, T32_DEV_TUNING, W + TUN_);
	t32_ic_w32(W + CORE_, T32_SD_PRIV, W + CDEV_);
	t32_ic_w32(W + CDEV_, T32_DEV_TUNING, W + TUN_);
	st = rnd() % 5;
	t32_ic_w32(W + TUN_, T32_TUN_STATE, st < 4 ? st : rnd());
	t32_ic_w32(W + WAIT_, 0, rnd() % 4 ? T32O_PTR(qproc) : 0);
	kpool = world + KPOOL_;
	t32_data_f3310[0] = W + CORE_;
	g.rd_flag = t32o_op5_dad;
	g.rd_wq = T32O_PTR(t32o_op5_dad) + 4;
	g.fd_flag = &t32o_op5_fd[0];
	g.fd_flag0 = &t32o_op5_fd[1];
	g.fd_cnt = &t32o_op5_fd[2];
	g.fd_wq = T32O_PTR(t32_data_cf12c);
	g.fd_wq0 = T32O_PTR(t32_data_cf134);
	g.core = W + CORE_;
	memset(&lg, 0, sizeof(lg));
	t32o_complete_n = t32o_complete_arg = 0;
	ss = seed ^ 0x9e3779b9u;
	if (!ss)
		ss = 1;
	wait_flag = 0;
	rs = save;
}

static void take_ours(void)
{
	memcpy(snap, world, WSZ);
	memcpy(snap_dad, t32o_op5_dad, sizeof(snap_dad));
	memcpy(snap_fd, t32o_op5_fd, sizeof(snap_fd));
	memcpy(snap_ti, t32o_op5_ti, sizeof(snap_ti));
	lg_ours = lg;
	snap_cmpn = t32o_complete_n;
	snap_cmpa = t32o_complete_arg;
	snap_prn = t32o_log.printfs;
}

static unsigned int compared, failed;
static void same(const char *what, u32 seed, int r_ours, int r_ref)
{
	unsigned int n = lg.n < MAXL ? lg.n : MAXL;

	compared++;
	if (r_ours == r_ref && lg.n == lg_ours.n && !memcmp(lg.e, lg_ours.e, n * sizeof(lg.e[0])) &&
	    !memcmp(snap, world, WSZ) && !memcmp(snap_dad, t32o_op5_dad, sizeof(snap_dad)) &&
	    !memcmp(snap_fd, t32o_op5_fd, sizeof(snap_fd)) && !memcmp(snap_ti, t32o_op5_ti, sizeof(snap_ti)) &&
	    snap_cmpn == t32o_complete_n && snap_cmpa == t32o_complete_arg && snap_prn == t32o_log.printfs)
		return;
	if (failed++ < 8)
		printf("t32_isptun_oracle: MISMATCH %s seed %08x: ret %d/%d log %u/%u\n",
		       what, seed, r_ours, r_ref, lg_ours.n, lg.n);
}

/* a copy the access check lets through must stay inside the world: else take the O_NONBLOCK exit */
static void rd_guard(u32 buf, u32 len)
{
	if (!(((buf + len) | buf | len) & o_seg()) && (len > 32 || buf - W - UBUF_ > 0x100))
		t32_ic_w32(W + FILE_, T32_FILE_FLAGS, T32_O_NONBLOCK);
}

/* ours runs on the low stack too (t32o_call): its kernel-stack buffers must have 32-bit addresses */
static u64 ours_tramp(u32 k, u32 a, u32 b, u32 c, u32 d)
{
	(void)d;
	switch (k) {
	case 1: return (u32)t32_isp_frame_done_wait(&ops, &g, a, (int)b, (u32 *)T32O_HOST(c));
	case 2: return (u32)t32_isp_core_tunning_default_ioctl(&ops, a, b, c);
	case 3: return (u32)t32_isp_core_tunning_unlocked_ioctl(&ops, &g, a, b, c);
	default: return (u32)t32_isp_core_tunning_unlocked_ioctl_fast(&ops, &g, (const u32 *)T32O_HOST(a));
	}
}
#define OURS(k, a, b, c)	((int)(u32)t32o_call((t32o_fn)(void (*)(void))ours_tramp, (k), (a), (b), (c), 0))

#define REF(fn, a0, a1, a2, a3)	((int)(u32)t32o_call((t32o_fn)(void (*)(void))(fn), (a0), (a1), (a2), (a3), 0))

int main(void)
{
	unsigned int it;

	snap = malloc(WSZ);
	if (!snap)
		return 1;
	for (it = 0; it < 8000; it++) {
		static u32 ctl[4];
		u32 seed = rnd(), a, b, c, ev, ch, cv[4], out[2] = { 0 };
		int r, t;

		fault_pct = it % 3 ? 10 : 50;
		wake_pct = it % 2 ? 30 : 0;
		printf_line = 0;

		/* isp_core_tuning_init */
		a = rnd();
		build(seed);
		r = (int)t32_isp_core_tuning_init(&ops, a, T32O_PTR(t32_data_cf0c0), T32O_PTR(isp_core_tuning_event));
		take_ours();
		build(seed);
		same("tuning_init", seed, r, REF(isp_core_tuning_init, a, rnd(), rnd(), rnd()));

		/* isp_core_tuning_event: data word 1 = channel (3 entries, wrap of ch << 5 kept in range) */
		ev = rnd() % 6 ? T32_EV_TUN_OPEN + rnd() % 5 : rnd() % 2 ? T32_EV_TUN_OPEN - 1 : rnd();
		ch = rnd() % 4 ? rnd() % 3 : (rnd() % 3) | (rnd() << 27);
		build(seed);
		t32_ic_w32(W + UBUF_, 4, ev == T32_EV_FRAME_DONE ? ch % 3 : ch);
		r = t32_isp_core_tuning_event(&ops, &g, W + TUN_, ev, W + UBUF_);
		take_ours();
		build(seed);
		t32_ic_w32(W + UBUF_, 4, ev == T32_EV_FRAME_DONE ? ch % 3 : ch);
		same("tuning_event", seed, r, REF(isp_core_tuning_event, W + TUN_, ev, W + UBUF_, rnd()));

		/* isp_frame_done_wakeup */
		build(seed);
		t32_ic_w32(W + UBUF_, 4, ch % 3);
		t32_isp_frame_done_wakeup(&ops, &g, W + UBUF_);
		take_ours();
		build(seed);
		t32_ic_w32(W + UBUF_, 4, ch % 3);
		REF(isp_frame_done_wakeup, W + UBUF_, rnd(), rnd(), rnd());
		same("frame_done_wakeup", seed, 0, 0);

		/* isp_frame_done_wait */
		ch = rnd() % 3;
		t = rnd() % 4 == 0 ? 0 : rnd() % 3 ? (int)(rnd() % 20) + 1 : (int)rnd();
		build(seed);
		wait_flag = ch ? g.fd_flag : g.fd_flag0;
		r = OURS(1, ch, (u32)t, W + UBUF_);
		take_ours();
		build(seed);
		wait_flag = ch ? g.fd_flag : g.fd_flag0;
		same("frame_done_wait", seed, r, REF(isp_frame_done_wait, ch, (u32)t, W + UBUF_, rnd()));
		(void)out;

		/* open / release */
		build(seed);
		r = t32_isp_core_tunning_open(&ops, W + FILE_);
		take_ours();
		build(seed);
		same("tunning_open", seed, r, REF(isp_core_tunning_open, rnd(), W + FILE_, rnd(), rnd()));
		printf_line = 1;
		build(seed);
		r = t32_isp_core_tunning_release(&ops, W + FILE_);
		take_ours();
		build(seed);
		same("tunning_release", seed, r, REF(isp_core_tunning_release, rnd(), W + FILE_, rnd(), rnd()));
		printf_line = 0;

		/* default / unlocked / fast ioctl */
		b = rnd() % 4;
		c = b == 0 ? T32_TUN_CMD_GET : b == 1 ? T32_TUN_CMD_PUT : b == 2 ? T32_TUN_CMD_CTRL : rnd();
		a = rnd() % 3 ? 0 : 1;	/* ctl[1]: 0 = set */
		b = rnd();
		build(seed);
		t32_ic_w32(W + UBUF_, 4, a ? b | 1 : 0);
		r = OURS(2, W + TUN_, c, W + UBUF_);
		take_ours();
		build(seed);
		t32_ic_w32(W + UBUF_, 4, a ? b | 1 : 0);
		same("default_ioctl", seed, r, REF(isp_core_tunning_default_ioctl, W + TUN_, c, W + UBUF_, rnd()));
		build(seed);
		t32_ic_w32(W + UBUF_, 4, a ? b | 1 : 0);
		r = OURS(3, it & 4 ? 0 : W + FILE_, c, W + UBUF_);
		take_ours();
		build(seed);
		t32_ic_w32(W + UBUF_, 4, a ? b | 1 : 0);
		same("unlocked_ioctl", seed, r, REF(isp_core_tunning_unlocked_ioctl, it & 4 ? 0 : W + FILE_, c, W + UBUF_, rnd()));
		cv[0] = rnd(); cv[1] = a ? rnd() : 0; cv[2] = rnd(); cv[3] = rnd();
		memcpy(ctl, cv, sizeof(cv));	/* ours may write the control back into ctl: the vendor gets cv */
		build(seed);
		r = OURS(4, T32O_PTR(ctl), 0, 0);
		take_ours();
		build(seed);
		same("unlocked_ioctl_fast", seed, r, REF(isp_core_tunning_unlocked_ioctl_fast, cv[0], cv[1], cv[2], cv[3]));

		/* poll / read / write */
		a = rnd() % 4 ? W + WAIT_ : 0;
		build(seed);
		r = (int)t32_isp_tunning_poll(&ops, &g, W + FILE_, a);
		take_ours();
		build(seed);
		same("tunning_poll", seed, r, REF(isp_tunning_poll, W + FILE_, a, rnd(), rnd()));
		b = rnd() % 5 ? rnd() % 33 : rnd();
		c = rnd() % 5 ? W + UBUF_ + (rnd() % 64) : rnd();
		build(seed);
		rd_guard(c, b);
		r = t32_isp_tunning_read(&ops, &g, W + FILE_, c, b);
		take_ours();
		build(seed);
		rd_guard(c, b);
		same("tunning_read", seed, r, REF(isp_tunning_read, W + FILE_, c, b, rnd()));
		build(seed);
		r = t32_isp_tunning_write(&ops, b);
		take_ours();
		build(seed);
		same("tunning_write", seed, r, REF(isp_tunning_write, W + FILE_, c, b, rnd()));
	}
	printf("t32_isptun_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
