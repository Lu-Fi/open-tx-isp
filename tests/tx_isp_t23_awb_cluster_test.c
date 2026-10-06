/*
 * Host test of driver/t23/tx_isp_t23_awb_cluster.h: the AWB cluster stage,
 * the colour temperature trend and the object selection of the stock
 * tx-isp-t23.ko.  The algorithm itself is audited against the stock module
 * in the MIPS emulator (driver/t23/audit/awb_cluster_emu.py: identical
 * state and weights for random scenes); this test pins the data handling
 * and one scene by hand.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_awb_cluster.h"

#define Q 10U

static void positions(uint32_t *pr, uint32_t *pb)
{
	unsigned int i;

	for (i = 0; i < T23X_CL_POS; i++) {
		pr[i] = 100U + 20U * i;
		pb[i] = 100U + 20U * i;
	}
}

static void test_bin(void)
{
	uint32_t pr[T23X_CL_POS], pb[T23X_CL_POS];

	positions(pr, pb);
	assert(t23x_cl_bin(99U << Q, pr, Q) == 0);              /* below */
	assert(t23x_cl_bin(100U << Q, pr, Q) == 1);             /* first edge */
	assert(t23x_cl_bin((120U << Q) - 1U, pr, Q) == 1);
	assert(t23x_cl_bin(120U << Q, pr, Q) == 2);
	assert(t23x_cl_bin((380U << Q) - 1U, pr, Q) == 14);
	assert(t23x_cl_bin(380U << Q, pr, Q) == 15);            /* upper end */
	assert(t23x_cl_bin((380U << Q) + 1U, pr, Q) == 0);      /* above */
}

static void test_count(void)
{
	static struct t23x_awb_cluster_state st;
	uint32_t pr[T23X_CL_POS], pb[T23X_CL_POS];
	uint32_t zr[T23X_CL_ZONES], zb[T23X_CL_ZONES];
	unsigned int i, total = 0;

	positions(pr, pb);
	/* 100 zones in cell (col 0, row 0), 100 at the upper end of both
	 * ranges (last cell), 25 at the upper end of rg only (col 13) in
	 * row 2 */
	for (i = 0; i < T23X_CL_ZONES; i++) {
		if (i < 100U) {
			zr[i] = 105U << Q;
			zb[i] = 100U << Q;
		} else if (i < 200U) {
			zr[i] = 380U << Q;
			zb[i] = 380U << Q;
		} else {
			zr[i] = 380U << Q;
			zb[i] = 145U << Q;
		}
	}
	memset(&st, 0, sizeof(st));
	t23x_awb_cluster_count(&st, zr, zb, pr, pb, Q);
	assert(st.index_num[0] == 100);
	assert(st.index_num[13 * 14 + 13] == 100);
	assert(st.index_num[2 * 14 + 13] == 25);
	for (i = 0; i < 196U; i++)
		total += st.index_num[i];
	assert(total == 225U);
	/* the histogram is state: a second run adds */
	t23x_awb_cluster_count(&st, zr, zb, pr, pb, Q);
	assert(st.index_num[0] == 200);
}

static void test_stage(void)
{
	static struct t23x_awb_cluster_state st;
	uint32_t pr[T23X_CL_POS], pb[T23X_CL_POS];
	uint32_t zr[T23X_CL_ZONES], zb[T23X_CL_ZONES], w[T23X_CL_ZONES];
	uint32_t cl[10] = { 1, 300, 300, 300, 2, 10, 0, 0, 0, 32 };
	uint32_t i;

	positions(pr, pb);
	/* 120 zones around (176, 176), 60 around (260, 230), 45 at (300, 150) */
	for (i = 0; i < T23X_CL_ZONES; i++) {
		if (i < 120U) {
			zr[i] = (176U << Q) + i % 7U;
			zb[i] = (176U << Q) + i % 5U;
		} else if (i < 180U) {
			zr[i] = (260U << Q) + i % 11U;
			zb[i] = (230U << Q) + i % 3U;
		} else {
			zr[i] = 300U << Q;
			zb[i] = 150U << Q;
		}
		w[i] = 1024U * (1U + i % 4U);
	}
	memset(&st, 0, sizeof(st));
	t23x_awb_cluster_count(&st, zr, zb, pr, pb, Q);
	assert(t23x_awb_cluster_weights(&st, cl, zr, zb, w, pr, pb, Q) == 1);
	/* three clusters of 120, 60 and 45 zones; weights follow the count
	 * against the biggest: x 1, x 1/2, x 44/120 */
	assert(st.v2_rg[0] == 176U && st.v2_bg[0] == 176U && st.v2_cnt[0] == 120U);
	assert(st.v2_rg[1] == 260U && st.v2_bg[1] == 230U && st.v2_cnt[1] == 60U);
	assert(st.v2_rg[2] == 300U && st.v2_bg[2] == 150U && st.v2_cnt[2] == 44U);
	assert(w[0] == 1024U && w[1] == 2048U && w[2] == 3072U && w[3] == 4096U);
	assert(w[120] == 512U && w[121] == 1024U && w[122] == 1536U);
	assert(w[180] == 375U && w[181] == 751U);

	/* ClusterEn is the caller's check; with no zone at all nothing comes
	 * out and the weights stay */
	memset(&st, 0, sizeof(st));
	for (i = 0; i < T23X_CL_ZONES; i++)
		w[i] = 0;
	t23x_awb_cluster_count(&st, zr, zb, pr, pb, Q);
	assert(t23x_awb_cluster_weights(&st, cl, zr, zb, w, pr, pb, Q) == 0);
	for (i = 0; i < T23X_CL_ZONES; i++)
		assert(w[i] == 0);
}

