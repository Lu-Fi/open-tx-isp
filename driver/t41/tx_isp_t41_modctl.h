/* SPDX-License-Identifier: MIT */
#ifndef TX_ISP_T41_MODCTL_H
#define TX_ISP_T41_MODCTL_H
/*
 * IMP_ISP_Tuning_SetModuleControl (control 0x08000072).  Stock
 * tisp_s_module_control keeps the upper seven reserved bits of the TOP
 * bypass word, stores the 25-bit key from the caller and writes it to
 * TOP 0x40; tisp_g_module_control returns the low 25 bits.  Bit 1 =
 * bypass the module.
 *
 * The open driver also restores calibrated bypass bits from its refresh
 * paths, so a key bit that differs from the calibration default is kept
 * as an override (mask/value) that the restore re-applies.  Only the
 * modules that were device-tested are accepted; a key that changes any
 * other bit is refused instead of being acknowledged.
 */
#define T41_MODCTL_KEY_MASK 0x01ffffffU
/* LSC 1, DPC 4, ADR 7, CCM 9, GAMMA 10, DEFOG 11, MDNS 13, YDNS 14,
 * BCSH 15, YSP 17, SDNS 18 */
#define T41_MODCTL_SUPPORTED \
	((1U<<1)|(1U<<4)|(1U<<7)|(1U<<9)|(1U<<10)|(1U<<11)|(1U<<13)| \
	 (1U<<14)|(1U<<15)|(1U<<17)|(1U<<18))

/* calib: 32 bytes (0/1) of calibrated bypass per bit, or NULL.
 * Returns 0 and the new TOP word / override state, or -1 (unsupported). */
static inline int t41_modctl_plan(unsigned int old_top, unsigned int key,
		const unsigned char *calib, unsigned int ovr_mask,
		unsigned int ovr_value, unsigned int *new_top,
		unsigned int *new_mask, unsigned int *new_value)
{
	unsigned int cur = old_top & T41_MODCTL_KEY_MASK, diff, bit;
	unsigned int mask = ovr_mask, value = ovr_value;

	if (key & ~T41_MODCTL_KEY_MASK)
		return -1;
	diff = cur ^ key;
	if (diff & ~T41_MODCTL_SUPPORTED)
		return -1;
	for (bit = 0; bit < 25; ++bit) {
		unsigned int b = 1U << bit;
		if (!(diff & b))
			continue;
		if (calib && calib[bit] <= 1 && (unsigned int)calib[bit] == !!(key & b)) {
			mask &= ~b;		/* back at the calibrated state */
			value &= ~b;
		} else {
			mask |= b;
			value = (value & ~b) | (key & b);
		}
	}
	*new_top = (old_top & ~T41_MODCTL_KEY_MASK) | key;
	*new_mask = mask;
	*new_value = value;
	return 0;
}
#endif
