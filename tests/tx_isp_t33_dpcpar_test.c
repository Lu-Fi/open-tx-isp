/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 DPC gain update on the vendor-form ports (openimp #99 group 13c): driver/t32/tx_isp_t33_dpcpar.h
 * (tisp_dpc_bin_para_update, func_dpc / ctr / cac_para_update, oracle dpcpar) with tisp_dpc_l_gain_update's gain
 * interpolations give the same dpcInfo words 0..169 and the same list 2 as the init builder t33_dpc_init that the
 * gain refresh ran before, for random sections, gains and a block left at another gain (words 170 / 171 =
 * section / gain).
 */
#define TX_ISP_T33
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../driver/t32/tx_isp_t32_rcfg33.h"
#include "../driver/t32/tx_isp_t32_iq.h"
#include "../driver/t32/tx_isp_t33_blk.h"
#include "../driver/t32/tx_isp_t33_dpc.h"
#include "../driver/t32/tx_isp_t33_dpcpar.h"

#define CAP 256

int main(void)
{
	static u8 sec[T33_DPC_SEC_BYTES];
	static u32 v1[T33_DPC_WORDS], v2[T33_DPC_WORDS], l1[CAP], l2[CAP];
	unsigned int n, k, bad = 0;

	srand(1);
	for (n = 0; n < 2000; n++) {
		u32 g0 = (u32)rand() << 1 ^ (u32)rand(), g = n % 7 ? ((u32)rand() & 0x7fffff) : (u32)(n & 0xff) << 16;

		for (k = 0; k < sizeof(sec); k++)
			sec[k] = (u8)rand();
		sec[0xe] = (u8)(n % 6);	/* the correction level table */
		g0 &= 0x7fffff;
		t33_dpc_init(sec, g0, v2, l2, CAP);	/* the block at another gain */
		v2[T33_DPC_SEC_WORD] = (u32)(unsigned long)sec;
		v2[T33_DPC_GAIN_WORD] = g;
		t33_dpc_bin_para_v(v2);
		t33_vb_intp(v2, sec, g, t33_dpc_intp, ARRAY_SIZE(t33_dpc_intp));
		t33_dpc_para_v(v2);
		t33_ctr_para_v(v2);
		t33_cac_para_v(v2);
		t33_dpc_init(sec, g, v1, l1, CAP);
		memset(l2, 0, sizeof(l2));
		t33_rcfg_tmpl_init(l2, CAP, t33_dpc_tmpl, ARRAY_SIZE(t33_dpc_tmpl));
		t33_dpc_store(v2, l2);
		for (k = 0; k < T33_DPC_SEC_WORD; k++)
			if (v1[k] != v2[k]) {
				if (bad++ < 5)
					printf("FAIL case %u word 0x%x: %#x / %#x\n", n, k, v1[k], v2[k]);
			}
		if (memcmp(l1, l2, sizeof(l1)) && bad++ < 5)
			printf("FAIL case %u: list 2\n", n);
	}
	printf("tx_isp_t33_dpcpar_test: %u cases, %u failures\n", n, bad);
	return bad != 0;
}
