/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) channel context set-up: tisp_awb_init and tisp_defog_init.
 *
 * Both functions allocate their state blocks, fill the defaults, derive a few
 * geometry values and call out to other blocks.  The allocations, memset /
 * memcpy and every call to another function go through t33_i9_ops, so the
 * caller (driver or binary oracle) supplies them.
 *
 * Note: MIPS div / divu by zero is undefined; the emulated core divides by 1,
 * which the helpers below mirror.
 */
#ifndef TX_ISP_T33_I9F_H
#define TX_ISP_T33_I9F_H

#include "tx_isp_t33_blk.h"

enum {
	T33_I9_RCFG,		/* tisp_rcfg_module_register(ch, module) */
	T33_I9_ERR,		/* isp_printf(2, ...) */
	T33_I9_AWB_MALLOC_INFO,	/* tisp_awb_malloc_info(ch) */
	T33_I9_AWB_PARAMS,	/* tisp_awb_params_refresh(ch, par) */
	T33_I9_AWB_SHOW,	/* tisp_awb_show_para_update(ch) */
	T33_I9_EVENT,		/* tisp_event_set_cb(ch, ev) */
	T33_I9_RISCV,		/* tisp_awb_set_riscv_param(ch) */
	T33_I9_HW,		/* tisp_awb_set_hardware_param(ch) */
	T33_I9_GAIN,		/* tisp_awb_set_gain(ch) */
	T33_I9_IRQ,		/* system_irq_func_set(ch, irq) */
	T33_I9_REGW,		/* tisp_reg_write_t33(ch, 12, reg, val) */
	T33_I9_CNT_WEIGHT,	/* defog_count_weight35abc(ch, h, w, byte) */
	T33_I9_PARA1,		/* tisp_defog_write_reg_para1(ch) */
	T33_I9_INTERP_K,	/* tisp_defog_interp_trans_k(ch, ...) */
	T33_I9_PARA2_UPD,	/* tisp_defog_update_reg_para2(ch) */
	T33_I9_PARA2_WR,	/* tisp_defog_write_reg_para2(ch) */
	T33_I9_PARA3		/* tisp_defog_write_reg_para3(ch) */
};

struct t33_i9_ops {
	void *(*kmalloc)(u32 size, u32 flags);	/* private_kmalloc */
	void *(*vmalloc)(u32 size);		/* private_vmalloc */
	void (*fill)(void *p, u32 c, u32 n);	/* memset */
	void (*copy)(void *d, const void *s, u32 n);	/* memcpy */
	void (*hook)(u32 id, u32 a, u32 b, u32 c, u32 d);
};

#define T33_I9_ADDR(p)	((u32)(uintptr_t)(p))

static inline u32 t33_i9_le16(const u8 *p)
{
	return p[0] | (u32)p[1] << 8;
}

