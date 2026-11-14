/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 LDC init / DMA config / list load (vendor tx-isp-t33.ko: tisp_ldc_t33_init,
 * tisp_ldc_cfg_dma, tisp_ldc_cfg_load, tisp_func_ldc_t33_dma_cfg).
 *
 * The module statics the vendor functions use are passed in a t33_ldca_env, the
 * kernel services / register accessors as t33_ldca_ops, so the code is the same
 * in the driver and in the binary oracle (tests/t33_binoracle/ldcA.c).
 */
#ifndef TX_ISP_T33_LDCA_H
#define TX_ISP_T33_LDCA_H

#include "tx_isp_t33_blk.h"

#define T33_LDCA_MOD	25

struct t33_ldca_ops {
	void *(*kmalloc)(u32 size, u32 flags);		/* private_kmalloc */
	void (*memclr)(void *p, u32 n);			/* memset(p, 0, n) */
	int (*rcfg_register)(u32 ch);			/* tisp_rcfg_module_register(ch, 25, ldc reg_init) */
	void (*err_alloc)(void);
	void (*err_register)(int ret);
	void (*par_reset)(void *dst);			/* the 80-byte stLdcPar record of a channel */
	void (*reg_write)(u32 reg, u32 val);		/* system_reg_write */
	void (*reg_write33)(u32 ch, u32 mod, u32 slot, u32 val);	/* tisp_reg_write_t33 */
	void (*bypass_refresh)(u32 ch);			/* tisp_top_ldc_bypass_refresh */
	void (*err_block)(void);
};

struct t33_ldca_env {
	const struct t33_ldca_ops *ops;
	u32 **attr;		/* stLDCAttr: 4 pointers to 48-byte records */
	u32 *mem_info;		/* stMemInfo: 76 bytes per channel */
	u32 *dma_cfg;		/* st_dma_cfg: 92 bytes per channel */
	u8 *par;		/* stLdcPar: 80 bytes per channel */
	u8 *ldc_info;		/* LdcInfo (2 bytes) */
	u8 *refresh;		/* tispLdcRefresh */
	u8 cfg_mode;		/* ldcCfgMode */
	u32 init_par_flag;	/* stIspInitPar word at byte 904 */
	u16 max_w, max_h, max_ow, max_oh;
};

/* tisp_func_ldc_t33_dma_cfg */
static inline void t33_ldca_dma_cfg(const struct t33_ldca_env *e, u32 ch, u32 mode, u32 w, u32 h,
				    u32 ow, u32 oh, u32 base_a, u32 base_b)
{
	const struct t33_ldca_ops *o = e->ops;
	u32 *d, ow8, v, a, s;
	u32 cfg = e->cfg_mode;

	(void)oh;
	ch &= 0xff;
	mode &= 0xff;
	w &= 0xffff;
	h &= 0xffff;
	ow8 = ((ow & 0xffff) + 7) & ~7U;
	d = e->dma_cfg + ch * 23;
	d[16] = base_a;
	d[21] = (d[18] * d[19] * 3) >> 1;
	d[17] = base_b;
	o->reg_write(0x20080, 1);
	if (mode == 1) {
		if (e->init_par_flag == 1) {
			v = (h * 3) >> 1;
			v = ((v + 1) >> 1) << 1;
			o->reg_write(0x20088, v << 15 | v);
			a = (u32)((int)(w * h * 3) / 2);
		} else {
			v = ((h + 1) >> 1) << 1;
			o->reg_write(0x20088, v << 15 | v);
			a = w * h;
		}
	} else {
		v = ((h >> 1) + 1) & 0xfffe;
		o->reg_write(0x20088, v << 15 | v);
		a = ((((w + 7) >> 3) << 3) * h) >> 1;
	}
	o->reg_write(0x20090, a + base_a);
	o->reg_write(0x20054, 0x40034);
	o->reg_write(0x20060, w << 16 | w);
	o->reg_write(0x2008c, base_a);
	o->reg_write(0x21070, 1);
	o->reg_write(0x21064, 0x100);
	o->reg_write(0x21078, 0x400080);
	o->reg_write(0x21060, 0x35);
	o->reg_write(0x21048, 4);
	o->reg_write(0x21054, ow8 << 16 | ow8);
	o->reg_write(0x2107c, base_b);
	o->reg_write(0x21080, (ow8 << 7) + base_b);
	for (s = 0; s <= ch; s++) {
		o->reg_write33(s, T33_LDCA_MOD, cfg ? 17 : 0x20058, 0);
		o->reg_write33(s, T33_LDCA_MOD, cfg ? 18 : 0x2005c, 0);
		o->reg_write33(s, T33_LDCA_MOD, cfg ? 12 : 0x2104c, 0);
		o->reg_write33(s, T33_LDCA_MOD, cfg ? 13 : 0x21050, 0);
	}
}

