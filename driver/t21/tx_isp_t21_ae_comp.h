/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T21 brightness (IMP_ISP_Tuning_SetBrightness, beyond vendor).
 *
 * oem-t21.ko only stores the 0..255 brightness in ae_compensation (and
 * custom_eff[0]); the stock AE never reads it, so SetBrightness was a no-op.
 * Here it works like an exposure compensation: the AE luma target that
 * tisp_ae_target returns is scaled by ae_compensation / 128.  128 (the
 * default) is the identity, bit-identical to stock.  The ISP colour path is
 * untouched, so colours do not clip: only the exposure the AE settles on
 * moves.  Very low values are held at 16/128 (a target of about 1/8) and
 * the result stays in 1..255 (AE luma range).
 */
#ifndef TX_ISP_T21_AE_COMP_H
#define TX_ISP_T21_AE_COMP_H

#include <linux/types.h>

#define T21_AE_COMP_NEUTRAL	128u
#define T21_AE_COMP_MIN		16u

static inline uint32_t t21_ae_comp_scale(uint32_t target, uint32_t comp)
{
	uint64_t t;

	comp &= 0xff;
	if (comp == T21_AE_COMP_NEUTRAL || target == 0)
		return target;
	if (comp < T21_AE_COMP_MIN)
		comp = T21_AE_COMP_MIN;
	t = ((uint64_t)target * comp + T21_AE_COMP_NEUTRAL / 2) /
	    T21_AE_COMP_NEUTRAL;
	if (t < 1)
		t = 1;
	if (t > 255)
		t = 255;
	return (uint32_t)t;
}

#endif
