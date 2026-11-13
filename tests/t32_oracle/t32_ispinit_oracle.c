// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_core_ops_init (driver/t32/tx_isp_t32_ispinit.h) against
 * the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc,
 * built for the host with env.c).  Random worlds built twice (subdev NULL / error / valid,
 * private block NULL / error / valid, enable on / off, pipe 0..4, pipe states 0..4, every media
 * bus code of 0x4ff8..0x5338 and random ones, sensor size in / out of range, 0..4 format table
 * users, reset / lock / sensor ioctl / lib_tisp_init / kthread_run / kthread_stop results,
 * thread words empty / valid / error, tuning / dual / no-bin flags, bin names empty or set,
 * sensor names 0..126 B); compared: return value, the ordered call log of every hook (arguments,
 * printf values, a hash of the 904 B start block given to lib_tisp_init with the bin names
 * compared up to their NUL), the bss words and every byte of the world.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispinit.h"

u64 ispcore_core_ops_init(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_d9014[], t32_data_d9008[], t32_data_d9004[], t32_data_f3354[], t32_data_d8ce0[];
extern u8 t32_data_f334c[], t32_data_f335c[];
extern u32 t32_data_f3358[];

/* format strings / names by address (cd1cc..cd230: bin name formats used by snprintf) */
u32 t32_data_c36ac[1], t32_data_c3678[1], t32_data_cd078[1], t32_data_cd098[1], t32_data_cd0b8[1];
u32 t32_data_cd0f4[1], t32_data_cd13c[1], t32_data_cd178[1], t32_data_cd254[1], t32_data_cd274[1];
u32 t32_data_cd294[1], t32_data_cd2ac[1], t32_data_cd2bc[1], t32_data_cd2dc[1], t32_data_cd300[1];
u32 t32_data_cd310[1], t32_data_cd320[1], t32_data_cd330[1], t32_data_f332c[4];
extern u32 t32_data_cd340[];
char t32_data_cd1cc[16] = "%s-a.bin", t32_data_cd1ec[16] = "b_%s", t32_data_cd20c[16] = "%s/c";
char t32_data_cd230[16] = "d%s.bin";

#define WSZ	0x10000u
#define MAXL	48
static u32 rs = 0x51c3a7u;
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
struct lg { unsigned int n; u32 e[MAXL][4]; u32 bss[12]; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W, SD, REQ, PRIV, ATTR;
static u32 r_reset, r_lib, r_kr[3], r_ks[6], r_cb;
static unsigned int n_kr, n_ks;
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b; lg.e[lg.n][3] = c;
	}
	lg.n++;
}
static void P32(u32 a, u32 v) { memcpy(T32O_HOST(a), &v, 4); }
static u32 hash(const u8 *p, u32 n, u32 h)
{
	u32 i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}
/* the start block with the bin names cut at their NUL (snprintf leaves stack bytes behind it) */
static void canon(u32 a)
{
	u32 i, j;
	u8 *p;

	for (i = 0; i < 4; i++) {
		p = T32O_HOST(a + 132 + 128 * i);
		for (j = 0; j < 128 && p[j]; j++)
			;
		if (j < 128)
			memset(p + j, 0, 128 - j);
	}
}

