/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) block init/deinit of CSC and CLM (openimp #99), binary oracle block "initA":
 *
 * - tisp_csc_init: register module 13, allocate the per-channel csc_para block (12 bytes: three 104/40/92
 *   byte buffers), copy the CSC section (tparamsP[ch] + 0x4570) into the working block, the built-in
 *   tables csc_cp / csc_ap into the other two, clear the API mode bytes 98/99, refresh by mode,
 *   update the register block, write the registers, set the frame-down interrupt.
 * - tisp_csc_deinit: free the three buffers, release module 13.
 * - tisp_clm_init: register module 19, allocate clm_info[ch] (3264 bytes, zeroed), point word 0 at the
 *   CLM payload (tparamsP[ch] + 0x903c), get the CSC block, store the init CT (par + 976), LUT-valid
 *   byte 3260 = 0xff, u16 at 2200 = 1, interpolate the CT (forced), refresh, write the registers,
 *   set the frame-done interrupt.
 *
 * Allocator, libc, printing, irq setup and the module functions ported elsewhere come in through the
 * ops struct (the oracle records them as CALLS).
 */
#ifndef TX_ISP_T33_INITA_H
#define TX_ISP_T33_INITA_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_cscP.h"

#define T33_CSC_ID_INIT		13
#define T33_CLM_ID_INIT		19
#define T33_CSC_SRC_BYTES	104
#define T33_CSC_DST_BYTES	40
#define T33_CSC_API_BYTES	92
#define T33_CSC_SEC_OFF		0x4570
#define T33_CLM_INFO_BYTES	3264
#define T33_CLM_PAYLOAD_OFF	0x903c

struct t33_initA_ops {
	void *(*vmalloc)(u32 n);
	void (*vfree)(void *p);
	void *(*memcpy)(void *d, const void *s, u32 n);
	void *(*memset)(void *d, int c, u32 n);
	int (*printk)(const char *fmt);
	int (*isp_printf)(int lvl, const char *fmt);
	int (*irq_set)(u32 ch, u32 irq, void *fn);
	int (*rcfg_register)(u32 ch, u32 mod, const void *tmpl);
	int (*rcfg_release)(u32 ch, u32 mod);
	int (*csc_write_reg)(u32 ch);
	int (*csc_refresh_by_mode)(u32 mode, u32 refresh, u8 *blk);
	int (*csc_update_para)(u32 ch);
	int (*csc_get)(u32 ch, u8 *out);
	int (*clm_ct_interp)(u32 ch, u32 ct, u32 force);
	int (*clm_calc_refresh)(u32 ch);
	int (*clm_write_reg)(u32 ch);
};

/* tisp_csc_init: returns 0 */
static inline int t33_csc_init(u32 a0, struct t33_cscp **csc_para, const u8 *const *tparamsP,
			       const u8 *csc_cp, const u8 *csc_ap, u8 *tispCscRefresh,
			       const struct t33_initA_ops *ops)
{
	u32 ch = a0 & 0xff;
	struct t33_cscp *p;
	int r;

	r = ops->rcfg_register(ch, T33_CSC_ID_INIT, NULL);
	*tispCscRefresh = 0;
	if (r < 0)
		ops->printk("tisp_csc_init error\n");
	p = ops->vmalloc(sizeof(*p));
	csc_para[ch] = p;
	p->src = ops->vmalloc(T33_CSC_SRC_BYTES);
	p->dst = ops->vmalloc(T33_CSC_DST_BYTES);
	p->api = ops->vmalloc(T33_CSC_API_BYTES);
	ops->memcpy(p->src, tparamsP[ch] + T33_CSC_SEC_OFF, T33_CSC_SRC_BYTES);
	ops->memcpy(p->dst, csc_cp, T33_CSC_DST_BYTES);
	ops->memcpy(p->api, csc_ap, T33_CSC_API_BYTES);
	p->src[98] = 0;
	p->src[99] = 0;
	ops->csc_refresh_by_mode(p->src[91], 0, p->src);
	ops->csc_update_para(ch);
	ops->csc_write_reg(ch);
	ops->isp_printf(0, "tisp_csc_init ok\n");
	ops->irq_set(ch, 48, NULL);
	return 0;
}

/* tisp_csc_deinit: returns 0 */
static inline int t33_csc_deinit(u32 a0, struct t33_cscp **csc_para, const struct t33_initA_ops *ops)
{
	u32 ch = a0 & 0xff;
	struct t33_cscp *p = csc_para[ch];
	int r;

	if (p) {
		if (p->dst) {
			ops->vfree(p->dst);
			p->dst = NULL;
		}
		if (p->api) {
			ops->vfree(p->api);
			p->api = NULL;
		}
		if (p->src) {
			ops->vfree(p->src);
			p->src = NULL;
		}
		ops->vfree(p);
		csc_para[ch] = NULL;
	}
	r = ops->rcfg_release(ch, T33_CSC_ID_INIT);
	if (r < 0)
		ops->printk("tisp_csc_deinit error\n");
	return 0;
}

/* tisp_clm_init: returns 0 */
static inline int t33_clm_initA(u32 a0, const u8 *par, u8 **clm_info, const u8 *const *tparamsP,
				const struct t33_initA_ops *ops)
{
	u32 ch = a0 & 0xff;
	u32 ct;
	u8 *inf;

	ops->rcfg_register(ch, T33_CLM_ID_INIT, NULL);
	inf = ops->vmalloc(T33_CLM_INFO_BYTES);
	clm_info[ch] = inf;
	ops->memset(inf, 0, T33_CLM_INFO_BYTES);
	*(u32 *)inf = (u32)(unsigned long)(tparamsP[ch] + T33_CLM_PAYLOAD_OFF);
	ops->csc_get(ch, inf + 4);
	ct = t33_le32(par + 976);
	inf[3260] = 0xff;
	*(u16 *)(inf + 2200) = 1;
	*(u32 *)(inf + 96) = ct;
	ops->clm_ct_interp(ch, ct, 1);
	ops->clm_calc_refresh(ch);
	ops->clm_write_reg(ch);
	ops->irq_set(ch, 53, NULL);
	ops->isp_printf(0, "tisp_clm_init ok\n");
	return 0;
}

#endif /* TX_ISP_T33_INITA_H */