/* tisp_ldc_cfg_dma */
static inline int t33_ldca_cfg_dma(const struct t33_ldca_env *e, u32 ch)
{
	const struct t33_ldca_ops *o = e->ops;
	const u32 *mi;
	u32 *p, v, w = e->max_w, h = e->max_h, ow = e->max_ow;
	u32 mode = e->ldc_info[1];

	ch &= 0xff;
	mi = e->mem_info + ch * 19;
	p = e->attr[ch];
	p[8] = mi[14];
	p[9] = mi[15];
	o->reg_write(0x2704c, mi[14]);
	o->reg_write(0x27050, w);
	o->reg_write(0x27054, p[9]);
	o->reg_write(0x27058, ow);
	if (mode == 1) {
		if (e->init_par_flag == 1) {
			o->reg_write(0x2705c, (u32)((int)(w * h * 3) / 2) + p[8]);
			v = h * 3 >> 1;
		} else {
			o->reg_write(0x2705c, w * h + p[8]);
			v = h;
		}
		v = ((v + 1) >> 1) << 1;
	} else {
		o->reg_write(0x2705c, (u32)((int)(w * h) / 2) + p[8]);
		v = ((h >> 1) + 1) & 0xfffe;
	}
	o->reg_write(0x27098, v << 15 | v);
	o->reg_write(0x2709c, 0x400080);
	o->reg_write(0x27060, w);
	o->reg_write(0x27064, (ow << 7) + p[9]);
	o->reg_write(0x27068, ow);
	t33_ldca_dma_cfg(e, ch, mode, w, h, ow, e->max_oh, p[8], p[9]);
	return 0;
}

#define T33_LDCA_SL(n, r)	(cfg ? (n) : (r))

/* tisp_ldc_cfg_load */
static inline int t33_ldca_cfg_load(const struct t33_ldca_env *e, u32 ch)
{
	const struct t33_ldca_ops *o = e->ops;
	u32 cfg = e->cfg_mode, c, a3;
	const u32 *p;

	ch &= 0xff;
	p = e->attr[ch];
	o->reg_write33(ch, T33_LDCA_MOD, T33_LDCA_SL(2, 0x27044), p[1]);
	o->reg_write33(ch, T33_LDCA_MOD, T33_LDCA_SL(3, 0x27048), p[2]);
	o->reg_write33(ch, T33_LDCA_MOD, T33_LDCA_SL(1, 0x27040), p[6] | p[7] << 16);
	if (p[6] != 2) {
		o->reg_write33(ch, T33_LDCA_MOD, T33_LDCA_SL(5, 0x2706c), p[10]);
		o->reg_write33(ch, T33_LDCA_MOD, T33_LDCA_SL(6, 0x27070), p[4]);
		switch (p[3]) {
		case 8:
			c = 0;
			break;
		case 16:
			c = 1;
			break;
		case 32:
			c = 2;
			break;
		case 64:
			c = 3;
			break;
		default:
			c = 4;
			o->err_block();
			break;
		}
		if (c < 4)
			o->reg_write33(ch, T33_LDCA_MOD, T33_LDCA_SL(7, 0x27074), c);
	}
	if (p[6] == 1) {
		e->refresh[ch] = 1;
		o->bypass_refresh(ch);
	} else {
		e->ldc_info[0] = 1;
		o->reg_write(0x27090, 1);
	}
	if (p[6] == 2 && (p[7] & ~2U) == 1)
		a3 = p[2] | 0x10000000;
	else
		a3 = p[2];
	o->reg_write33(ch, T33_LDCA_MOD, T33_LDCA_SL(15, 0x21084), a3);
	o->reg_write(0x27088, 0);
	o->reg_write(0x27084, 0xffffffff);
	o->reg_write(0x27030, 1);
	o->reg_write33(ch, T33_LDCA_MOD, T33_LDCA_SL(9, 0x27020), 0xffffffff);
	o->reg_write33(ch, T33_LDCA_MOD, T33_LDCA_SL(10, 0x27024), 1);
	return 0;
}
#undef T33_LDCA_SL

/* tisp_ldc_t33_init: info byte 44 == 1 allocates the attribute record and registers list 25 */
static inline int t33_ldca_init(const struct t33_ldca_env *e, u32 ch, u8 *info)
{
	const struct t33_ldca_ops *o = e->ops;
	void *a;
	int r;

	ch &= 0xff;
	if (info[44] == 1) {
		a = o->kmalloc(48, 208);
		e->attr[ch] = (u32 *)a;
		if (!a) {
			o->err_alloc();
			return -1;
		}
		o->memclr(a, 48);
		r = o->rcfg_register(ch);
		if (r < 0)
			o->err_register(r);
	}
	info[16] = info[12];
	info[17] = info[13];
	info[18] = info[14];
	info[19] = info[15];
	o->par_reset(e->par + ch * 80);
	return 0;
}

#endif /* TX_ISP_T33_LDCA_H */