static inline void t33_i9_put16(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static inline void t33_i9_put32(u8 *p, u32 v)
{
	t33_i9_put16(p, v);
	t33_i9_put16(p + 2, v >> 16);
}

static inline int t33_i9_sdiv(int a, int b)
{
	return b ? a / b : a;
}

static inline u32 t33_i9_udiv(u32 a, u32 b)
{
	return b ? a / b : a;
}

/* mode block byte offsets for the 30 zone values par[3192 + 2k] */
static const u8 t33_awb_mode_off[30] = {
	104, 121, 112, 110, 148, 91, 141, 129, 134, 89, 88, 122, 109, 125, 147,
	128, 145, 94, 113, 144, 105, 98, 118, 130, 149, 142, 133, 99, 119, 93,
};

/* tisp_awb_init: sz = sensor info (u16 width at +12, height at +14) */
static inline int t33_awb_init_ctx(u32 ch_arg, const u8 *sz, u8 **awb_info, u8 **ct_detect, u8 **tparams,
				   const u8 *lut, const u8 *riscv, u8 *awbg_refresh, u8 *awb_refresh,
				   const struct t33_i9_ops *o)
{
	u32 ch = ch_arg & 0xff, w = t33_i9_le16(sz + 12), h = t33_i9_le16(sz + 14);
	u32 t1, t2, a2, a3, t4, a1, k;
	int zx, zy;
	u8 *blk, *st, *par, *mode, *p;

	o->hook(T33_I9_RCFG, ch, 29, 0, 0);
	o->hook(T33_I9_RCFG, ch, 7, 0, 0);
	*awbg_refresh = 0;
	*awb_refresh = 0;
	blk = o->kmalloc(24, 208);
	awb_info[ch] = blk;
	if (!blk)
		o->hook(T33_I9_ERR, 2, 0, 0, 0);
	p = o->kmalloc(192, 208);
	ct_detect[ch] = p;
	o->fill(p, 0, 192);
	mode = o->kmalloc(154, 208);
	t33_i9_put32(blk + 8, T33_I9_ADDR(mode));
	if (!mode)
		o->hook(T33_I9_ERR, 2, 0, 0, 0);
	t33_i9_put32(blk + 20, 0);
	st = o->kmalloc(0x1345c, 208);
	t33_i9_put32(blk + 4, T33_I9_ADDR(st));
	o->fill(st, 0, 0x1345c);
	if (!mode)
		o->hook(T33_I9_ERR, 2, 0, 0, 0);
	st[0x1043a] = 0;
	t33_i9_put16(st + 28690, w);
	t33_i9_put16(st + 28692, h);
	o->hook(T33_I9_AWB_MALLOC_INFO, ch, 0, 0, 0);
	par = tparams[ch] + 3400;
	t33_i9_put32(blk, T33_I9_ADDR(par));
	t33_i9_put32(st + 28676, 512);
	t33_i9_put32(st + 28684, 512);
	t33_i9_put16(st + 28690, w);
	t33_i9_put16(st + 28692, h);
	st[28758] = 0;
	st[28759] = 1;
	o->copy(st + 28764, lut, 1028);
	t33_i9_put32(st + 29792, 1024);
	t33_i9_put32(st + 29796, 1024);
	for (k = 0; k < 15; k++) {
		t33_i9_put32(st + 38068 + 4 * k, 1024);
		t33_i9_put32(st + 38128 + 4 * k, 1024);
		t33_i9_put32(st + 38188 + 4 * k, 5000);
	}
	t33_i9_put32(st + 0x10430, 400);
	t33_i9_put32(st + 0x10434, 1024);
	t33_i9_put32(st + 0x1043c, 5000);
	t33_i9_put16(st + 0x10440, 256);
	t33_i9_put16(st + 0x10442, 256);
	for (k = 0; k < 6; k++)
		t33_i9_put16(st + 0x10444 + 2 * k, 256);
	for (k = 0; k < 4; k++)
		t33_i9_put16(st + 0x10450 + 2 * k, 256);
	p = o->kmalloc(28, 208);
	t33_i9_put32(blk + 12, T33_I9_ADDR(p));
	o->fill(p, 0, 28);
	p = o->kmalloc(4292, 208);
	t33_i9_put32(blk + 16, T33_I9_ADDR(p));
	o->fill(p, 0, 4292);
	st[0x10439] = 0;
	o->hook(T33_I9_AWB_PARAMS, ch, T33_I9_ADDR(tparams[ch] + 3400), 0, 0);
	o->hook(T33_I9_AWB_SHOW, ch, 0, 0, 0);
	o->hook(T33_I9_EVENT, ch, 12, 0, 0);

	/* zone sizes: raise the zone divisors until a zone is below 127 */
	t2 = t33_i9_le16(par + 3188);
	t1 = t33_i9_le16(par + 3184);
	a2 = t33_i9_le16(par + 3186);
	a3 = t33_i9_le16(par + 3190);
	zx = (int)(t33_i9_le16(st + 28690) - t2 + 1) / 2;
	for (;;) {
		t4 = (u32)t33_i9_sdiv(zx, (int)a3) & 0xffff;
		if (t4 < 127)
			break;
		a3 = (a3 + 1) & 0xffff;
	}
	zy = (int)(t33_i9_le16(st + 28692) - t1 + 1) / 2;
	for (;;) {
		a1 = (u32)t33_i9_sdiv(zy, (int)a2) & 0xffff;
		if (a1 < 127)
			break;
		a2 = (a2 + 1) & 0xffff;
	}
	t33_i9_put16(par + 3186, a2);
	t33_i9_put16(par + 3190, a3);
	if (!st[38057] && !st[0x10438]) {
		t33_i9_put32(par + 8, 1);
		t33_i9_put32(par + 12, 0);
	}
	st[38057] = 0;
	for (k = 0; k < 15; k++)
		t33_i9_put16(par + 3192 + 2 * k, t4);
	for (k = 15; k < 30; k++)
		t33_i9_put16(par + 3192 + 2 * k, a1);
	t33_i9_put16(par + 3290, t4);
	t33_i9_put16(par + 3292, a1);
	t33_i9_put16(mode + 24, t2);
	t33_i9_put16(mode + 12, t1);
	for (k = 0; k < 30; k++)
		mode[t33_awb_mode_off[k]] = (u8)t33_i9_le16(par + 3192 + 2 * k);
	mode[117] = (u8)a3;
	mode[140] = (u8)a2;
	if (riscv[ch] == 1)
		o->hook(T33_I9_RISCV, ch, 0, 0, 0);
	if (!st[0x10439]) {
		o->hook(T33_I9_HW, ch, 0, 0, 0);
		o->hook(T33_I9_GAIN, ch, 0, 0, 0);
	}
	o->hook(T33_I9_IRQ, ch, 3, 0, 0);
	o->hook(T33_I9_EVENT, ch, 12, 0, 0);
	return 0;
}

/* tisp_defog_init: sz = sensor info (u16 width at +12, height at +14, words at +920 / +924) */
static inline int t33_defog_init_ctx(u32 ch, const u8 *sz, u32 *max_size, u8 **weight, u8 **dinfo, u8 **tparams,
				     const u8 *cfg_mode, u8 *refresh, const struct t33_i9_ops *o)
{
	u32 h = t33_i9_le16(sz + 14), w = t33_i9_le16(sz + 12), ch8 = ch & 0xff;
	u32 R, C, rh, rw, N, szc, t3, a3, t6, a0, hw, b4, a3v;
	u8 *info, *buf, *g, *par;
	u32 m;

	max_size[ch] = ((h + 5) / 10) * ((w + 9) / 18) * 13;
	weight[ch] = o->vmalloc(max_size[ch]);
	if (!weight[ch])
		o->hook(T33_I9_ERR, 2, 0, 0, 0);
	o->fill(weight[ch], 0, max_size[ch]);
	o->hook(T33_I9_RCFG, ch8, 12, 0, 0);
	info = o->kmalloc(56, 208);
	dinfo[ch] = info;
	if (!info)
		o->hook(T33_I9_ERR, 2, 0, 0, 0);
	buf = o->kmalloc(8192, 208);
	if (!buf)
		return -1;
	m = *cfg_mode;
	hw = T33_I9_ADDR(buf) + 0x80000000U;
	o->hook(T33_I9_REGW, ch, 12, m ? 11 : 0xa210, hw);
	o->hook(T33_I9_REGW, ch, 12, m ? 12 : 0xa214, hw + 0x800);
	o->hook(T33_I9_REGW, ch, 12, m ? 13 : 0xa218, hw + 0x1000);
	o->hook(T33_I9_REGW, ch, 12, m ? 14 : 0xa21c, hw + 0x1800);
	o->hook(T33_I9_REGW, ch, 12, m ? 10 : 0xa20c, 3);
	par = tparams[ch] + 23496;
	t33_i9_put32(info + 4, T33_I9_ADDR(buf));
	t33_i9_put32(info + 8, hw);
	t33_i9_put32(info, 4);
	t33_i9_put32(info + 12, T33_I9_ADDR(par));
	t33_i9_put32(info + 24, t33_le32(sz + 924) << 22 | t33_le32(sz + 920) >> 10);
	g = o->kmalloc(3948, 208);
	t33_i9_put32(info + 16, T33_I9_ADDR(g));
	o->fill(g, 0, 3948);
	buf = o->kmalloc(220, 208);
	t33_i9_put32(info + 20, T33_I9_ADDR(buf));
	o->fill(buf, 0, 220);
	t33_i9_put32(info + 44, 0);
	t33_i9_put32(info + 32, h);
	t33_i9_put32(info + 36, w);
	t33_i9_put32(info + 40, 0);

	if (h == 1080 && w == 1920) {
		a0 = 0x2dcc2;
		t6 = a0 - 3474;
		a3 = a0 - 3411;
		t3 = a0 - 6821;
		szc = 1;
		rw = 6;
		rh = 0;
		C = 106;
		R = 108;
	} else {
		u32 a1b, t5;

		R = ((h >> 1) / 10) * 2;
		rh = (h >> 1) % 10;
		C = ((w >> 1) / 18) * 2;
		rw = (w >> 1) % 18;
		N = (R + 2) * (C + 2);
		t3 = t33_i9_udiv((N >> 1) + 0x80000000U, N);
		a3v = C * (R + 2);
		a3 = t33_i9_udiv((a3v >> 1) + 0x80000000U, a3v);
		a1b = R * (C + 2);
		t6 = t33_i9_udiv((a1b >> 1) + 0x80000000U, a1b);
		t5 = R * C;
		a0 = t33_i9_udiv((t5 >> 1) + 0x80000000U, t5);
		szc = (int)N < 8193 ? 0 : (int)N < 16385 ? 1 : N < 0x8001 ? 2 : 3;
	}
	t33_i9_put32(g + 2160, t3);
	t33_i9_put32(g + 2164, a3);
	t33_i9_put32(g + 2168, t6);
	t33_i9_put32(g + 2172, a0);
	g[2455] = (u8)rh;
	g[2456] = (u8)rw;
	g[2457] = (u8)szc;
	b4 = ((R & 0xff) << 16) | (C & 0xff);
	o->hook(T33_I9_REGW, ch, 12, m ? 3 : 0xa040, b4);
	o->hook(T33_I9_REGW, ch, 12, m ? 4 : 0xa044, rh << 16 | rw);
	o->hook(T33_I9_REGW, ch, 12, m ? 6 : 0xa04c, 0);
	o->hook(T33_I9_REGW, ch, 12, m ? 8 : 0xa204, 0);
	o->hook(T33_I9_REGW, ch, 12, m ? 9 : 0xa208, szc);
	o->hook(T33_I9_REGW, ch, 12, m ? 10 : 0xa20c, 2);
	o->hook(T33_I9_REGW, ch, 12, m ? 1 : 0xa010, 3);

	a3v = par[433];
	info[48] = (u8)a3v;
	if (a3v != 128 || w != t33_i9_le16(par + 190) || h != t33_i9_le16(par + 192)) {
		o->hook(T33_I9_CNT_WEIGHT, ch8, h, w, a3v);
		t33_i9_put16(par + 190, w);
		t33_i9_put16(par + 192, h);
	}
	o->hook(T33_I9_PARA1, ch, 0, 0, 0);
	if (!par[594])
		t33_i9_put32(par + 4, t33_le32(info + 24));
	o->hook(T33_I9_INTERP_K, ch8, t33_le32(par + 4), T33_I9_ADDR(par + 8), T33_I9_ADDR(par + 614));
	o->hook(T33_I9_PARA2_UPD, ch, 0, 0, 0);
	o->hook(T33_I9_PARA2_WR, ch, 0, 0, 0);
	o->fill(g + 2458, 128, 180);
	o->fill(g + 2638, 128, 180);
	o->fill(g + 2818, 128, 180);
	o->fill(g + 2998, 0, 180);
	o->hook(T33_I9_PARA3, ch, 0, 0, 0);
	*refresh = 1;
	o->hook(T33_I9_IRQ, ch8, 10, 0, 0);
	o->hook(T33_I9_EVENT, ch, 3, 0, 0);
	o->hook(T33_I9_IRQ, ch8, 47, 0, 0);
	return 0;
}

#endif /* TX_ISP_T33_I9F_H */
