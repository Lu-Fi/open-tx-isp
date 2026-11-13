// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ISP core glue part 1 (driver/t32/tx_isp_t32_ispcore.h) against
 * the vendor functions isp_irq_handle, isp_irq_thread_handle, ispcore_sensor_ops_release_all_sensor,
 * ispcore_sensor_ops_ioctl, ispcore_link_setup, ispcore_irq_thread_handle, isp_info_lsc,
 * isp_core_tuning_clear, isp_core_tuning_deinit and ispcore_update_fs_work from Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 *
 * Each case builds a random world (one 64 KiB block: core subdev, private block, 16 sensor slots
 * with ops / sensor-ops tables, recorder functions as table entries, random filler) twice at the
 * same address, runs ours on one and the vendor on the other and compares: the return value,
 * every recorded indirect call (callee, the arguments the callee takes, order), every kfree and
 * every byte of the block.  Callee return values are scripted per call (0, -515, 2, 1, random).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispcore.h"

/*
 * #282 F5: a mutated port that follows a wrong table offset into unmapped memory used to die with SIGSEGV
 * (rc 139), which run.sh could only count as "detected" by crash.  The reference never faults on these inputs
 * (the unmutated oracle passes), so a fault is the port's: report it as a mismatch with exit 1 instead.
 */
#include <signal.h>
#include <unistd.h>
static void t32o_segv(int sig)
{
	static const char m[] = "MISMATCH: the port faulted (wild guest access, wrong table offset); the vendor did not\n";

	(void)sig;
	if (write(2, m, sizeof(m) - 1) < 0)
		_exit(2);
	_exit(1);
}
static void __attribute__((constructor)) t32o_segv_install(void)
{
	signal(SIGSEGV, t32o_segv);
	signal(SIGBUS, t32o_segv);
}

