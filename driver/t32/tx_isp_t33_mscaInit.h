/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA block init (vendor tisp_msca_t33_init): per-channel attribute and
 * info blocks, then the register defaults of the three output channels, either
 * as list slots (cfg mode != 0) or as register addresses (cfg mode 0), written
 * through tisp_reg_write_t33 (module 23).
 * The callees keep their vendor symbol names (asm labels), so no prototype of
 * another header can clash with these declarations.
 */
#ifndef TX_ISP_T33_MSCAINIT_H
#define TX_ISP_T33_MSCAINIT_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_msca.h"

extern void *t33_msca_kmalloc(u32 size, u32 flags) __asm__("private_kmalloc");
extern int t33_msca_isp_printf(int lvl, const char *fmt, ...) __asm__("isp_printf");
extern int t33_msca_printk(const char *fmt, ...) __asm__("printk");
extern int t33_msca_reg_module(u32 chn, u32 mod, void *reg_init) __asm__("tisp_rcfg_module_register");
extern int t33_msca_regw(u32 chn, u32 mod, u32 slot_or_addr, u32 val) __asm__("tisp_reg_write_t33");
extern int t33_msca_sysw(u32 addr, u32 val) __asm__("system_reg_write");
extern int t33_msca_irq_set(u32 chn, u32 irq, void *fn) __asm__("system_irq_func_set");

static inline u32 t33_msca_h(const u8 *p)
{
	return *(const u16 *)p;
}

static inline void t33_msca_sh(u8 *p, u32 v)
{
	*(u16 *)p = (u16)v;
}

/* one register default: list slot in cfg mode, register address otherwise */
static inline void t33_msca_wr(u8 chn, u8 cfg, u32 slot, u32 addr, u32 val)
{
	t33_msca_regw(chn, T33_MOD_MSCA, cfg ? slot : addr, val);
}

/* coefficient port list: n writes to one port, two big and two small defaults */
static inline void t33_msca_coef(u8 chn, u8 cfg, u32 slot, u32 addr, u32 n, u32 big0, u32 big1,
				 u32 sm0, u32 sm1)
{
	u32 k, v;

	for (k = 0; k < n; k++) {
		v = 0;
		if (k == big0 || k == big1)
			v = 0x8000000;
		else if (k == sm0 || k == sm1)
			v = 0x800;
		t33_msca_wr(chn, cfg, slot + k, addr, v);
	}
}

/*
 * tisp_msca_t33_init: par = size block (u16 width at +16, height at +18),
 * init_flag = stIspInitPar word 904, cfg = mscaCfgMode.
 */
