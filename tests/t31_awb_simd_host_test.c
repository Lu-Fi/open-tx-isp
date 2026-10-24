/* Host test: T31 AWB colour-temperature detection fast C / MXU2 paths.
 *
 * The real source is used: the helper header tx_isp_t31_awb_simd.h and the
 * detection code (ISPAWBInterpolation1/2, func_zone_ct_weight,
 * Tiziano_Awb_Ct_Detect and the awb_div/awb_mul wrappers) extracted from
 * driver/t31/tx_isp_tuning.c by the Makefile (t31_awb_*.inc).  The MXU2 ops
 * are the lane model of the header, so this checks the vector algorithms;
 * the instruction encodings are checked on the device by t31_awb_selftest.
 *
 * Checks, all bit-exact:
 *   - t31_fixdiv_fast / t31_fixmul_fast == tx_isp_fixdiv_u32 / fixmul_u32
 *   - the three sweep kernels, C vs MXU2 model
 *   - whole Tiziano_Awb_Ct_Detect, modes 0 / 1 / 2: outputs and all
 *     intermediate arrays
 * Prints host timings of the three modes (x86, indicative only).
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define __maybe_unused __attribute__((unused))
#define pr_debug(...) do { } while (0)
typedef uint32_t u32;
typedef uint64_t u64;

#include "../driver/include/tx_isp/tx_isp_math.h"

static inline u32 fix_point_mult2_32(u32 q, u32 a, u32 b) { return tx_isp_fixmul_u32(q, a, b); }
static inline u32 fix_point_mult3_32(u32 q, u32 a, u32 b, u32 c) { return tx_isp_fixmul3_u32(q, a, b, c); }
static inline u32 fix_point_div_32(u32 q, u32 n, u32 d) { return tx_isp_fixdiv_u32(q, n, d); }
static int32_t fix_point_div_64(int32_t sh, int32_t sc, int32_t nl, int32_t nh,
				int32_t dl, int32_t dh)
{
	uint64_t n = ((uint64_t)(uint32_t)nh << 32) | (uint32_t)nl;
	uint64_t d = ((uint64_t)(uint32_t)dh << 32) | (uint32_t)dl;

	(void)sc;
	if (!d)
		return 0;
	n <<= (sh & 0x1f);
	return (int32_t)(n / d);
}

#define AWB_STATS_COLS 15
#define AWB_STATS_ROWS 15
#define AWB_STATS_ZONES 225
#define CLUSTER_ENTRIES 60
#define CLUSTER_BG_OFF CLUSTER_ENTRIES
#define CLUSTER_CNT_OFF (CLUSTER_ENTRIES * 2)
#define CLUSTER_WAVG_OFF (CLUSTER_ENTRIES * 3)
#define CIDX_MAX_ENTRIES 12
static uint32_t zone_pix_wgh[225], rgbg_d_wght[225], rgbg_dis[225];
static uint32_t rgbg_wght[225] __attribute__((aligned(16)));
static uint32_t Cluster_rgbg_index_num[14 * 14];
static uint32_t Cluster_rgbg_value1[CLUSTER_ENTRIES * 3];
static uint32_t Cluster_rgbg_value2[CLUSTER_ENTRIES * 4];
static uint32_t Cluster_rgbg_index_max[CIDX_MAX_ENTRIES * 3];

#include "../driver/t31/tx_isp_t31_awb_simd.h"
#if !T31_MXU2_ASM
struct t31_vreg t31_vr[32];
#endif

static int awb_ctdet_mode;
#include "t31_awb_defs.inc"
#include "t31_awb_region.inc"

static int scenes_arg = 6000, kernel_arg = 20000, fix_arg = 60000;
static u32 rng = 12345;
static u32 rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 20) { printf("FAIL: " __VA_ARGS__); printf("\n"); } } } while (0)

static void test_fixpoint(void)
{
	u32 q, i, n = 0;

	for (q = 0; q < 32; q++) {
		for (i = 0; i < (u32)fix_arg; i++) {
			u32 num = rnd() << (rnd() % 9), den = rnd() >> (rnd() % 12);

			if (i % 7 == 0) num = 0xffffffffu - rnd() % 3;
			if (i % 11 == 0) den = 1 + rnd() % 3;
			if (i % 13 == 0) den = 0xffffffffu - rnd() % 3;
			CHECK(t31_fixdiv_fast(q, num, den) == tx_isp_fixdiv_u32(q, num, den),
			      "fixdiv q=%u %u/%u", q, num, den);
			CHECK(t31_fixmul_fast(q, num, den) == tx_isp_fixmul_u32(q, num, den),
			      "fixmul q=%u %u*%u", q, num, den);
			n++;
		}
	}
	printf("fixdiv/fixmul: %u cases\n", n);
}

static u32 a_[232], b_[232], c_[232], d_[232], e_[232], bj_[2][232], bc_[2][232];

static void test_kernels(void)
{
	int n, i, k;

	for (n = 0; n < kernel_arg; n++) {
		u32 mode = n & 3, cx, cy, thr, s1[3], s2[3], w1[2], w2[2];

		for (i = 0; i < 225; i++) {
			a_[i] = mode == 3 ? (rnd() << 8) ^ rnd() : rnd() & 0x3ff;
			b_[i] = mode == 3 ? (rnd() << 8) ^ rnd() : rnd() & 0x3ff;
			c_[i] = rnd() << 8;
			d_[i] = rnd();
			e_[i] = (rnd() & 3) ? rnd() & 0xffff : 0;
		}
		cx = mode == 3 ? rnd() << 8 : rnd() & 0x3ff;
		cy = mode == 3 ? rnd() << 8 : rnd() & 0x3ff;
		thr = mode == 1 ? 0 : mode == 2 ? 0xffffffffu : rnd() & 0xffff;
		a_[223] = a_[224] = cx;
		b_[223] = b_[224] = cy + (n & 1);
		t31_awb_sweep_sum_c(a_, b_, c_, d_, cx, cy, thr, &s1[0], &s1[1], &s1[2]);
		t31_awb_sweep_sum(2, a_, b_, c_, d_, cx, cy, thr, &s2[0], &s2[1], &s2[2]);
		CHECK(!memcmp(s1, s2, sizeof(s1)), "sweep_sum n=%d", n);
		t31_awb_sweep_wcount_c(a_, b_, e_, cx, cy, thr, &w1[0], &w1[1]);
		t31_awb_sweep_wcount(2, a_, b_, e_, cx, cy, thr, &w2[0], &w2[1]);
		CHECK(!memcmp(w1, w2, sizeof(w1)), "wcount n=%d", n);
	}
	for (n = 0; n < kernel_arg / 4; n++) {
		for (i = 0; i < 225; i++) {
			a_[i] = rnd() & 0x1ff;
			b_[i] = rnd() & 0x1ff;
			bj_[0][i] = bj_[1][i] = (rnd() & 1) ? 1 + rnd() % 60 : 0;
			bc_[0][i] = bc_[1][i] = rnd() & 7;
		}
		for (k = 0; k < 8; k++) {
			u32 cx = rnd() & 0x1ff, cy = rnd() & 0x1ff, thr = rnd() & 0x7fff;
			u32 id = 1 + rnd() % 60, cc = rnd() & 7;

			t31_awb_assign_c(a_, b_, cx, cy, thr, id, cc, bj_[0], bc_[0]);
			t31_awb_assign(2, a_, b_, cx, cy, thr, id, cc, bj_[1], bc_[1]);
		}
		CHECK(!memcmp(bj_[0], bj_[1], 225 * 4) && !memcmp(bc_[0], bc_[1], 225 * 4),
		      "assign n=%d", n);
	}
	printf("kernels: %d sweep cases, %d assign cases\n", kernel_arg, kernel_arg / 4);
}

struct out {
	u32 ct, status, rgbg[2];
	u32 zone[450];
	u32 wght[225], pixw[225], dis[225], dwght[225];
	u32 v1[180], v2[240];
};

static u64 nowns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (u64)t.tv_sec * 1000000000ull + t.tv_nsec;
}

static void test_ctdet(void)
{
	static const u32 qs[3] = { 10, 8, 12 };
	static struct out o[3];
	static u32 zin[450], pix[225], ct_mesh[225], wmesh[225], awbw[225], lut[514];
	u64 ns[3] = { 0, 0, 0 };
	int n, i, m, scenes = scenes_arg, active = 0;

	for (n = 0; n < scenes; n++) {
		u32 q = qs[n % 3], rg_pos[15], bg_pos[15], light[20] = { 0 };
		u32 ctw[4], ctmode[2], dis[3], cl[6], pp;
		u32 n_lights = rnd() % 6, centres = 1 + rnd() % 4, crg[4], cbg[4];
		u32 spread = 4 + rnd() % 40;

		for (i = 0; i < 15; i++) {
			rg_pos[i] = 60 + i * (16 + (n % 5));
			bg_pos[i] = 50 + i * (14 + (n % 7));
		}
		for (i = 0; i < 225; i++) {
			ct_mesh[i] = 300 + rnd() % 900;
			wmesh[i] = rnd() % 257;
			awbw[i] = 32 + rnd() % 224;
		}
		for (i = 0; i < 514; i++)
			lut[i] = i < 0x202 ? 5 + rnd() % 40 : 0;
		for (i = 0; i < (int)n_lights; i++) {
			light[2 * i] = 100 + i * 40 + rnd() % 20;
			light[2 * i + 1] = 120 + i * 20 + rnd() % 20;
		}
		ctw[0] = 900; ctw[1] = 700; ctw[2] = 300; ctw[3] = 200;
		ctmode[0] = n & 1; ctmode[1] = 32 + rnd() % 128;
		dis[0] = (n >> 1) & 1; dis[1] = 3 << q; dis[2] = 1 << q;
		cl[0] = (n % 5) ? 1 : 0;
		cl[1] = 200 + rnd() % 1500;
		cl[2] = 100 + rnd() % 800;
		cl[3] = 500 + rnd() % 4000;
		cl[4] = 1 + rnd() % 6;
		cl[5] = 2 + rnd() % 12;
		for (i = 0; i < 4; i++) {
			crg[i] = 80 + rnd() % 250;
			cbg[i] = 70 + rnd() % 200;
		}
		for (i = 0; i < 225; i++) {
			u32 c = rnd() % centres, rg = crg[c] + rnd() % spread, bg = cbg[c] + rnd() % spread;

			if (!(rnd() % 23)) { rg = rnd() % 600; bg = rnd() % 500; }
			zin[i] = rg << q;
			zin[225 + i] = bg << q;
			pix[i] = (n % 97 == 7) ? 0 : ((rnd() % 9) ? 1500 + rnd() % 2500 : 0);
		}
		for (m = 0; m < 3; m++) {
			u64 t0;

			pp = q;
			memcpy(o[m].zone, zin, sizeof(zin));
			o[m].ct = 5000; o[m].status = 0xdead; o[m].rgbg[0] = o[m].rgbg[1] = 0;
			awb_ctdet_mode = m;
			t0 = nowns();
			Tiziano_Awb_Ct_Detect(o[m].zone, pix, light, n_lights, ctw, ctmode, wmesh,
					      15, 15, rg_pos, bg_pos, ct_mesh, awbw, dis, &o[m].ct,
					      lut, &pp, cl, o[m].rgbg, &o[m].status);
			ns[m] += nowns() - t0;
			memcpy(o[m].wght, rgbg_wght, sizeof(o[m].wght));
			memcpy(o[m].pixw, zone_pix_wgh, sizeof(o[m].pixw));
			memcpy(o[m].dis, rgbg_dis, sizeof(o[m].dis));
			memcpy(o[m].dwght, rgbg_d_wght, sizeof(o[m].dwght));
			memcpy(o[m].v1, Cluster_rgbg_value1, sizeof(o[m].v1));
			memcpy(o[m].v2, Cluster_rgbg_value2, sizeof(o[m].v2));
		}
		if (cl[0] == 1 && o[0].v2[CLUSTER_CNT_OFF])
			active++;
		for (m = 1; m < 3; m++)
			CHECK(!memcmp(&o[0], &o[m], sizeof(o[0])),
			      "ctdet scene %d q=%u mode %d: ct %u/%u status %u/%u rg %u/%u bg %u/%u",
			      n, q, m, o[0].ct, o[m].ct, o[0].status, o[m].status,
			      o[0].rgbg[0], o[m].rgbg[0], o[0].rgbg[1], o[m].rgbg[1]);
	}
	printf("ctdet: %d scenes x 3 modes (%d with live clusters); host us/call: reference %.1f, fast C %.1f, MXU2 model %.1f\n",
	       scenes, active, ns[0] / 1000.0 / scenes, ns[1] / 1000.0 / scenes,
	       ns[2] / 1000.0 / scenes);
}

int main(int argc, char **argv)
{
	if (argc > 1)
		scenes_arg = atoi(argv[1]);
	if (argc > 2)
		kernel_arg = atoi(argv[2]);
	if (argc > 3)
		fix_arg = atoi(argv[3]);
	test_fixpoint();
	test_kernels();
	test_ctdet();
	if (fails) {
		printf("%d FAILURES\n", fails);
		return 1;
	}
	printf("t31_awb_simd_host_test: PASS\n");
	return 0;
}
