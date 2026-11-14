/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "ccmsm": the CCM / CSC small helpers of
 * driver/t32/tx_isp_t33_ccmsm.h with the vendor calling convention, built
 * for mipsel and run against the vendor tx-isp-t33.ko instructions.
 */
#include <stdint.h>
#include "tx_isp_t33_ccmsm.h"

/* module globals the vendor functions read (ccm_para / csc_para pointer arrays) */
u32 ccm_para[256];
u32 csc_para[256];

int bo_tisp_ccm_no_interp(u32 ch)
{
	u32 *state = (u32 *)(uintptr_t)ccm_para[ch];
	const u32 *par = (const u32 *)(uintptr_t)state[0];

	t33_ccm_no_interp(state, par);
	return 0;
}

unsigned long long bo_func_ccm_calc_single_ks(u32 a0, u32 a1, u32 a2, u32 a3)
{
	return t33_ccm_single_ks(a0, a1, a2, a3);
}

int bo_tisp_csc_get(u32 ch, u8 *out)
{
	const u32 *state = (const u32 *)(uintptr_t)csc_para[ch];
	const u8 *par = (const u8 *)(uintptr_t)state[0];
	const u8 *fb = (const u8 *)(uintptr_t)state[2];

	return t33_csc_get(par, fb, out);
}
