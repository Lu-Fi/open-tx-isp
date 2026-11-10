/* SPDX-License-Identifier: GPL-2.0 */
/*
 * d03a0 = 1 LUT check shared by the oracles (include after the driver header that defines struct t32_front_write).
 * In shadow mode the vendor sends the data words of a LUT port into the register shadow image of channel 0
 * (word ctl + 2 + k for the k-th data word of the port); the control write (first) and the end write (second)
 * of the port stay tagged writes, logged with the image slots ctl and ctl + endoff.  Every other write stays a
 * tagged write in the same order, and no other image word may change (before: the image before the call, NULL =
 * the arming pattern of t32o_shadow_arm).  ours = the direct-mode write list.
 */
#ifndef T32O_SHADOW_H
#define T32O_SHADOW_H
#include <string.h>
#include "t32_oracle_env.h"

static inline int t32o_lut_shadow_check(const struct t32_front_write *ow, int on, unsigned int nports,
					const u32 (*ports)[2], const u32 *ctl, u32 endoff,
					const u32 *img, u32 words, const u32 *before)
{
	static u32 exp[1u << 16];
	unsigned int cnt[4][2] = { { 0 } }, nv = 0, p;
	int k;
	u32 i;

	if (words > (1u << 16) || nports > 4)
		return 1;
	for (i = 0; i < words; i++)
		exp[i] = before ? before[i] : 0xa5a50000u ^ i;
	for (k = 0; k < on; k++) {
		for (p = 0; p < nports; p++)
			if (ow[k].reg == ports[p][1])
				break;
		if (p < nports) {
			if (ctl[p] + 2 + cnt[p][1] >= words)
				return 1;
			exp[ctl[p] + 2 + cnt[p][1]++] = ow[k].val;
			continue;
		}
		for (p = 0; p < nports; p++)
			if (ow[k].reg == ports[p][0])
				break;
		if (p < nports) {	/* control word at the image slot ctl, end word at ctl + endoff */
			u32 want = 0x80000000u | (ctl[p] + (cnt[p][0]++ ? endoff : 0));

			if (nv >= t32o_log.n || (t32o_log.w[nv].reg & 0x80ffffffu) != want ||
			    t32o_log.w[nv].val != ow[k].val)
				return 1;
		} else if (nv >= t32o_log.n || !t32o_reg_eq(t32o_log.w[nv].reg, ow[k].reg) ||
			   t32o_log.w[nv].val != ow[k].val) {
			return 1;
		}
		nv++;
	}
	return nv != t32o_log.n || memcmp(img, exp, words * 4);
}
#endif
