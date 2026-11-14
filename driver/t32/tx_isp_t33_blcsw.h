/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 black level (BLC) runtime: gain-dependent update, parameter switch and
 * channel init of the vendor's tisp_blc_* functions.
 *
 * Per-channel state (the vendor's 85-byte blc_para[ch] block, pointers are 32 bit):
 *   sec  IQ section (+17640 into tparamsP[ch]); bytes 129/130 mode, 132 write-back flag,
 *        u16 manual levels at +118
 *   ip   { tgain, bayer & 0x1f }
 *   cp   8 channel pointers (32 bytes), five u16 black levels at +32, u16 gain at +42
 *   ap   u16 flag at +0, five u16 levels at +2 (auto/manual override block)
 */
#ifndef TX_ISP_T33_BLCSW_H
#define TX_ISP_T33_BLCSW_H

#include "tx_isp_t33_blk.h"

struct t33_blc_info {
	u8 *sec;
	u32 *ip;
	u8 *cp;
	u8 *sp;
	u8 *ap;
	u32 ipd[2];
	u32 cpd[11];
	u8 spd[1];
	u8 apd[12];
};

/* hardware / kernel services of tisp_blc_init, supplied by the caller */
struct t33_blc_ops {
	void (*write_reg)(u32 ch);		/* tisp_blc_write_reg */
	int (*module_register)(u32 ch);		/* tisp_rcfg_module_register(ch, 4, reg_init) */
	void *(*alloc)(u32 size);		/* private_vmalloc */
	void (*log_err)(u32 line, int val);	/* printk */
	void (*irq_set)(u32 ch);		/* system_irq_func_set(ch, 40, frame done) */
	const u8 *ip0, *cp0, *sp0, *ap0;	/* default contents (blc_ip, blc_cp, blc_sp, blc_ap) */
};

static inline void t33_blc_st16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

/* tisp_blc_interp_by_tgain */
static inline void t33_blc_interp(struct t33_blc_info *b, u32 tgain)
{
	u8 *sec = b->sec, *cp = b->cp + 32, *ap = b->ap;
	u32 mode = sec[129], sub = sec[130], i = tgain >> 16, f = tgain & 0xffff;
	u32 apen = t32_tp_le16(ap), max = 0, g, v, k;
	int skip = mode == 1 || (mode == 0 && sub < 2), done = 0;

	if (!skip && apen == 1) {
		for (k = 0; k < 5; k++)
			t33_blc_st16(cp + 2 * k, t32_tp_le16(ap + 2 + 2 * k));
	} else {
		if (t33_le32(sec + 4)) {
			for (k = 0; k < 5; k++)
				t33_blc_st16(cp + 2 * k, t32_tp_le16(sec + 118 + 2 * k));
		} else {
			for (k = 0; k < 5; k++)
				t33_blc_st16(cp + 2 * k, t32_tp_intp_u16(i, f, sec + 8 + 22 * k));
			if (!sec[132])
				for (k = 0; k < 5; k++)
					t33_blc_st16(sec + 118 + 2 * k, t32_tp_le16(cp + 2 * k));
		}
		if (apen != 1) {
			for (k = 0; k < 5; k++)
				t33_blc_st16(ap + 2 + 2 * k, t32_tp_le16(cp + 2 * k));
			done = 1;
		}
	}
	if (!done && sec[132] == 1 && (mode == 2 || (mode == 0 && sub == 2)))
		for (k = 0; k < 5; k++)
			t33_blc_st16(sec + 118 + 2 * k, t32_tp_le16(ap + 2 + 2 * k));
	for (k = 0; k < 5; k++) {
		v = t32_tp_le16(cp + 2 * k);
		if (v > max)
			max = v;
	}
	g = 0x1fff;
	if (!(max >> 11)) {
		g = 0x2000000U / (0x1000U - max);
		g = (g >> 1) + (g & 1);		/* tisp_round_int64(x, 0, 1) */
	}
	t33_blc_st16(cp + 10, g);
}

/* tisp_blc_tgain_update */
static inline int t33_blc_tgain_update(u32 ch, struct t33_blc_info *b, u32 tgain, void (*write_reg)(u32))
{
	t33_blc_interp(b, tgain);
	b->ip[0] = tgain;
	write_reg(ch);
	return 0;
}

/* tisp_blc_switch_para */
static inline int t33_blc_switch_para(u32 ch, struct t33_blc_info *b, u8 *tparams, void (*write_reg)(u32))
{
	b->sec = tparams + 17640;
	t33_blc_interp(b, b->ip[0]);
	write_reg(ch);
	return 0;
}

static inline void t33_blc_cpy(u8 *d, const u8 *s, u32 n)
{
	u32 k;

	for (k = 0; k < n; k++)
		d[k] = s[k];
}

/* tisp_blc_init (isp: the ISP init parameters, words at +4 (bayer) and +928 (gain)) */
static inline int t33_blc_init_ch(u32 ch, const u8 *isp, u8 *tparams, struct t33_blc_info **slot,
				  const struct t33_blc_ops *ops)
{
	/* channel slot (level index) of the eight cp pointers per Bayer order */
	static const u8 map[4][8] = {
		{ 0, 1, 0, 1, 2, 3, 2, 3 },
		{ 3, 2, 3, 2, 1, 0, 1, 0 },
		{ 1, 0, 1, 0, 3, 2, 3, 2 },
		{ 2, 3, 2, 3, 0, 1, 0, 1 },
	};
	struct t33_blc_info *b;
	u32 idx, k;
	int r;

	ch &= 0xff;
	r = ops->module_register(ch);
	if (r < 0) {
		ops->log_err(387, r);
		return r;
	}
	b = (struct t33_blc_info *)ops->alloc(85);
	*slot = b;
	if (!b) {
		ops->log_err(394, 0);
		return -12;
	}
	b->sec = tparams + 17640;
	b->ip = b->ipd;
	b->cp = (u8 *)b->cpd;
	b->sp = b->spd;
	b->ap = b->apd;
	t33_blc_cpy((u8 *)b->ip, ops->ip0, 8);
	t33_blc_cpy(b->cp, ops->cp0, 44);
	t33_blc_cpy(b->sp, ops->sp0, 1);
	t33_blc_cpy(b->ap, ops->ap0, 12);
	b->ip[0] = t33_le32(isp + 928);
	b->ip[1] = t33_le32(isp + 4) & 0x1f;
	b->sec[129] = 0;
	b->sec[130] = 0;
	t33_blc_st16(b->ap, 0);
	idx = t33_le32(isp + 4) & 0x1f;
	if (idx < 4)
		for (k = 0; k < 8; k++)
			((u32 *)b->cp)[k] = (u32)(unsigned long)(b->cp + 32 + 2 * map[idx][k]);
	t33_blc_interp(b, b->ip[0]);
	ops->write_reg(ch);
	ops->irq_set(ch);
	return 0;
}

#endif /* TX_ISP_T33_BLCSW_H */
