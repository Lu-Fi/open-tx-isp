/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 CSC / CSCCR helpers of the vendor module (openimp #99), binary oracle block "cscmisc"
 * (tests/t33_binoracle/cscmisc.conf):
 *
 * - tisp_t33_csc_write_reg: the CSC working block (csc_para[ch][1], the vendor's csc_cp, 0x28 bytes) into the
 *   list 13 slots 3..9 (registers 0xb040..0xb058 without the cfg mode), the 0xb010 trigger slot 1, then
 *   tispCscRefresh = 1 with the list idle (the next tisp_csc_fra_down_interrupt starts it), else 2 (the
 *   interrupt writes the slots again).
 * - tisp_csccr_bin_par_update: on the T33 it only checks the CSCCR ranges (no update_para): the section's
 *   ranges (+5..+0xc) when byte 0xd is 1, or 0 with byte 0xe (last setter) below 2, else the API ranges
 *   (csccrInfo + 0x14); -1 and a message when a minimum is above its maximum.
 *
 * tisp_sdns_other_params_transform is t33_sdns_other_params (tx_isp_t33_blk.h, used by t33_sdns_par).
 */
#ifndef TX_ISP_T33_CSC_H
#define TX_ISP_T33_CSC_H

#define T33_CSC_ID	13

struct t33_csc_ops {
	int (*reg_write)(u32 ch, u32 mod, u32 reg, u32 val);
	int (*dma_state)(u32 mod);
};

static inline u32 t33_csc_le16(const u8 *p)
{
	return p[0] | (u32)p[1] << 8;
}

static inline u32 t33_csc_reg(u8 cfg, u32 slot, u32 addr)
{
	return cfg ? slot : addr;
}

/* tisp_t33_csc_write_reg: cp = the working block, cfg = cscCfgMode, refresh = tispCscRefresh; returns 0 */
static inline int t33_csc_write_reg(u32 a0, const u8 *cp, u8 cfg, u8 *refresh, const struct t33_csc_ops *ops)
{
	u32 ch = a0 & 0xff;

	ops->reg_write(ch, T33_CSC_ID, t33_csc_reg(cfg, 3, 0xb040),
		       (cp[1] & 7U) << 4 | (cp[2] & 7U) << 8 | (cp[0] & 1U) | (t33_csc_le16(cp + 4) & 0x1ff) << 16);
	ops->reg_write(ch, T33_CSC_ID, t33_csc_reg(cfg, 4, 0xb044),
		       (t33_csc_le16(cp + 8) & 0x1ff) << 12 | (t33_csc_le16(cp + 6) & 0x1ff));
	ops->reg_write(ch, T33_CSC_ID, t33_csc_reg(cfg, 5, 0xb048),
		       (t33_csc_le16(cp + 0xc) & 0x3ff) << 10 | (u32)cp[0xe] << 20 | (t33_csc_le16(cp + 0xa) & 0x1ff));
	ops->reg_write(ch, T33_CSC_ID, t33_csc_reg(cfg, 6, 0xb04c),
		       (t33_csc_le16(cp + 0x12) & 0x1ff) << 10 | (t33_csc_le16(cp + 0x14) & 0x3ff) << 20 |
		       (t33_csc_le16(cp + 0x10) & 0x1ff));
	ops->reg_write(ch, T33_CSC_ID, t33_csc_reg(cfg, 7, 0xb050),
		       (t33_csc_le16(cp + 0x18) & 0x1ff) << 10 | (t33_csc_le16(cp + 0x1a) & 0x1ff) << 20 |
		       (t33_csc_le16(cp + 0x16) & 0x3ff));
	ops->reg_write(ch, T33_CSC_ID, t33_csc_reg(cfg, 8, 0xb054), t33_csc_le16(cp + 0x1c));
	ops->reg_write(ch, T33_CSC_ID, t33_csc_reg(cfg, 9, 0xb058), t33_csc_le16(cp + 0x1e) | t33_csc_le16(cp + 0x20) << 16);
	ops->reg_write(ch, T33_CSC_ID, t33_csc_reg(cfg, 1, 0xb010), 0xffffffff);
	*refresh = ops->dma_state(T33_CSC_ID) ? 2 : 1;
	return 0;
}

/* tisp_csccr_bin_par_update: par = the CSCCR section (20 bytes), api = the API ranges (csccrInfo + 0x14) */
static inline int t33_csccr_bin_par_update(const u8 *par, const u8 *api)
{
	const u8 *r = (par[0xd] == 1 || (par[0xd] == 0 && par[0xe] < 2)) ? par + 5 : api;

	if (r[1] < r[0] || r[3] < r[2] || r[5] < r[4] || r[7] < r[6]) {
		isp_printf(1, "\n CSCCR SET DATA ERROR ! MIN-VALUE > MAX-VALUE \n");
		return -1;
	}
	return 0;
}

#endif /* TX_ISP_T33_CSC_H */