static inline int t33_msca_t33_init(u8 chn, const u8 *par, u8 **attr_tab, u8 **info_tab, u32 *osd_info,
				    u8 *const *tparams, u32 init_flag, u8 cfg, void *reg_init,
				    void *frame_irq)
{
	u8 *attr, *info, *b;
	u32 i, s5, s6, a2, w = t33_msca_h(par + 16), h = t33_msca_h(par + 18);
	int ret;

	attr = t33_msca_kmalloc(6036, 208);
	attr_tab[chn] = attr;
	if (!attr) {
		t33_msca_isp_printf(2, "%s: msca attr alloc failed\n", __func__, 1500);
		return -12;
	}
	memset(attr, 0, 6036);
	t33_msca_sh(attr + 66, w);
	t33_msca_sh(attr + 68, h);
	*osd_info = 0;
	t33_msca_isp_printf(0, "%s: msca size %u x %u\n", __func__, 1508, w, h);
	ret = t33_msca_reg_module(chn, T33_MOD_MSCA, reg_init);
	if (ret < 0)
		t33_msca_printk("%s[%d]: msca rcfg register failed %d\n", __func__, 1513, ret);
	t33_msca_sh(attr + 74, 0);
	t33_msca_sh(attr + 76, 0);
	t33_msca_sh(attr + 70, w);
	t33_msca_sh(attr + 72, h);
	t33_msca_wr(chn, cfg, 23, 0xf0090, 0);
	t33_msca_wr(chn, cfg, 24, 0xf0094, t33_msca_h(attr + 70) << 16 | t33_msca_h(attr + 72));

	info = t33_msca_kmalloc(368, 208);
	info_tab[chn] = info;
	if (!info) {
		t33_msca_isp_printf(2, "%s: msca info alloc failed\n", __func__, 1528);
		return -12;
	}
	memset(info, 0, 368);
	*(u8 **)info = tparams[chn] + 0xa30c;
	*(u8 **)(info + 4) = tparams[chn] + 0xa3e8;

	for (i = 0; i < 3; i++) {
		s5 = 30 * i;
		s6 = 256 * i;
		b = attr + 22 * i;
		t33_msca_sh(b + 2, w);
		t33_msca_sh(b + 4, h);
		t33_msca_sh(b + 6, 0);
		t33_msca_sh(b + 8, 0);
		t33_msca_sh(b + 10, w);
		t33_msca_sh(b + 12, h);
		t33_msca_sh(b + 14, w);
		t33_msca_sh(b + 16, h);
		t33_msca_sh(b + 18, 0);
		t33_msca_sh(b + 20, 0);
		attr[6032 + i] = 1;

		t33_msca_wr(chn, cfg, 26 + s5, 0xf0100 + s6, 0);
		if (init_flag == 1 && cfg)
			a2 = 2 * (i + 202);
		else
			a2 = cfg ? 53 + s5 : 0xf0170 + s6;
		t33_msca_regw(chn, T33_MOD_MSCA, a2, t33_msca_h(b + 2));
		t33_msca_wr(chn, cfg, 27 + s5, 0xf0104 + s6, t33_msca_h(b + 10) << 16 | t33_msca_h(b + 12));
		t33_msca_wr(chn, cfg, 28 + s5, 0xf0108 + s6, 0);
		t33_msca_wr(chn, cfg, 29 + s5, 0xf010c + s6, t33_msca_h(b + 14) << 16 | t33_msca_h(b + 16));
		t33_msca_wr(chn, cfg, 30 + s5, 0xf0110 + s6, 16384);
		t33_msca_wr(chn, cfg, 31 + s5, 0xf0114 + s6, 16384);
		if (i != 0)
			t33_msca_wr(chn, cfg, 54 + s5, 0xf0174 + s6,
				    *(const u32 *)(*(u8 **)info_tab[chn] + 44));
		if (init_flag != 1)
			t33_msca_wr(chn, cfg, 35 + s5, 0xf0128 + s6, 0);
		else
			t33_msca_sysw(0xf0128 + s6, 0);
		t33_msca_wr(chn, cfg, 34 + s5, 0xf0124 + s6, 0xffffffff);
		t33_msca_wr(chn, cfg, 6, 0xf004c, 0x77);

		if (i == 0)
			t33_msca_coef(chn, cfg, 204, 0xf0560, 28, 0, 9, 18, 23);
		else if (i == 1)
			t33_msca_coef(chn, cfg, 233, 0xf0570, 34, 0, 27, 10, 22);
		else
			t33_msca_coef(chn, cfg, 268, 0xf0580, 34, 0, 27, 10, 22);

		if (init_flag == 1) {
			t33_msca_wr(chn, cfg, 44 + s5, 0xf014c + s6, 0);
			t33_msca_wr(chn, cfg, 52 + s5, 0xf016c + s6, 0);
		}
		if (init_flag != 1) {
			t33_msca_wr(chn, cfg, 44 + s5, 0xf014c + s6, 2);
			t33_msca_wr(chn, cfg, 52 + s5, 0xf016c + s6, 2);
		}
	}
	t33_msca_wr(chn, cfg, 3, 0xf0040, 0);
	t33_msca_wr(chn, cfg, 1, 0xf0010, 1);
	t33_msca_irq_set(chn, 33, frame_irq);
	return 0;
}

#endif /* TX_ISP_T33_MSCAINIT_H */