/* the vendor callees: log, return a seeded value */
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 s16, u32 s20, u32 s24)
{
	u32 f = a1, h;

	t32o_log.printfs++;
	if (f == T32O_PTR(t32_data_cd0f4) || f == T32O_PTR(t32_data_cd274) || f == T32O_PTR(t32_data_cd2bc))
		h = 0;	/* a2 / a3 are leftovers */
	else if (f == T32O_PTR(t32_data_cd098) || f == T32O_PTR(t32_data_cd13c))
		h = a2 ^ a3 * 3 ^ s16 * 5;
	else if (f == T32O_PTR(t32_data_cd0b8))
		h = a2 ^ a3 * 3 ^ s16 * 5 ^ s20 * 7 ^ s24 * 11;
	else
		h = a2 ^ a3 * 3;
	L(1, a0, f, h);
	return srnd();
}
u64 get_isp_memopt(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(2, 0, 0, 0); return srnd(); }
u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(3, a0, 0, 0); return ATTR; }
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	canon(a0);
	L(4, a0, hash(T32O_HOST(a0), T32_CI_ATTR_SIZE, 2166136261u), 0);
	return srnd();
}
u64 private_reset_tx_isp_module(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(5, a0, 0, 0); return r_reset; }
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(6, a0, 0, 0); P32(a1, srnd()); return srnd(); }
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(7, a0, a1, 0); return srnd(); }
static u64 sensor_ioctl(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	L(8, a0, a1, 0);
	P32(a2, r_cb);
	return srnd();
}
u64 lib_tisp_init(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	canon(a1);
	L(9, a0, a1, hash(T32O_HOST(a1), T32_CI_ATTR_SIZE, 2166136261u));
	return r_lib;
}
u64 lib_tisp_deinit(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(10, a0, 0, 0); return srnd(); }
u64 tisp_process_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(11, 0, 0, 0); return srnd(); }
u64 tisp_process_deinit(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(12, a0, 0, 0); return srnd(); }
u64 private_kthread_run(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(13, a0, a1, a2); return r_kr[n_kr++ % 3]; }
u64 private_kthread_stop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(14, a0, 0, 0); return r_ks[n_ks++ % 6]; }
u64 private_init_completion(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(15, a0, 0, 0); return srnd(); }
u64 ispcore_video_s_stream(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(16, a0, a1, 0); return srnd(); }
/* thread functions: only their addresses are used */
u64 pm_core_event_process_thread(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 isp_fw_process(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 1; }
u64 isp_fw_process1(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 2; }
u64 isp_fw_process2(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 3; }
u64 isp_fw_process3(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 4; }

static void o_pr(u32 l, u32 f, u32 a2, u32 a3, u32 s16, u32 s20, u32 s24) { isp_printf(l, f, a2, a3, s16, s20, s24); }
static u32 o_mo(void) { return (u32)get_isp_memopt(0, 0, 0, 0); }
static u32 o_vm(u32 s) { return (u32)private_vmalloc(s, 0, 0, 0); }
static void o_vf(u32 p) { private_vfree(p, 0, 0, 0); }
static u32 o_rs(u32 a) { return (u32)private_reset_tx_isp_module(a, 0, 0, 0); }
static void o_lk(u32 l, u32 *f) { __private_spin_lock_irqsave(l, T32O_PTR(f), 0, 0); }
static void o_ul(u32 l, u32 f) { private_spin_unlock_irqrestore(l, f, 0, 0); }
static u32 o_cl(u32 fn, u32 sd, u32 cmd, u32 *arg)
{
	return (u32)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(sd, cmd, T32O_PTR(arg), 0);
}
static u32 o_li(u32 b, u32 a) { return (u32)lib_tisp_init(b, a, 0, 0); }
static void o_ld(u32 p) { lib_tisp_deinit(p, 0, 0, 0); }
static void o_pi(void) { tisp_process_init(0, 0, 0, 0); }
static void o_pd(u32 p) { tisp_process_deinit(p, 0, 0, 0); }
static u32 o_kr(u32 f, u32 a, u32 n) { return (u32)private_kthread_run(f, a, n, 0); }
static u32 o_ks(u32 t) { return (u32)private_kthread_stop(t, 0, 0, 0); }
static void o_ic(u32 c) { private_init_completion(c, 0, 0, 0); }
static void o_ss(u32 sd, u32 r) { ispcore_video_s_stream(sd, r, 0, 0); }
static struct t32_ci_glob glob;
static const struct t32_ci_ops ops = { o_pr, o_mo, o_vm, o_vf, o_rs, o_lk, o_ul, o_cl, o_li, o_ld, o_pi, o_pd,
				       o_kr, o_ks, o_ic, o_ss, &glob };

static u32 thr(void)	/* a thread word: empty, valid or an error pointer */
{
	u32 k = pick(3);

	return k == 0 ? 0 : k == 1 ? 0x81000000U + (rnd() & 0xfff0) : 0xfffff001U + pick(0xfff);
}

static void str(u32 a, u32 max)
{
	u32 n = pick(max + 1), i;

	for (i = 0; i < n; i++)
		world[a - W + i] = 'A' + pick(26);
	world[a - W + n] = 0;
}

static void build(u32 seed)
{
	static const u32 codes[] = { 0x5000, 0x5001, 0x5002, 0x5003, 0x5100, 0x5101, 0x5102, 0x5103 };
	u32 save = rs, i, p, k, c;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4)
		P32(W + i, rnd());
	SD = W + 0x100;
	REQ = W + 0x40;
	PRIV = W + 0x1000;
	ATTR = W + 0x8000;
	P32(SD + T32_IS_PRIV, PRIV);
	P32(SD + T32_CI_SD_SENSOR_IOCTL, pick(4) ? T32O_PTR(sensor_ioctl) : 0);
	k = pick(24);
	if (k == 0)
		SD = 0;
	else if (k == 1)
		SD = 0xfffff001U + pick(0xfff);
	else if (k == 2)
		P32(SD + T32_IS_PRIV, 0);
	else if (k == 3)
		P32(SD + T32_IS_PRIV, 0xfffff001U + pick(0xfff));
	P32(REQ, pick(3) ? 1 + pick(2) * rnd() : 0);
	P32(REQ + 4, pick(8) ? pick(4) : 4);
	for (i = 0; i < 5; i++)
		P32(PRIV + T32_CI_STATE + 4 * i, pick(3) ? 2 : pick(5));
	for (i = 0; i < 5; i++) {
		p = PRIV + T32_CI_PIPE_SIZE * i + T32_CI_PIPE;
		P32(p, pick(6) ? 1 + pick(4096) : 4096 + pick(3));
		P32(p + 4, pick(6) ? 1 + pick(4096) : 4096 + pick(3));
		k = pick(8);
		c = k < 2 ? codes[pick(8)] : k < 4 ? 0x5200 + pick(12) : k < 6 ? 0x5300 + pick(48)
		    : k == 6 ? 0x4ff8 + pick(0x340) : rnd();
		P32(p + 8, c);
		P32(p + 52, W + 0x4000 + 8 * i);
		P32(W + 0x4000 + 8 * i, W + 0x4100 + 0x80 * i);
		str(W + 0x4100 + 0x80 * i, pick(3) ? 30 : 126);
		P32(p + 56, W + 0x4c00 + 0x80 * i);
		P32(PRIV + T32_CI_MODE + 4 * i, pick(3) ? 10 : pick(12));
		P32(PRIV + T32_CI_FWTHREAD + 4 * i, thr());
		for (k = 0; k < 4; k++) {
			c = PRIV + T32_CI_BINS + 8 + T32_CI_BINS_SIZE * i + T32_CI_BIN_SIZE * k;
			if (pick(2))
				world[c - W] = 0;
			else
				str(c, 40);
		}
	}
	P32(PRIV + T32_CI_FMT_IDX, pick(8));
	P32(PRIV + T32_CI_USERS, W + 0x6000);
	P32(PRIV + T32_CI_NUSERS, pick(6));
	for (i = 0; i < 5; i++)
		P32(W + 0x6000 + T32_CI_USER_SIZE * i + 128, pick(3) ? rnd() : 0);
	P32(PRIV + T32_CI_SATTR, pick(5));
	for (i = 0; i < 88; i++)
		t32_data_d8ce0[i] = rnd();
	t32_data_d9014[0] = pick(4);
	t32_data_d9004[0] = pick(2) ? 0 : rnd();
	t32_data_d9008[0] = pick(2) ? 0 : rnd();
	t32_data_f3354[0] = rnd();
	t32_data_f334c[0] = rnd();
	t32_data_f335c[0] = rnd();
	t32_data_f3358[0] = rnd();
	t32_data_f332c[0] = thr();
	t32_data_f332c[1] = rnd();
	t32_data_f332c[2] = rnd();
	t32_data_f332c[3] = rnd();
	r_reset = pick(6) ? 0 : 1 + pick(2) * rnd();
	r_lib = pick(6) ? 0 : 1 + pick(2) * rnd();
	r_cb = rnd();
	for (i = 0; i < 3; i++)
		r_kr[i] = thr();
	for (i = 0; i < 6; i++)
		r_ks[i] = pick(3) ? 0 : rnd() | 1;
	n_kr = n_ks = 0;
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}

