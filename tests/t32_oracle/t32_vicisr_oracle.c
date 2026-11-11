// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable isp_vic_interrupt_service_routine (driver/t32/tx_isp_t32_vicisr.h)
 * against the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc, built for the host with env.c).  The VIC status registers (KSEG1 0xb338020c +
 * (ch << 12)) and the OST counter (0xb0002078) are host pages mapped at those addresses.
 * Random worlds built twice (subdev / private NULL, error or valid; status words with frame
 * start / done / error / MDMA bits, slot registers, single-frame mode and slot flags, masks,
 * start budgets, capture counters, notify callback and frame-done hook present or not, print
 * flag and counts, 1..3 channels); compared: return value, call log (fs works, queued works,
 * DMA mode, the frame-start text and time, notify events, VIC control, stop, reset / stream on,
 * MDMA calls, frame-done hook), completions, every bss word and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_vicisr.h"

u64 isp_vic_interrupt_service_routine(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_d9008[], t32_data_d9320[], t32_data_d931c[], t32_data_d934c[], t32_data_f3838[], system_wq;
extern u32 t32_data_cedf4[], t32_data_cee04[], t32_data_cee14[], t32_data_cee24[], t32_data_c40d8[], t32_data_bd790[];
extern u32 t32o_complete_n, t32o_complete_arg;
u64 private_complete(u32 a0, u32 a1, u32 a2, u32 a3);

#define WSZ	0x1400u
#define MAXL	64
static u32 rs = 0x51c15u;
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
struct lg { unsigned int n; u32 e[MAXL][4]; u32 g[2 + 1 + 26 + 1], cn, ca; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W, SD, ST[5];
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b; lg.e[lg.n][3] = c;
	}
	lg.n++;
}
static void P32(u32 a, u32 v) { memcpy(T32O_HOST(a), &v, 4); }
u64 lib_tisp_vic_intp_handle(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; memcpy(T32O_HOST(a0), ST, sizeof(ST)); L(1, 0, 0, 0); return srnd(); }
u64 ispcore_update_fs_work(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(2, a0, a1, a2); return srnd(); }
u64 queue_work_on(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(3, a0, a1, a2); return srnd(); }
u64 isp_vic_set_dma_enable(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(4, a0, a1, 0); return srnd(); }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	u32 h = 2166136261u;
	const char *p = T32O_HOST(a4);

	while (*p)
		h = (h ^ (u8)*p++) * 16777619u;
	L(5, a0 ^ (a3 << 4), a1 ^ a2, h ^ a5);
	return 0;
}
u64 lib_tisp_vic_ctrl(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(6, a0, a1, 0); return srnd(); }
u64 lib_tisp_vic_stop_byindex(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(7, 0, 0, 0); return srnd(); }
u64 lib_tisp_vic_reset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(8, 0, 0, 0); return srnd(); }
u64 lib_tisp_vic_stream_on(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(9, 0, 0, 0); return srnd(); }
u64 vic_mdma_irq_function(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(10, a0, a1, 0); return srnd(); }
static u64 r_cb(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(11, a0, a1, a2); return srnd(); }
static u64 r_fd(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(12, a0, 0, 0); return srnd(); }

static void o_ih(u32 *st) { lib_tisp_vic_intp_handle(T32O_PTR(st), 0, 0, 0); }
static void o_fs(u32 s, u32 c, u32 a) { ispcore_update_fs_work(s, c, a, 0); }
static void o_q(u32 s)
{
	static u32 *const w[4] = { t32_data_cee24, t32_data_cee14, t32_data_cee04, t32_data_cedf4 };

	queue_work_on(1, system_wq, T32O_PTR(w[s]), 0);
}
static void o_de(u32 v, u32 m) { isp_vic_set_dma_enable(v, m, 0, 0); }
static void o_ps(const char *m, u32 t) { isp_printf(1, T32O_PTR(t32_data_c40d8), T32O_PTR(t32_data_bd790), 541, T32O_PTR(m), t); }
static int o_call(u32 fn, u32 a0, u32 a1, u32 a2) { return (int)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(a0, a1, a2, 0); }
static void o_vc(u32 n, u32 v) { lib_tisp_vic_ctrl(n, v, 0, 0); }
static void o_sb(void) { lib_tisp_vic_stop_byindex(0, 0, 0, 0); }
static void o_co(u32 c) { private_complete(c, 0, 0, 0); }
static void o_vr(void) { lib_tisp_vic_reset(0, 0, 0, 0); }
static void o_so(void) { lib_tisp_vic_stream_on(0, 0, 0, 0); }
static void o_md(u32 v, u32 n) { vic_mdma_irq_function(v, n, 0, 0); }
static const struct t32_vicisr_ops ops = { o_ih, o_fs, o_q, o_de, o_ps, o_call, o_vc, o_sb, o_co, o_vr, o_so, o_md,
	t32_data_d9008, t32_data_d9320, (u8 *)t32_data_d931c, t32_data_f3838, t32_data_d934c };

static u32 bits(void)
{
	u32 v = 0;

	if (rnd() % 2)
		v |= 0x10;
	if (rnd() % 2)
		v |= 0x1;
	if (rnd() % 4 == 0)
		v |= rnd() & 0xffffff00u;
	if (rnd() % 4 == 0)
		v |= rnd() & 0xee;
	if (rnd() % 8 == 0)
		v |= 1U << (8 + rnd() % 24);	/* a single error bit */
	return v;
}

