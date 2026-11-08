/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T30 Temper (3D denoise) attribute, IMAGE_TUNING_CID_TEMPER_ATTR 0x8000083 (Lu-Fi/openimp#202).
 *
 * The vendor libimp 1.0.5 enables Temper AUTO at EnableTuning when the sensor has no NCU bin
 * (EnableTuning 0xd76f4 -> SetTemperDnsAttr 0xd6520 -> tuning SET 0x8000083).  The SDK handler
 * apical_isp_temper_dns_s_attr (tx-isp-core-tuning.c) writes the Temper registers (kept in the
 * driver arm) and these system-table items (apical.h system_tab):
 *   11 global_manual_temper, 46 global_temper_threshold_target,
 *   47 global_maximum_temper_strength, 48 global_minimum_temper_strength.
 *   MANUAL: 11 = 1, 47 = max(47, s), 48 = min(48, s), 46 = s;
 *   AUTO:   47 = tuning temper_max, 48 = tuning temper_min (u8 stores), 11 = 0;  DISABLE: none.
 * apical_isp_temper_dns_g_attr: mode = bypass temper (0x40 bit 10) ? DISABLE : 11 ? MANUAL : AUTO,
 * strength = item 46.  Host test: tests/t30_temper_host_test.c (check_t30_temper.sh, mutations).
 */
#ifndef TX_ISP_T30_TEMPER_H
#define TX_ISP_T30_TEMPER_H

#define T30_TEMPER_DISABLE	0
#define T30_TEMPER_AUTO		1
#define T30_TEMPER_MANUAL	2

#define T30_STAB_MANUAL_TEMPER	11
#define T30_STAB_TEMPER_TARGET	46
#define T30_STAB_TEMPER_MAX	47
#define T30_STAB_TEMPER_MIN	48

/* system-table part of apical_isp_temper_dns_s_attr; the mode was checked (0..2) */
static inline void t30_temper_s_attr_stab(uint8_t *st, int mode, uint8_t strength,
					  uint32_t temper_max, uint32_t temper_min)
{
	if (mode == T30_TEMPER_MANUAL) {
		st[T30_STAB_MANUAL_TEMPER] = 1;
		if (strength > st[T30_STAB_TEMPER_MAX])
			st[T30_STAB_TEMPER_MAX] = strength;
		if (strength < st[T30_STAB_TEMPER_MIN])
			st[T30_STAB_TEMPER_MIN] = strength;
		st[T30_STAB_TEMPER_TARGET] = strength;
	} else if (mode == T30_TEMPER_AUTO) {
		st[T30_STAB_TEMPER_MAX] = (uint8_t)temper_max;
		st[T30_STAB_TEMPER_MIN] = (uint8_t)temper_min;
		st[T30_STAB_MANUAL_TEMPER] = 0;
	}
}

/* apical_isp_temper_dns_g_attr */
static inline void t30_temper_g_attr(const uint8_t *st, uint32_t reg40, int *mode,
				     uint8_t *strength)
{
	if (reg40 & 0x400)
		*mode = T30_TEMPER_DISABLE;
	else
		*mode = st[T30_STAB_MANUAL_TEMPER] ? T30_TEMPER_MANUAL : T30_TEMPER_AUTO;
	*strength = st[T30_STAB_TEMPER_TARGET];
}

#endif
