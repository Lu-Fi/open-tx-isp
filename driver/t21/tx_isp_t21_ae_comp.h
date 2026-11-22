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

/*
 * IMP_ISP_Tuning_SetAeComp (tuning CID 0x8000023, beyond vendor: the T21
 * 1.0.33 libimp has no such call).  Its 0..255 value (128 = neutral) is a
 * second factor on the same AE target: the effective compensation is
 * brightness x aecomp / 128, so either control alone behaves as before.
 */
static inline uint32_t t21_ae_comp_combine(uint32_t bright, uint32_t aecomp)
{
	uint32_t c;

	bright &= 0xff;
	aecomp &= 0xff;
	if (aecomp == T21_AE_COMP_NEUTRAL)
		return bright;
	if (bright == T21_AE_COMP_NEUTRAL)
		return aecomp;
	c = (bright * aecomp + T21_AE_COMP_NEUTRAL / 2) / T21_AE_COMP_NEUTRAL;
	return c > 255 ? 255 : c;
}

/* SetMaxAgain: a lower cap than before is applied at once (trig / force_trig,
 * tx_isp_t21_adr_oem.inc L_tisp_s_max_again); equal or higher keeps stock. */
static inline int t21_cap_lowered(uint32_t old_cap, uint32_t new_cap)
{
	return new_cap < old_cap;
}

#endif