static u64 ours(u32 sd, u32 req, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	return (u32)t32_ispcore_core_ops_init(&ops, sd, req);
}

static void fin(void)
{
	lg.bss[0] = t32_data_f334c[0];
	lg.bss[1] = t32_data_f3354[0];
	lg.bss[2] = t32_data_f335c[0];
	lg.bss[3] = t32_data_f3358[0];
	lg.bss[4] = t32_data_f332c[0];
	lg.bss[5] = t32_data_f332c[1];
	lg.bss[6] = t32_data_f332c[2];
	lg.bss[7] = t32_data_f332c[3];
	lg.bss[8] = t32_data_d9014[0] ^ t32_data_d9004[0] * 3 ^ t32_data_d9008[0] * 5;
	lg.bss[9] = hash((const u8 *)t32_data_d8ce0, 352, 2166136261u);
	lg.bss[10] = n_kr;
	lg.bss[11] = n_ks;
	canon(ATTR);
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;
	struct t32_ci_glob g = {
		t32_data_f334c, t32_data_f3354, t32_data_f335c, (u8 *)t32_data_f3358,
		t32_data_d9014, t32_data_d9004, t32_data_d9008, t32_data_f332c, T32O_PTR(t32_data_d8ce0),
		{ T32O_PTR(t32_data_cd078), T32O_PTR(t32_data_cd098), T32O_PTR(t32_data_cd0b8), T32O_PTR(t32_data_cd0f4),
		  T32O_PTR(t32_data_cd13c), T32O_PTR(t32_data_cd178), T32O_PTR(t32_data_cd254), T32O_PTR(t32_data_cd274),
		  T32O_PTR(t32_data_cd294), T32O_PTR(t32_data_cd2bc), T32O_PTR(t32_data_cd2dc), T32O_PTR(t32_data_cd340) },
		T32O_PTR(t32_data_c36ac), T32O_PTR(t32_data_c3678),
		{ T32O_PTR(t32_data_cd1cc), T32O_PTR(t32_data_cd1ec), T32O_PTR(t32_data_cd20c), T32O_PTR(t32_data_cd230) },
		T32O_PTR(t32_data_cd2ac),
		{ T32O_PTR(t32_data_cd300), T32O_PTR(t32_data_cd310), T32O_PTR(t32_data_cd320), T32O_PTR(t32_data_cd330) },
		T32O_PTR(pm_core_event_process_thread),
		{ T32O_PTR(isp_fw_process), T32O_PTR(isp_fw_process1), T32O_PTR(isp_fw_process2), T32O_PTR(isp_fw_process3) },
	};

	glob = g;
	for (it = 0; it < 40000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, SD, REQ, 0, 0, 0);
		fin();
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ispcore_core_ops_init, SD, REQ, 0, 0, 0);
		fin();
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispinit_oracle: MISMATCH seed %08x: %d/%d log %u/%u\n", seed, r, rr, lg_ours.n, lg.n);
			if (getenv("T32O_DBG") && failed < 6) {
				unsigned int j;

				for (j = 0; j < MAXL && j < lg.n; j++)
					if (memcmp(lg.e[j], lg_ours.e[j], 16))
						printf("  log %u: id %u/%u %x/%x %x/%x %x/%x\n", j, lg_ours.e[j][0], lg.e[j][0],
						       lg_ours.e[j][1], lg.e[j][1], lg_ours.e[j][2], lg.e[j][2], lg_ours.e[j][3], lg.e[j][3]);
				for (j = 0; j < WSZ; j++)
					if (snap[j] != world[j])
						printf("  world +%x: %02x/%02x\n", j, snap[j], world[j]);
				for (j = 0; j < 12; j++)
					if (lg.bss[j] != lg_ours.bss[j])
						printf("  bss %u: %x/%x\n", j, lg_ours.bss[j], lg.bss[j]);
			}
		}
	}
	printf("t32_ispinit_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