static void build(u32 seed)
{
	u32 save = rs, i, V;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4)
		P32(W + i, rnd());
	SD = rnd() % 16 ? W : rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff;
	V = W + 0x400;
	P32(W + T32_SD_PRIV, rnd() % 16 ? V : rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff);
	P32(W + T32_SD_NOTIFY, rnd() % 4 ? T32O_PTR(r_cb) : 0);
	P32(W + 244, W + 0xc00);	/* status bit 0x400: the vendor loads +244 -> +936 */
	P32(V + T32_VI_SINGLE, rnd() % 3 ? 1 : rnd() % 3);
	for (i = 0; i < 4; i++) {
		world[0x400 + T32_VI_SLOTFLAG + i] = rnd() % 4;
		P32(V + T32_VI_BUDGET + 4 * i, rnd() % 4 - 1);
	}
	for (i = 0; i < 2; i++) {
		P32(V + T32_VI_FSMASK + 4 * i, rnd() % 3 ? rnd() % 16 : 0);
		P32(V + T32_VI_FDMASK + 4 * i, rnd() % 3 ? rnd() % 16 : 0);
		P32(V + T32_VI_LASTMASK + 4 * i, rnd() % 4 ? rnd() % 4 : rnd());
	}
	P32(V + T32_VI_PENDING, rnd() % 2 ? rnd() : 0);
	P32(V + T32_VI_WAITING, rnd() % 2 ? rnd() : 0);
	P32(V + T32_VI_NCH, rnd() % 8 ? 1 + rnd() % 3 : rnd() % 2 ? 8 + rnd() % 3 : 0);
	P32(V + T32_VI_SENSOR, W + 0x1000);
	P32(W + 0x1000 + 148, rnd() % 2 ? 2 : rnd() % 4);
	P32(V + T32_VI_BUDGET_ON, rnd() % 2 ? rnd() : 0);
	P32(V + T32_VI_CAP_ON, rnd() % 3 ? rnd() : 0);
	P32(V + T32_VI_CAP_MODE, rnd() % 4 ? rnd() % 4 : rnd());
	i = rnd() % 8;
	P32(V + T32_VI_CAP_N, i);
	P32(V + T32_VI_CAP_END, rnd() % 2 ? i + 1 : rnd() % 8);
	for (i = 0; i < 2; i++) {
		P32(T32_VIC_STAT_REG(i), rnd() % 2 ? rnd() & 0x707 : 0);
		t32_data_d9320[i] = rnd() % 14;
	}
	P32(T32_OST_COUNT, rnd());
	t32_data_d9008[0] = rnd() % 2 ? 0 : rnd();
	t32_data_d931c[0] = rnd() % 2 ? rnd() % 8 : rnd();
	t32_data_f3838[0] = rnd() % 3 ? T32O_PTR(r_fd) : 0;
	for (i = 0; i < 26; i++)
		t32_data_d934c[i] = rnd();
	ST[0] = rnd();
	ST[1] = bits();
	ST[2] = bits();
	ST[3] = rnd() % 2 ? rnd() & 0xf : 0;
	ST[4] = rnd() % 2 ? rnd() & 0xf : 0;
	system_wq = rnd();
	t32o_complete_n = 0;
	t32o_complete_arg = 0;
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}

static void fin(void)
{
	memcpy(lg.g, t32_data_d9320, 8);
	lg.g[2] = t32_data_d931c[0];
	memcpy(lg.g + 3, t32_data_d934c, 26 * 4);
	lg.g[29] = t32_data_d9008[0];
	lg.cn = t32o_complete_n;
	lg.ca = t32o_complete_arg;
}

static u64 ours(u32 sd, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return (u32)t32_isp_vic_interrupt_service_routine(&ops, sd);
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	if (mmap((void *)0xb3380000ul, 0x2000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == MAP_FAILED ||
	    mmap((void *)0xb0002000ul, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == MAP_FAILED) {
		printf("t32_vicisr_oracle: cannot map the register pages\n");
		return 1;
	}
	for (it = 0; it < 20000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, SD, 0, 0, 0, 0);
		fin();
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))isp_vic_interrupt_service_routine, SD, rnd(), rnd(), rnd(), 0);
		fin();
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_vicisr_oracle: MISMATCH seed %08x: %d/%d log %u/%u\n", seed, r, rr, lg_ours.n, lg.n);
			if (getenv("T32O_DBG") && failed == 1) {
				unsigned int k;

				for (k = 0; k < lg.n && k < MAXL; k++)
					printf("  %2u: %u %08x %08x %08x | %u %08x %08x %08x\n", k, lg_ours.e[k][0], lg_ours.e[k][1],
					       lg_ours.e[k][2], lg_ours.e[k][3], lg.e[k][0], lg.e[k][1], lg.e[k][2], lg.e[k][3]);
				for (k = 0; k < WSZ; k++)
					if (snap[k] != world[k])
						printf("  world +%x %02x/%02x\n", k, snap[k], world[k]);
				for (k = 0; k < 30; k++)
					if (lg.g[k] != lg_ours.g[k])
						printf("  g%u %08x/%08x\n", k, lg_ours.g[k], lg.g[k]);
			}
		}
	}
	printf("t32_vicisr_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
