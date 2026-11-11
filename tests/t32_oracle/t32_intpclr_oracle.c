// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_intp_static_clear (tx_isp_t32_librest.h) against the
 * vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc; its memcpy is
 * renamed to a recorder by fix_op20_ipsclr.sh, the vendor copies from NULL).
 * Per case the snapshot objects and the count hold random bytes, the recorder
 * fills each destination with a per call pattern and returns a random token.
 * BEYOND VENDOR (#281): ours zeroes the objects instead of copying from NULL.
 * Compared: the vendor's memcpy calls must be exactly (object k, NULL, size k)
 * in order (the one named difference: ours writes zeros there, the recorder a
 * pattern), the count, every byte of the five objects outside that (16-byte
 * canaries in front and behind), and the return value (ours: the last object,
 * what memcpy returns; vendor: the recorder's token of the last call).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_librest.h"

extern u8 t32_data_e3cf0[128], t32_data_e3d70[128], t32_data_e3df0[8], t32_data_e3df8[128], t32_data_e3e78[128];
extern u32 t32_data_e3ef8[1];
extern u64 tisp_intp_static_clear(u32, u32, u32, u32);

static u32 rs = 0x1d872b41u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 8
struct hlog { unsigned int n; u32 e[LOGN][3]; };
static struct hlog vl, ol, *cur;
static u32 tok[LOGN];
static u8 *mapv[6], *mapo[6];	/* vendor object -> our object, for the log compare */

static u32 rec(u32 dst, u32 src, u32 n)
{
	unsigned int k = cur->n;

	if (k < LOGN) { cur->e[k][0] = dst; cur->e[k][1] = src; cur->e[k][2] = n; }
	cur->n++;
	if (dst && n <= 128)
		memset((void *)(uintptr_t)dst, 0x30 + k, n);
	return k < LOGN ? tok[k] : 0;
}
u64 op20_ips_memcpy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(a0, a1, a2); }

#define CAN 16
static u8 *mk(u32 size) { return (u8 *)t32o_alloc(size + 2 * CAN) + CAN; }

int main(void)
{
	static const u32 sz[5] = { 128, 128, 8, 128, 128 };
	u8 *vo[5] = { t32_data_e3e78, t32_data_e3df8, t32_data_e3df0, t32_data_e3d70, t32_data_e3cf0 };
	u8 *oo[5], init[5][128];
	static u8 cnt_o[4 + 2 * CAN] __attribute__((aligned(16)));
	u32 *cnt = (u32 *)(void *)(cnt_o + CAN), rv, ro, i, k;
	unsigned long n, N = 4000;

	for (k = 0; k < 5; k++) oo[k] = mk(sz[k]);
	for (k = 0; k < 5; k++) { mapv[k] = vo[k]; mapo[k] = oo[k]; }
	for (n = 0; n < N; n++) {
		struct t32_ips s;
		u32 c0 = rnd();

		for (k = 0; k < LOGN; k++) tok[k] = rnd();
		for (k = 0; k < 5; k++)
			for (i = 0; i < sz[k]; i++) init[k][i] = (u8)rnd();
		for (k = 0; k < 5; k++) {
			memcpy(oo[k], init[k], sz[k]);
			memset(oo[k] - CAN, 0xee, CAN); memset(oo[k] + sz[k], 0xee, CAN);
		}
		memset(cnt_o, 0xee, sizeof(cnt_o));
		*cnt = c0;

		t32o_reset();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		s.count = cnt;
		s.blk_e78 = oo[0]; s.blk_df8 = oo[1]; s.pair_df0 = oo[2]; s.blk_d70 = oo[3]; s.blk_cf0 = oo[4];
		ro = T32O_PTR(t32_intp_static_clear(&s));

		t32o_reset();
		for (k = 0; k < 5; k++) memcpy(vo[k], init[k], sz[k]);
		t32_data_e3ef8[0] = c0;
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_intp_static_clear, rnd(), rnd(), rnd(), rnd(), 0);

		for (i = 0; i < vl.n && i < LOGN; i++)
			for (k = 0; k < 5; k++)
				if (vl.e[i][0] == T32O_PTR(mapv[k])) vl.e[i][0] = T32O_PTR(mapo[k]);
		int bad = rv != tok[4] || ro != T32O_PTR(oo[4]) || vl.n != 5 || ol.n || t32_data_e3ef8[0] != *cnt;
		for (k = 0; k < 5 && !bad; k++) {
			bad = vl.e[k][0] != T32O_PTR(oo[k]) || vl.e[k][1] != 0 || vl.e[k][2] != sz[k];
			for (i = 0; i < sz[k] && !bad; i++)
				bad = oo[k][i] != 0 || vo[k][i] != (u8)(0x30 + k);	/* ours 0, the vendor's recorder pattern */
			for (i = 0; i < CAN && !bad; i++)
				bad = oo[k][-1 - (int)i] != 0xee || oo[k][sz[k] + i] != 0xee;
		}
		for (i = 0; i < CAN && !bad; i++)
			bad = cnt_o[i] != 0xee || cnt_o[CAN + 4 + i] != 0xee;
		if (bad) {
			printf("t32_intpclr_oracle: MISMATCH case %lu: ret %08x/%08x calls %u/%u count %08x/%08x\n",
			       n, rv, ro, vl.n, ol.n, t32_data_e3ef8[0], *cnt);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  v %08x %08x %u o %08x %08x %u\n", vl.e[i][0], vl.e[i][1], vl.e[i][2],
				       ol.e[i][0], ol.e[i][1], ol.e[i][2]);
			return 1;
		}
	}
	printf("t32_intpclr_oracle: %lu cases, 0 mismatches\n", N);
	return 0;
}
