/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 csccr / bcsh init and deinit (vendor tisp_csccr_init, tisp_csccr_deinit,
 * tisp_bcsh_deinit).
 *
 * The callees (tisp_rcfg_module_register / _release, tisp_rcfg_csccr_reg_init,
 * tisp_reg_write_t33, tisp_csccr_para_refresh_by_mode, tisp_csccr_bin_par_update,
 * tisp_csccr_t33_frame_done_interrupt, system_irq_func_set, lib_tisp_set_csccr,
 * private_kmalloc / private_kfree / private_vfree) are ported / proven
 * elsewhere and are declared by the includer.  The deinit functions return the
 * release result, or printk's result when the release failed (vendor tail call).
 */
#ifndef TX_ISP_T33_INITC_H
#define TX_ISP_T33_INITC_H

#include "tx_isp_t33_blk.h"

#define T33_CSCCR_MOD		22
#define T33_BCSH_MOD		18
#define T33_CSCCR_INFO_BYTES	28U
#define T33_CSCCR_CP_OFF	16U
#define T33_CSCCR_SEC_OFF	17880U

#ifndef T33_INITC_GLUE
extern int tisp_rcfg_module_register(u32 ch, u32 mod, void *reg_init);
extern int tisp_rcfg_module_release(u32 ch, u32 mod);
extern int tisp_rcfg_csccr_reg_init(u32 ch, u32 list, u32 slot, u32 val);
extern int tisp_reg_write_t33(u32 ch, u32 list, u32 slot, u32 val);
extern int tisp_csccr_para_refresh_by_mode(u32 mode, u32 one, u8 *dest);
extern int tisp_csccr_bin_par_update(u32 ch);
extern int tisp_csccr_t33_frame_done_interrupt(u32 ch);
extern int system_irq_func_set(u32 ch, u32 irq, void *fn);
extern int lib_tisp_set_csccr(u32 ch, u32 night);
extern void *private_kmalloc(u32 size, u32 flags);
extern void private_kfree(void *p);
extern void private_vfree(void *p);
#endif
#ifndef __KERNEL__
extern int printk(const char *fmt, ...);
#endif

extern u8 t33_csccr_cp[12];
extern u8 t33_csccr_ap[12];

static inline void t33_initC_copy12(u8 *d, const u8 *s)
{
	unsigned int i;

	for (i = 0; i < 12; i++)
		d[i] = s[i];
}

/* tisp_csccr_init */
static inline int t33_csccr_init(u32 ch, u32 **csccr_info, u8 **tparams,
				 u8 *cfg_mode, u8 *refresh, u32 *output, const u8 *start_night)
{
	u32 idx = ch & 0xff;
	u32 *info;
	u8 *sec;
	const u8 *b;
	u32 q0, q1, s3, s5;
	u8 b8, b9, b10;
	int r;

	r = tisp_rcfg_module_register(idx, T33_CSCCR_MOD, (void *)tisp_rcfg_csccr_reg_init);
	if (r < 0)
		printk("%s:%d: module register failed %d\n", "tisp_csccr_init", 461, r);
	info = private_kmalloc(T33_CSCCR_INFO_BYTES, 208);
	csccr_info[idx] = info;
	info[3] = (u32)(tparams[idx] + T33_CSCCR_SEC_OFF);
	t33_initC_copy12((u8 *)info, t33_csccr_cp);
	t33_initC_copy12((u8 *)info + T33_CSCCR_CP_OFF, t33_csccr_ap);
	sec = (u8 *)info[3];
	sec[13] = 0;
	sec[14] = 0;
	tisp_csccr_para_refresh_by_mode(sec[4], 0, sec);
	tisp_csccr_bin_par_update(idx);

	info = csccr_info[idx];
	b = (const u8 *)info;
	q0 = (u16)t32_tp_le16(b + 0);
	q1 = (u16)t32_tp_le16(b + 4);
	b8 = b[8];
	b9 = b[9];
	b10 = b[10];
	s3 = (u32)(int)(short)(u16)t32_tp_le16(b + 2);
	s5 = (u32)(int)(short)(u16)t32_tp_le16(b + 6);

	tisp_reg_write_t33(idx, T33_CSCCR_MOD, *cfg_mode ? 3 : 0x14040, q1 & 0x7ff);
	tisp_reg_write_t33(idx, T33_CSCCR_MOD, *cfg_mode ? 4 : 0x14044, b10 << 8 | b8 << 16 | b9);
	tisp_reg_write_t33(idx, T33_CSCCR_MOD, *cfg_mode ? 6 : 0x14050, q0 & 0x7ff);
	tisp_reg_write_t33(idx, T33_CSCCR_MOD, *cfg_mode ? 7 : 0x14054, (s3 & 0x1ff) << 16 | (s5 & 0x1ff));
	tisp_reg_write_t33(idx, T33_CSCCR_MOD, *cfg_mode ? 1 : 0x14010, 3);

	*refresh = 1;
	output[idx] = ((u32)(u16)t32_tp_le16(b + 2) & 0x1ff) << 16 |
		      ((u32)(u16)t32_tp_le16(b + 6) & 0x1ff);
	system_irq_func_set(idx, 57, (void *)tisp_csccr_t33_frame_done_interrupt);
	lib_tisp_set_csccr(idx, start_night[idx]);
	return 0;
}

/* tisp_csccr_deinit */
static inline int t33_csccr_deinit(u32 ch, u32 **csccr_info, u8 *start_night, u32 *frame_count)
{
	u32 idx = ch & 0xff;
	u32 *info = csccr_info[idx];
	int r;

	start_night[0] = 0;
	start_night[1] = 0;
	start_night[2] = 0;
	start_night[3] = 0;
	if (info) {
		private_kfree(info);
		csccr_info[idx] = 0;
	}
	frame_count[idx] = 0;
	r = tisp_rcfg_module_release(idx, T33_CSCCR_MOD);
	if (r < 0)
		return printk("%s:%d: module release failed %d\n", "tisp_csccr_deinit", 520, r);
	return r;
}

/* tisp_bcsh_deinit */
static inline int t33_bcsh_deinit(u32 ch, void **bcsh_para, u32 **bcsh_info)
{
	u32 idx = ch & 0xff;
	void *p = bcsh_para[idx];
	int r;

	if (p) {
		private_vfree(p);
		bcsh_para[idx] = 0;
	}
	if (bcsh_info[idx]) {
		private_kfree(bcsh_info[idx]);
		bcsh_info[idx] = 0;
	}
	r = tisp_rcfg_module_release(idx, T33_BCSH_MOD);
	if (r < 0)
		return printk("%s:%d: module release failed %d\n", "tisp_bcsh_deinit", 1908, r);
	return r;
}

#endif /* TX_ISP_T33_INITC_H */
