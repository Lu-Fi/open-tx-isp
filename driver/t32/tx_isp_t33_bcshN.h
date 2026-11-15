/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) BCSH block init (openimp #99): vendor tisp_bcsh_init.
 *
 * Registers list 18, allocates the block state (bcsh_para[ch], 656 bytes)
 * and bcshInfo[ch] (178 bytes), fills the state from the module defaults
 * (bcsh_ip / bcsh_cp / bcsh_sp / bcsh_ap) and the init parameters, clears
 * ten flags of the IQ section (tparamsP[ch] + 0x8b78), runs the interpolation
 * callees and writes the register list (tisp_reg_write_t33) when the list DMA
 * is idle, else only flags a refresh.  The callees are ported elsewhere; the
 * register writes are t33_bcshk_write_all (tx_isp_t33_bcshK.h).
 */
#ifndef TX_ISP_T33_BCSHN_H
#define TX_ISP_T33_BCSHN_H

#include "tx_isp_t33_bcshK.h"

#define T33_BCSHN_ID	0x01350181U

/* module data */
extern u8 *tparamsP[];
extern u8 bcsh_ip[40];
extern u8 bcsh_cp[300];
extern u8 bcsh_sp[112];
extern u8 bcsh_ap[144];

/* callees (vendor names) */
extern u32 tisp_rcfg_bcsh_reg_init(u32 *e);
extern int tisp_rcfg_module_register(u32 ch, u32 id, u32 (*fn)(u32 *));
extern void *private_vmalloc(unsigned long size);
extern void *private_kmalloc(unsigned long size, unsigned int flags);
extern void private_spin_lock_init(void *lock);
extern int isp_printf(int level, const char *fmt, ...);
extern int tisp_csc_get(u32 ch, u8 *out);
extern int tisp_bcsh_get_new_para(u8 *sec);
extern int tisp_bcsh_interp_by_ct(u32 ch, u32 a1, u32 a2, u32 a3, u32 force);
extern int tisp_bcsh_irq_frame_down(void);
extern int system_irq_func_set(u32 ch, u32 id, int (*fn)(void));
#ifndef __KERNEL__
extern int printk(const char *fmt, ...);
#endif

static inline void t33_bcshN_put32(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
	p[off + 2] = (u8)(v >> 16);
	p[off + 3] = (u8)(v >> 24);
}

/* tisp_bcsh_init(ch, par): par the init parameter block (words at 920, 924, 976) */
static inline int t33_bcshN_init(u32 ch_arg, const u8 *par)
{
	u32 ch = ch_arg & 0xff, ev_lo, ev_hi, a1;
	u8 *st, *sec;
	int r;

	r = tisp_rcfg_module_register(ch, 18, tisp_rcfg_bcsh_reg_init);
	if (r < 0)
		printk("tisp_bcsh_init: module register failed %d\n", r);

	st = (u8 *)private_vmalloc(656);
	bcsh_para[ch] = st;
	memset(st, 0, 656);
	bcshInfo[ch] = private_kmalloc(178, 208);
	if (!bcshInfo[ch])
		isp_printf(2, "tisp_bcsh_init: no info %s %d\n", __func__, 1828);
	memset(bcshInfo[ch], 0, 178);

	sec = tparamsP[ch] + 0x8b78;
	*(u8 **)st = sec;
	if (t33_le32(sec) != T33_BCSHN_ID)
		isp_printf(1, "tisp_bcsh_init: bad version %s %d %x %x\n", __func__, 1834, T33_BCSHN_ID,
			   t33_le32(sec));

	memcpy(st + 8, bcsh_ip, 40);
	memcpy(st + 48, bcsh_cp, 300);
	memcpy(st + 348, bcsh_sp, 112);
	memcpy(st + 460, bcsh_ap, 144);
	t33_bcshN_put32(st, 24, t33_le32(par + 984));
	t33_bcshN_put32(st, 28, t33_le32(par + 988));
	private_spin_lock_init(st + 652);
	tisp_csc_get(ch, st + 504);

	sec[1198] = 0;
	sec[1199] = 0;
	sec[1143] = 0;
	sec[1144] = 0;
	sec[1137] = 0;
	sec[1138] = 0;
	sec[1192] = 0;
	sec[1193] = 0;
	sec[1187] = 0;
	sec[1188] = 0;

	ev_lo = t33_le32(par + 920);
	ev_hi = t33_le32(par + 924);
	t33_bcshN_put32(st, 8, ev_lo >> 10 | ev_hi << 22);
	t33_bcshN_put32(st, 12, ev_hi >> 10);
	t33_bcshN_put32(st, 32, t33_le32(par + 976));
	t33_bcshN_put32(st, 36, 255);
	st[40] = 0;
	st[41] = 0;
	st[43] = 0;

	tisp_bcsh_get_new_para(sec);
	/* the vendor leaves a1 as it was (st + 604) */
	a1 = (u32)(unsigned long)(st + 604);
	tisp_bcsh_interp_by_ev(ch, a1, t33_le32(st + 8), t33_le32(st + 12), 1);
	tisp_bcsh_interp_by_ct(ch, a1, t33_le32(st + 32), 0, 1);
	tisp_bcsh_calc_para_refresh(ch, 0);
	tisp_params_transform(ch);

	if (tisp_rcfg_get_dma_state(18)) {
		tispBcshRefresh = 2;
	} else {
		t33_bcshk_write_all(ch);
		tispBcshRefresh = 1;
	}
	system_irq_func_set(ch, 52, tisp_bcsh_irq_frame_down);
	isp_printf(0, "tisp_bcsh_init: version %x\n", T33_BCSHN_ID);
	return 0;
}

#endif /* TX_ISP_T33_BCSHN_H */
