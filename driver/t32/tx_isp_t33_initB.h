/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) block init/deinit glue for CCM and CLM, from the vendor
 * tx-isp-t33.ko:
 *  - tisp_ccm_init: register module 10, allocate the 656-byte block state
 *    (ccm_para[ch]), copy the ccm_sp / ccm_ip / ccm_ap defaults, take the
 *    section pointer from tparamsP[ch] + 21536, the CT / EV from the init
 *    parameters (+976 / +920 / +924), then interp_by_ct, interp_by_ev
 *    (forced), no_interp, calc_para_refresh, write_reg and the frame-done
 *    IRQ hook;
 *  - tisp_ccm_deinit / tisp_clm_deinit: free the block state and release
 *    module 10 / 19.
 */
#ifndef TX_ISP_T33_INITB_H
#define TX_ISP_T33_INITB_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_ccm.h"
#include "tx_isp_t33_clm.h"

#define T33_CCM_PARA_BYTES	656U
#define T33_CCM_SEC_OFF		21536U
#define T33_CCM_IRQ_LINE	45U

/* vendor module data objects */
extern u32 tparamsP[4];
extern u32 ccm_para[4];
extern u32 clm_info[4];

/* ccm_sp (identity matrix, 36 bytes) / ccm_ip (16) / ccm_ap (40) defaults */
static const u8 t33_ccm_sp[36] = {
	0x00, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0x00, 0x04, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x04, 0, 0,
};
static const u8 t33_ccm_ip[16] = { 0 };
static const u8 t33_ccm_ap[40] = { 0 };

/* callees recorded by the oracle / proven elsewhere */
extern int tisp_rcfg_module_register(u32 ch, u32 mod, void *fn);
extern int tisp_rcfg_module_release(u32 ch, u32 mod);
extern void *private_vmalloc(u32 n);
extern void private_vfree(void *p);
extern int tisp_ccm_interp_by_ct(u32 ch, u32 ct);
extern int tisp_ccm_interp_by_ev(u32 ch, u32 a1, u32 lo, u32 hi, u32 force);
extern void tisp_ccm_no_interp(u32 ch);
extern void tisp_ccm_calc_para_refresh(u32 ch);
extern void tisp_ccm_write_reg(u32 ch, u32 slot);
extern int system_irq_func_set(u32 ch, u32 line, void *fn);
extern int isp_printf(u32 lvl, const char *fmt, ...);
#ifndef __KERNEL__
extern int printk(const char *fmt, ...);
#endif

/* real call (not expanded inline), as the vendor's memcpy */
static inline void t33_initb_cpy(void *dst, const void *src, unsigned int n)
{
	__asm__ volatile("" : "+r"(n));
	memcpy(dst, src, n);
}

/* tisp_ccm_init (always returns 0) */
static inline int t33_ccm_initB(u32 ch, const u8 *initpar)
{
	u32 *v;
	u32 lo, hi, ct;
	int r;

	r = tisp_rcfg_module_register(ch & 0xff, T33_MOD_CCM, NULL);
	if (r < 0)
		printk("tisp_ccm_init: module register failed %d\n", r);

	v = private_vmalloc(T33_CCM_PARA_BYTES);
	v[0] = tparamsP[ch] + T33_CCM_SEC_OFF;
	ccm_para[ch] = (u32)(unsigned long)v;
	t33_initb_cpy((u8 *)v + 336, t33_ccm_sp, 36);
	t33_initb_cpy((u8 *)v + 8, t33_ccm_ip, 16);
	t33_initb_cpy((u8 *)v + 612, t33_ccm_ap, 40);
	v = (u32 *)(unsigned long)ccm_para[ch];
	ct = t33_le32(initpar + 976);
	v[4] = ct;
	v[5] = 0xff;
	lo = t33_le32(initpar + 920);
	hi = t33_le32(initpar + 924);
	v[2] = (hi << 22) | (lo >> 10);
	v[3] = hi >> 10;
	*(u8 *)(unsigned long)(v[0] + 1882) = 0;
	*(u8 *)(unsigned long)(v[0] + 1883) = 0;
	tisp_ccm_interp_by_ct(ch, v[4]);
	tisp_ccm_interp_by_ev(ch, v[4], v[2], v[3], 1);
	tisp_ccm_no_interp(ch & 0xff);
	tisp_ccm_calc_para_refresh(ch);
	tisp_ccm_write_reg(ch, 0);
	isp_printf(0, "ccm init\n", 0x135181);
	system_irq_func_set(ch & 0xff, T33_CCM_IRQ_LINE, NULL);
	return 0;
}

/* tisp_ccm_deinit: free ccm_para[ch] and release module 10 */
static inline int t33_ccm_deinit(u32 ch)
{
	void *p = (void *)(unsigned long)ccm_para[ch];

	if (p) {
		private_vfree(p);
		ccm_para[ch] = 0;
	}
	if (tisp_rcfg_module_release(ch & 0xff, T33_MOD_CCM) < 0)
		printk("tisp_ccm_deinit: module release failed\n");
	return 0;
}

/* tisp_clm_deinit: free clm_info[ch] and release module 19 */
static inline int t33_clm_deinit(u32 ch)
{
	void *p = (void *)(unsigned long)clm_info[ch];

	if (p) {
		private_vfree(p);
		clm_info[ch] = 0;
	}
	if (tisp_rcfg_module_release(ch & 0xff, T33_MOD_CLM) < 0)
		printk("tisp_clm_deinit: module release failed\n");
	return 0;
}

#endif /* TX_ISP_T33_INITB_H */