/* an iteration limit of 2^32 - 1 with convergence 0 must still end */
static void test_iteration_cap(void)
{
	static struct t23x_awb_cluster_state st;
	uint32_t pr[T23X_CL_POS], pb[T23X_CL_POS];
	uint32_t zr[T23X_CL_ZONES], zb[T23X_CL_ZONES], w[T23X_CL_ZONES];
	uint32_t cl[10] = { 1, 40, 100, 100, 0, 0xffffffffU, 0, 0, 0, 32 };
	uint32_t seed = 12345, i, run;

	positions(pr, pb);
	memset(&st, 0, sizeof(st));
	for (run = 0; run < 20U; run++) {
		for (i = 0; i < T23X_CL_ZONES; i++) {
			seed = seed * 1103515245U + 12345U;
			zr[i] = (100U << Q) + (seed >> 8) % (280U << Q);
			seed = seed * 1103515245U + 12345U;
			zb[i] = (100U << Q) + (seed >> 8) % (280U << Q);
			w[i] = 1000U + i;
		}
		t23x_awb_cluster_count(&st, zr, zb, pr, pb, Q);
		(void)t23x_awb_cluster_weights(&st, cl, zr, zb, w, pr, pb, Q);
	}
}

static void test_pick_and_trend(void)
{
	uint32_t obj[7] = { 1, 1100, 1200, 1300, 1400, 1500, 1600 };
	uint32_t api[7] = { 0, 1024, 1024, 1024, 1024, 1024, 1024 };
	uint32_t st[2];

	/* status[0] 1: the IQ copy; 2: the live object; 0 + a pending user
	 * value (status[1] 2): the live object; else the IQ copy */
	st[0] = 1; st[1] = 2;
	assert(t23x_awb_obj_pick(obj, api, st) == api);
	st[0] = 2; st[1] = 0;
	assert(t23x_awb_obj_pick(obj, api, st) == obj);
	st[0] = 0; st[1] = 2;
	assert(t23x_awb_obj_pick(obj, api, st) == obj);
	st[0] = 0; st[1] = 0;
	assert(t23x_awb_obj_pick(obj, api, st) == api);

	/* trend off: the gain is g << 2, limited */
	assert(t23x_awb_gain_trend(api, 4000U, 0x200U, 0) == 0x800U);
	assert(t23x_awb_gain_trend(api, 4000U, 0x1000U, 0) == 0x3fffU);
	/* on: >= 5000 K [1], [2]; <= 3000 K [5], [6]; between [3], [4] */
	assert(t23x_awb_gain_trend(obj, 5000U, 0x200U, 0) == 0x800U + 1100U - 1024U);
	assert(t23x_awb_gain_trend(obj, 5000U, 0x200U, 1) == 0x800U + 1200U - 1024U);
	assert(t23x_awb_gain_trend(obj, 4999U, 0x200U, 0) == 0x800U + 1300U - 1024U);
	assert(t23x_awb_gain_trend(obj, 3001U, 0x200U, 1) == 0x800U + 1400U - 1024U);
	assert(t23x_awb_gain_trend(obj, 3000U, 0x200U, 0) == 0x800U + 1500U - 1024U);
	assert(t23x_awb_gain_trend(obj, 1000U, 0x200U, 1) == 0x800U + 1600U - 1024U);
	/* an offset below 1024 that takes the gain below 0 wraps to the limit
	 * (stock does not saturate at 0) */
	obj[5] = 0;
	assert(t23x_awb_gain_trend(obj, 2000U, 0x80U, 0) == 0x3fffU);
	obj[5] = 100;
	assert(t23x_awb_gain_trend(obj, 2000U, 0x100U, 0) == 0x400U - 924U);
	/* not exactly 1: off */
	obj[0] = 2;
	assert(t23x_awb_gain_trend(obj, 5000U, 0x200U, 0) == 0x800U);
}

int main(void)
{
	test_bin();
	test_count();
	test_stage();
	test_iteration_cap();
	test_pick_and_trend();
	puts("tx_isp_t23_awb_cluster_test ok");
	return 0;
}
