// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the parameter getters of driver/t32/tx_isp_t32_param.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc):
 * tisp_version_param_array_get, tisp_comn_param_array_get,
 * tisp_lut_params_copy.
 *
 * Compared: return values, the length word, every byte of the output buffer
 * (plus canaries), the vendor's common block da850[ch] and both LUT infos
 * and all table memory (canaries behind every table).  The version block's
 * never-written bytes are the ref's zeroed stack (vendor: stack garbage).
 * Inputs: random channel state / AE state / mode / sizes 0..64.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_param.h"

u64 tisp_version_param_array_get(u32, u32, u32, u32);
u64 tisp_comn_param_array_get(u32, u32, u32, u32);
u64 tisp_lut_params_copy(u32, u32, u32, u32);

#define CALL(f, a, b, c) t32o_call((t32o_fn)(void (*)(void))(f), a, b, c, 0, 0)

u32 t32_data_d9008;
extern u8 t32_data_dd2d4[0x400];	/* env.c: AE state */
u32 t32_data_dc214[24];
char t32_data_c7a3c[] = "isp_20260618a";	/* data.c (c7a3c) */
char t32_data_c7a4c[] = "H20260618a";
char t32_data_c7b48[] = "PRJ007";

static u32 rs = 0x9a7a3e75u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, k, failed = 0, compared = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	static u8 blk_o[704], dc_o[1808], ae_o[1024];

	for (c = 0; c < n; c++) {
		u32 which = c % 3, vr = 0, orr = 0, lv, lo;
		u8 *outv, *outo;
		u32 *lenv;
		int bad = 0;

		t32o_reset();
		outv = t32o_alloc(600); outo = t32o_alloc(600); lenv = t32o_alloc(8);
		for (k = 0; k < 600; k++)
			outv[k] = outo[k] = (u8)rnd();
		lv = lo = *lenv = rnd();
		if (which == 0) {
			vr = (u32)CALL(tisp_version_param_array_get, rnd(), T32O_PTR(outv), T32O_PTR(lenv));
			orr = (u32)t32_version_param_array_get(outo, &lo);
			lv = *lenv;
		} else if (which == 1) {
			u32 a0 = (rnd() & 0x100) | (rnd() & 1), ch = a0 & 0xff;

			for (k = 0; k < 704; k++)
				t32_data_da850[k] = blk_o[k] = (u8)rnd();
			for (k = 0; k < 1808; k++)
				t32_data_dc284[k] = dc_o[k] = (u8)rnd();
			for (k = 0; k < 1024; k++)
				t32_data_dd2d4[k] = ae_o[k] = (u8)rnd();
			t32_data_d03a0[0] = (u8)rnd();
			t32_data_d9008 = (rnd() & 1) ? 0 : rnd();
			vr = (u32)CALL(tisp_comn_param_array_get, a0, T32O_PTR(outv), T32O_PTR(lenv));
			orr = (u32)t32_comn_param_array_get(blk_o + ch * 336, dc_o + ch * 904, ae_o + ch * 232,
							    t32_data_d03a0[0], t32_data_d9008, outo, &lo);
			lv = *lenv;
			bad |= memcmp(t32_data_da850, blk_o, 704) != 0;
		} else {
			/* LUT infos with tables: vendor and ours each get the same images */
			u32 ch = rnd() % 4, mode = rnd() % 6, t;
			struct t32_lut_info *dv = t32o_alloc(64), *sv = t32o_alloc(64), *dO = t32o_alloc(64), *sO = t32o_alloc(64);
			u8 *tabv = t32o_alloc(14 * 80), *tabo = t32o_alloc(14 * 80);

			for (k = 0; k < 14 * 80; k++)
				tabv[k] = tabo[k] = (u8)rnd();
			for (t = 0; t < 7; t++) {
				sv->size[t] = sO->size[t] = (u16)(rnd() % 65);
				dv->size[t] = dO->size[t] = (u16)rnd();
				sv->tab[t] = T32O_PTR(tabv + 80 * t); sO->tab[t] = T32O_PTR(tabo + 80 * t);
				dv->tab[t] = T32O_PTR(tabv + 80 * (7 + t)); dO->tab[t] = T32O_PTR(tabo + 80 * (7 + t));
			}
			dv->rsvd = dO->rsvd = (u16)rnd();
			sv->rsvd = sO->rsvd = (u16)rnd();
			memset(t32_data_dc214, 0, sizeof(t32_data_dc214));
			t32_data_dc214[ch * 6 + mode] = T32O_PTR(sv);
			t32_data_dc274[ch] = T32O_PTR(dv);
			CALL(tisp_lut_params_copy, (rnd() & 0x300) | ch, (rnd() & 0x700) | mode, 0);
			t32_lut_params_copy(dO, sO);
			bad |= memcmp(tabv, tabo, 14 * 80) != 0 || memcmp(dv->size, dO->size, 16) || memcmp(sv->size, sO->size, 16);
		}
		bad |= vr != orr || lv != lo || memcmp(outv, outo, 600);
		compared++;
		if (bad && failed++ < 8)
			printf("MISMATCH case %u: ret %08x / %08x len %08x / %08x out %d\n", which, vr, orr, lv, lo,
			       memcmp(outv, outo, 600) != 0);
	}
	printf("t32_param_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
