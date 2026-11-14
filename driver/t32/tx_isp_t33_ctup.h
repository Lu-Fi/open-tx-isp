/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) tisp_ct_update: the AWB colour temperature fan-out (AWB
 * event 11).  On a new (ct, ct2) pair of channel ch the CCM, LSC, BCSH and
 * CLM CT updates run unless their top bypass bit (tisp_top_get_bypass) is
 * set: CCM 0x80, LSC 0x08, BCSH 0x4000, CLM 0x8000 (PRJ008 day and night
 * 0x00e4c098: all four skipped).  Then the pair is kept in last[0..1].
 *
 * The callees are hooks: by default the vendor names (the binary oracle
 * records them, tests/t33_binoracle/ctup.conf); the driver defines
 * T33_CTUP_HOOKS and the six macros before including this header.
 */
#ifndef TX_ISP_T33_CTUP_H
#define TX_ISP_T33_CTUP_H

#ifndef T33_CTUP_HOOKS
extern u32 tisp_top_get_bypass(u32 id);
extern int tisp_ccm_ct_update(u32 ch, u32 x, int ct, int ct2);
extern int tisp_lsc_ct_update(u32 ch, u32 x, int ct, int ct2);
extern int tisp_bcsh_ct_update(u32 ch, u32 x, int ct, int ct2);
extern int tisp_clm_ct_update(u32 ch, u32 x, int ct, int ct2);
#define T33_CTUP_BYPASS(ch)		tisp_top_get_bypass(ch)
#define T33_CTUP_CCM(ch, x, ct, c2)	tisp_ccm_ct_update(ch, x, ct, c2)
#define T33_CTUP_LSC(ch, x, ct, c2)	tisp_lsc_ct_update(ch, x, ct, c2)
#define T33_CTUP_BCSH(ch, x, ct, c2)	tisp_bcsh_ct_update(ch, x, ct, c2)
#define T33_CTUP_CLM(ch, x, ct, c2)	tisp_clm_ct_update(ch, x, ct, c2)
#endif

static inline int t33_ct_update(u32 ch, u32 x, int ct, int ct2, int *last)
{
	u32 byp;

	if (last[0] == ct && last[1] == ct2)
		return 0;
	byp = T33_CTUP_BYPASS(ch & 0xff);
	if (!(byp & 0x80))
		T33_CTUP_CCM(ch, x, ct, ct2);
	if (!(byp & 0x8))
		T33_CTUP_LSC(ch, x, ct, ct2);
	if (!(byp & 0x4000))
		T33_CTUP_BCSH(ch, x, ct, ct2);
	if (!(byp & 0x8000))
		T33_CTUP_CLM(ch, x, ct, ct2);
	last[0] = ct;
	last[1] = ct2;
	return 0;
}

#endif /* TX_ISP_T33_CTUP_H */