u64 isp_irq_handle(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_irq_thread_handle(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_sensor_ops_release_all_sensor(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_sensor_ops_ioctl(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_tuning_clear(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_core_tuning_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
u64 ispcore_update_fs_work(u32 a0, u32 a1, u32 a2, u32 a3);
int ispcore_link_setup(void);
int ispcore_irq_thread_handle(void);
int isp_info_lsc(void);
extern u32 t32_data_f3310[1];

#define WSZ	0x10000u
#define NREC	4
#define MAXC	64

static u32 rs = 0x51c0de5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* call log of one side */
struct clog {
	unsigned int n, nfree;
	u32 c[MAXC][4];		/* recorder id, a0, a1, a2 */
	u32 freed[8];
};
static struct clog lg;
static u32 rets[MAXC];
static unsigned int arity;	/* arguments the callee of the current case takes */

static u64 rec(u32 id, u32 a0, u32 a1, u32 a2)
{
	u32 r;

	if (lg.n >= MAXC) {
		lg.n++;
		return 0;
	}
	lg.c[lg.n][0] = id;
	lg.c[lg.n][1] = a0;
	lg.c[lg.n][2] = arity > 1 ? a1 : 0;
	lg.c[lg.n][3] = arity > 2 ? a2 : 0;
	r = rets[lg.n++];
	return 0xdead000000000000ull | r;	/* v1 is garbage for the caller */
}
static u64 rec0(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(0, a0, a1, a2); }
static u64 rec1(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(1, a0, a1, a2); }
static u64 rec2(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(2, a0, a1, a2); }
static u64 rec3(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(3, a0, a1, a2); }
static u64 (*const recs[NREC])(u32, u32, u32, u32) = { rec0, rec1, rec2, rec3 };

u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (lg.nfree < 8)
		lg.freed[lg.nfree] = a0;
	lg.nfree++;
	return 0;
}

static int our_call(u32 fn, u32 a0, u32 a1, u32 a2)
{
	return (int)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, 0);
}
static void our_kfree(void *p)
{
	private_kfree(T32_IC_A(p), 0, 0, 0);
}
static const struct t32_ic_ops ops = { our_call, our_kfree };

/* the world: core subdev at +0, private block at +0x1000, sensors at +0x2000 (0x200 each),
 * ops tables at +0x4000 (0x40 each), sensor-ops tables at +0x5000 (0x20 each), core ops at +0x5800,
 * ioctl argument at +0x8000, random filler everywhere else */
static u8 *world;
static u32 W;
static u32 wseed;

static u32 rec_or_null(int pnull)
{
	return (int)(rnd() % 100) < pnull ? 0 : T32_IC_A(recs[rnd() % NREC]);
}

static void build(u32 seed)
{
	u32 save = rs, i, sops, o;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32_IC_A(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	/* private block or none */
	t32_ic_w32(W, T32_SD_PRIV, rnd() % 8 ? W + 0x1000 : 0);
	/* core subdev ops: core ops table with isr / thread entries */
	o = W + 0x4000 + 0x40 * 16;
	t32_ic_w32(W, T32_SD_OPS, o);
	t32_ic_w32(o, T32_OPS_CORE, rnd() % 8 ? W + 0x5800 : 0);
	t32_ic_w32(W + 0x5800, T32_CORE_ISR, rec_or_null(15));
	t32_ic_w32(W + 0x5800, T32_CORE_ITHREAD, rec_or_null(15));
	for (i = 0; i < T32_SD_SENSORS_N; i++) {
		u32 s = W + 0x2000 + 0x200 * i, so = W + 0x4000 + 0x40 * i;

		t32_ic_w32(W, T32_SD_SENSORS + 4 * i, rnd() % 10 < 3 ? 0 : s);
		t32_ic_w32(s, T32_SD_OPS, so);
		sops = rnd() % 10 < 2 ? 0 : W + 0x5000 + 0x20 * i;
		t32_ic_w32(so, T32_OPS_SENSOR, sops);
		if (sops) {
			t32_ic_w32(sops, T32_SENSOR_RELEASE, rec_or_null(20));
			t32_ic_w32(sops, T32_SENSOR_IOCTL, rec_or_null(20));
		}
	}
	for (i = 0; i < MAXC; i++) {
		u32 k = rnd() % 10;

		rets[i] = k < 4 ? 0 : k < 7 ? (u32)T32_ENOIOCTLCMD : k == 7 ? 2 : k == 8 ? 1 : rnd();
	}
	memset(&lg, 0, sizeof(lg));
	wseed = rs;
	rs = save;
}

static u8 *snap_ours;
static struct clog lg_ours;
static unsigned int compared, failed;

static void take_ours(void)
{
	memcpy(snap_ours, world, WSZ);
	lg_ours = lg;
}

static int same(const char *what, u32 seed, int r_ours, int r_ref)
{
	unsigned int i, n = lg.n < MAXC ? lg.n : MAXC;

	compared++;
	if (r_ours == r_ref && lg.n == lg_ours.n && lg.nfree == lg_ours.nfree &&
	    !memcmp(lg.freed, lg_ours.freed, sizeof(lg.freed)) && !memcmp(snap_ours, world, WSZ)) {
		for (i = 0; i < n; i++)
			if (memcmp(lg.c[i], lg_ours.c[i], sizeof(lg.c[i])))
				break;
		if (i == n)
			return 0;
	}
	if (failed++ < 5)
		printf("t32_ispcore_oracle: MISMATCH %s seed %08x: ret %d/%d calls %u/%u frees %u/%u\n",
		       what, seed, r_ours, r_ref, lg_ours.n, lg.n, lg_ours.nfree, lg.nfree);
	return 1;
}

#define REF(fn, a0, a1, a2, a3)	((int)(u32)t32o_call((t32o_fn)(void (*)(void))(fn), (a0), (a1), (a2), (a3), 0))

int main(void)
{
	unsigned int it;

	snap_ours = malloc(WSZ);
	if (!snap_ours)
		return 1;
	if (ispcore_link_setup() != t32_ispcore_link_setup() ||
	    ispcore_irq_thread_handle() != t32_ispcore_irq_thread_handle() ||
	    isp_info_lsc() != t32_isp_info_lsc())
		failed++;
	compared += 3;

	for (it = 0; it < 20000; it++) {
		u32 seed = rnd(), irq, dev, cmd, arg, sd, idx, ch, val, x;
		int r;

		/* isp_irq_handle / isp_irq_thread_handle */
		irq = rnd() % 64;
		dev = it % 50 == 0 ? T32_SD_IRQ_COOKIE : 0;
		arity = 3;
		build(seed);
		if (!dev)
			dev = W + T32_SD_IRQ_COOKIE;
		r = t32_isp_irq_handle(&ops, irq, dev);
		take_ours();
		build(seed);
		same("isp_irq_handle", seed, r, REF(isp_irq_handle, irq, dev, rnd(), rnd()));
		arity = 2;
		build(seed);
		r = t32_isp_irq_thread_handle(&ops, irq, dev);
		take_ours();
		build(seed);
		same("isp_irq_thread_handle", seed, r, REF(isp_irq_thread_handle, irq, dev, rnd(), rnd()));

		/* sensor ops walks */
		arity = 1;
		build(seed);
		r = t32_ispcore_sensor_ops_release_all_sensor(&ops, W);
		take_ours();
		build(seed);
		same("release_all_sensor", seed, r, REF(ispcore_sensor_ops_release_all_sensor, W, rnd(), rnd(), rnd()));
		arity = 3;
		cmd = rnd() % 3 ? T32_SENSOR_CMD_ATTR : rnd() % 2 ? T32_SENSOR_CMD_ATTR + 1 : rnd();
		arg = W + 0x8000 + (rnd() % 0x100) * 4;
		build(seed);
		r = t32_ispcore_sensor_ops_ioctl(&ops, W, cmd, arg);
		take_ours();
		build(seed);
		same("sensor_ops_ioctl", seed, r, REF(ispcore_sensor_ops_ioctl, W, cmd, arg, rnd()));

		/* isp_core_tuning_clear: idx << 5 wraps in 32 bits */
		idx = (rnd() % 0x700) | (it & 1 ? rnd() << 27 : 0);
		build(seed);
		t32_isp_core_tuning_clear(idx, W);
		take_ours();
		build(seed);
		REF(isp_core_tuning_clear, idx, W, rnd(), rnd());
		same("tuning_clear", seed, 0, 0);

		/* isp_core_tuning_deinit */
		sd = it % 4 ? W + (rnd() % 0x1000) : 0;
		build(seed);
		t32_isp_core_tuning_deinit(&ops, sd);
		take_ours();
		build(seed);
		REF(isp_core_tuning_deinit, sd, rnd(), rnd(), rnd());
		same("tuning_deinit", seed, 0, 0);

		/* ispcore_update_fs_work: (ch * 12 + idx) << 3 in 32 bits, priv at +0x1000 */
		x = rnd() % 0x1a00;
		ch = it % 3 ? rnd() % 4 : rnd();
		idx = x - ch * 12;
		val = rnd();
		build(seed);
		t32_ic_w32(W, T32_SD_PRIV, W + 0x1000);
		t32_data_f3310[0] = W;
		r = t32_ispcore_update_fs_work(W, ch, idx, val);
		take_ours();
		build(seed);
		t32_ic_w32(W, T32_SD_PRIV, W + 0x1000);
		t32_data_f3310[0] = W;
		same("update_fs_work", seed, r, REF(ispcore_update_fs_work, ch, idx, val, rnd()));
	}
	(void)wseed;
	printf("t32_ispcore_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
