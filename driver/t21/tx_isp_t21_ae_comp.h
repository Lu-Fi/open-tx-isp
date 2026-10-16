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

#ifndef ACCESS_ONCE
#define ACCESS_ONCE(x) (*(volatile __typeof__(x) *)&(x))
#endif

#define T21_AE_COMP_NEUTRAL	128u
#define T21_AE_COMP_MIN		16u

/*
 * IMP_ISP_Tuning_SetBacklightComp (beyond vendor on T21, the stock driver
 * has none): level 0..10, 0 = off.  It lifts the AE luma target by 5 % per
 * level (+50 % at 10) on top of the brightness compensation, so a dark
 * subject in front of a bright background gets exposure.  0 is the identity.
 */
#define T21_BLC_MAX		10u
static uint32_t t21_blc_level;

static inline uint32_t t21_ae_comp_scale(uint32_t target, uint32_t comp)
{
	uint32_t t;
	uint32_t blc = ACCESS_ONCE(t21_blc_level);

	comp &= 0xff;
	if ((comp == T21_AE_COMP_NEUTRAL && !blc) || target == 0)
		return target;
	if (comp < T21_AE_COMP_MIN)
		comp = T21_AE_COMP_MIN;
	/* 32-bit arithmetic (no __udivdi3 in the module): an AE target is
	 * 1..255, anything larger is cut so the product cannot overflow */
	if (target > 65535)
		target = 65535;
	t = (target * comp * (100 + 5 * blc) +
	     T21_AE_COMP_NEUTRAL * 50) / (T21_AE_COMP_NEUTRAL * 100);
	if (t < 1)
		t = 1;
	if (t > 255)
		t = 255;
	return (uint32_t)t;
}

#endif
