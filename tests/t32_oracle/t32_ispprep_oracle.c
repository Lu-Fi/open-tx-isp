// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_prepare_module and isp_vic_set_dma_enable
 * (driver/t32/tx_isp_t32_ispprep.h) against the vendor functions from Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc, built for the host with env.c).
 * Random worlds built twice (core device / private block NULL, error pointer or valid, channel
 * states around 2, channel count 0..5, VIC mode 3 / 4 / other, DMA armed or not, counters around
 * their limits); scripted fw / event queue states and set_par results.  Compared: return value,
 * call log (isp_printf format and defined arguments, fw_empty channel, event_work_empty, msleep,
 * set_par arguments, dma_start), the busy counter and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispprep.h"

u64 ispcore_prepare_module(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_vic_set_dma_enable(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_f336c[1], t32_data_ccbb8[1], t32_data_ccbdc[1], t32_data_ccbfc[1], t32_data_ccc18[1], t32_data_c3aa4[1];

#define WSZ	0x2000u
#define MAXL	32
static u32 rs = 0x9e9u;
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
struct lg { unsigned int n; u32 e[MAXL][7]; };
static struct lg lg, lg_ours;
static void L(u32 id, u32 a, u32 b, u32 c, u32 d, u32 e, u32 f)
{
	if (lg.n < MAXL) {
		u32 *p = lg.e[lg.n];

		p[0] = id; p[1] = a; p[2] = b; p[3] = c; p[4] = d; p[5] = e; p[6] = f;
	}
	lg.n++;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 f = a1 == T32O_PTR(t32_data_ccbdc) ? 1 : a1 == T32O_PTR(t32_data_ccbfc) ? 2 :
		a1 == T32O_PTR(t32_data_ccbb8) ? 3 : a1 == T32O_PTR(t32_data_ccc18) ? 4 : a1 == T32O_PTR(t32_data_c3aa4) ? 5 : 9;

	L(1, a0, f, f == 1 ? a2 : 0, f >= 3 ? a3 : 0, 0, 0);
	return 0;
}
u64 lib_tisp_fw_process_empty(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(2, a0, 0, 0, 0, 0, 0); return srnd() % 3 ? 1 : 0; }
u64 isp_core_event_work_empty(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(3, a0, 0, 0, 0, 0, 0); return srnd() % 2; }
u64 private_msleep(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(4, a0, 0, 0, 0, 0, 0); return 0; }
u64 lib_tisp_vic_set_par(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	L(5, a0, a1, a2, a3, a4, a5);
	return srnd() % 4 ? 0 : srnd() | 1;
}
u64 lib_tisp_vic_dma_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(6, 0, 0, 0, 0, 0, 0); return 0; }

static void o_log(u32 lvl, u32 line, u32 arg)
{
	u32 fmt = line == 0 ? T32O_PTR(t32_data_ccbdc) : line == 1 ? T32O_PTR(t32_data_ccbfc) :
		  line == 4192 ? T32O_PTR(t32_data_ccbb8) : line == 4245 ? T32O_PTR(t32_data_ccc18) : T32O_PTR(t32_data_c3aa4);

	isp_printf(lvl, fmt, arg, line);
}
static int o_fw(u32 ch) { return (int)lib_tisp_fw_process_empty(ch, 0, 0, 0); }
static int o_ev(u32 ch) { return (int)isp_core_event_work_empty(ch, 0, 0, 0); }
static void o_ms(u32 ms) { private_msleep(ms, 0, 0, 0); }
static int o_sp(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { return (int)lib_tisp_vic_set_par(a0, a1, a2, a3, a4, a5); }
static void o_ds(void) { lib_tisp_vic_dma_start(0, 0, 0, 0); }
static const struct t32_prep_ops ops = { o_log, o_fw, o_ev, o_ms, o_sp, o_ds };

static u8 *world, snap[WSZ];
static u32 W, SD;
static u32 badp(void) { return rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff; }
static void build(u32 seed)
{
	u32 save = rs, i, P, c;

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4) {
		u32 v = rnd();

		memcpy(world + i, &v, 4);
	}
	SD = rnd() % 16 ? W : badp();
	t32_ic_w32(W, T32_SD_PRIV, rnd() % 12 ? W + 0x100 : badp());
	P = W + 0x100;
	for (i = 0; i < 4; i++)
		t32_ic_w32(P, T32_PM_CH_STATE + 4 * i, rnd() % 6 ? 2 + rnd() % 2 : rnd() % 2);
	t32_ic_w32(P, T32_PRIV_NCH, rnd() % 6);
	/* VIC at W + 0x600 (fields up to +2448 fit) */
	i = rnd() % 4;
	t32_ic_w32(W + 0x600, T32_VIC_MODE, i == 0 ? 3 : i == 1 ? 4 : rnd() % 8);
	t32_ic_w32(W + 0x600, T32_VIC_DMA_ARMED, rnd() % 5 ? 1 + rnd() % 2 : 0);
	t32_ic_w32(W + 0x600, T32_VIC_DMA_MODE, rnd() % 4);
	c = rnd() % 4 ? rnd() % 20 : rnd();
	t32_ic_w32(W + 0x600, T32_VIC_DMA_CNT, c);
	t32_ic_w32(W + 0x600, T32_VIC_DMA_DONE, rnd() % 4 ? c - rnd() % 6 : rnd());
	t32_ic_w32(W + 0x600, T32_VIC_DMA_LIMIT, rnd() % 4 ? c + 1 - rnd() % 3 : rnd());
	t32_data_f336c[0] = rnd();
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0xdeadbeefu) | 1;
	rs = save;
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 30000; it++) {
		u32 seed = rnd(), k = it & 1, on = rnd() % 5 ? rnd() % 4 : rnd(), cnt_ours;
		int r = 0, rr;

		build(seed);
		if (k)
			t32_isp_vic_set_dma_enable(&ops, W + 0x600, on);
		else
			r = t32_ispcore_prepare_module(&ops, SD, t32_data_f336c);
		lg_ours = lg;
		cnt_ours = t32_data_f336c[0];
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))(k ? isp_vic_set_dma_enable : ispcore_prepare_module),
					 k ? W + 0x600 : SD, on, rnd(), rnd(), 0);
		if (k)
			rr = 0;
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ) || cnt_ours != t32_data_f336c[0]) {
			if (failed++ < 5)
				printf("t32_ispprep_oracle: MISMATCH %s seed %08x: %d/%d log %u/%u\n", k ? "dma_enable" : "prepare", seed, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispprep_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
