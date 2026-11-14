/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 CT / EV fan-out and the BCSH CT / EV triggers, vendor-equal to tx-isp-t31.ko
 * (H20221206a): tisp_ct_update 0x14de4, tisp_ev_update 0x14cd4, tisp_bcsh_ev_update 0x2a63c,
 * tisp_bcsh_ct_update 0x2a6d8 (openimp #532 / #533).
 *
 * tisp_ct_update: top bypass word 0xc read once; CCM CT update unless bit 9 (0x200), LSC unless bit 6
 * (0x40), BCSH unless bit 16 (0x10000).
 * tisp_ev_update: 0xc read first; nothing happens when (ev, aux) equals the stored pair (vendor ev_last);
 * order AWB, CCM, ADR (unless 0x80), defog (unless 0x800), BCSH, WDR (unless 0x8); then the pair is stored.
 * tisp_bcsh_ev/ct_update: keep the raw value and the compare value (ev >> 10 / ct); run tiziano_bcsh_update
 * when BCSH_real == 1 or |now - last| > threshold (unsigned), then BCSH_real = 0 and last = now.
 *
 * The callees are hooks: by default the vendor names with the vendor arguments (the binary oracle records
 * them, tests/t31_binoracle/ctev.conf); the driver defines T31_CTEV_HOOKS and the macros before including.
 */
#ifndef TX_ISP_T31_CTEV_H
#define TX_ISP_T31_CTEV_H

#ifndef T31_CTEV_HOOKS
extern uint32_t system_reg_read(uint32_t reg);
extern int tisp_ccm_ct_update(uint32_t a, uint32_t b);
extern int tisp_lsc_ct_update(uint32_t a, uint32_t b);
extern int tisp_bcsh_ct_update(uint32_t a, uint32_t b);
extern int tisp_awb_ev_update(uint32_t a, uint32_t b);
extern int tisp_ccm_ev_update(uint32_t a, uint32_t b);
extern int tisp_adr_ev_update(uint32_t a, uint32_t b);
extern int tisp_defog_ev_update(uint32_t a, uint32_t b);
extern int tisp_bcsh_ev_update(uint32_t a, uint32_t b);
extern int tisp_wdr_ev_update(uint32_t a, uint32_t b);
extern int tiziano_bcsh_update(void);
#define T31_CTEV_BYPASS()		system_reg_read(0xc)
#define T31_CT_CCM(a, b)		tisp_ccm_ct_update(a, b)
#define T31_CT_LSC(a, b)		tisp_lsc_ct_update(a, b)
#define T31_CT_BCSH(a, b)		tisp_bcsh_ct_update(a, b)
#define T31_EV_AWB(a, b)		tisp_awb_ev_update(a, b)
#define T31_EV_CCM(a, b)		tisp_ccm_ev_update(a, b)
#define T31_EV_ADR(a, b)		tisp_adr_ev_update(a, b)
#define T31_EV_DEFOG(a, b)		tisp_defog_ev_update(a, b)
#define T31_EV_BCSH(a, b)		tisp_bcsh_ev_update(a, b)
#define T31_EV_WDR(a, b)		tisp_wdr_ev_update(a, b)
#define T31_BCSH_UPDATE()		tiziano_bcsh_update()
#endif

static inline int t31_ct_update(uint32_t a, uint32_t b)
{
	uint32_t byp = T31_CTEV_BYPASS();

	if (!(byp & 0x200))
		T31_CT_CCM(a, b);
	if (!(byp & 0x40))
		T31_CT_LSC(a, b);
	if (!(byp & 0x10000))
		T31_CT_BCSH(a, b);
	return 0;
}

/* last[0..1]: the vendor ev_last pair (.bss, zero at load) */
static inline int t31_ev_update(uint32_t ev, uint32_t aux, uint32_t *last)
{
	uint32_t byp = T31_CTEV_BYPASS();

	if (last[0] == ev && last[1] == aux)
		return 0;
	T31_EV_AWB(ev, aux);
	T31_EV_CCM(ev, aux);
	if (!(byp & 0x80))
		T31_EV_ADR(ev, aux);
	if (!(byp & 0x800))
		T31_EV_DEFOG(ev, aux);
	T31_EV_BCSH(ev, aux);
	if (!(byp & 0x8))
		T31_EV_WDR(ev, aux);
	last[0] = ev;
	last[1] = aux;
	return 0;
}

/* raw: vendor BCSH_ev / BCSH_ct; now: cm_ev (ev >> 10) / cm_ct; real, last, thr: the BCSH_real block words */
static inline int t31_bcsh_trig(uint32_t now, int *real, uint32_t *last, uint32_t thr, uint32_t *nowp)
{
	uint32_t d;

	*nowp = now;
	if (*real != 1) {
		d = *last < now ? now - *last : *last - now;
		if (!(thr < d))
			return 0;
	}
	T31_BCSH_UPDATE();
	*real = 0;
	*last = *nowp;
	return 0;
}

static inline int t31_bcsh_ev_update(uint32_t ev, uint32_t *raw, uint32_t *now, int *real, uint32_t *last, uint32_t thr)
{
	*raw = ev;
	return t31_bcsh_trig(ev >> 10, real, last, thr, now);
}

static inline int t31_bcsh_ct_update(uint32_t ct, uint32_t *raw, uint32_t *now, int *real, uint32_t *last, uint32_t thr)
{
	*raw = ct;
	return t31_bcsh_trig(ct, real, last, thr, now);
}

#endif /* TX_ISP_T31_CTEV_H */
