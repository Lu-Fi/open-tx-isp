/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) tisp_dpc_init on the vendor's dpcInfo layout: module list registration, the block state
 * (bin copy, gain interpolation, DPC / CTR / CAC updates from tx_isp_t33_dpcpar.h) and the 37 slot / register
 * stores (tisp_reg_write_t33: slot in list mode, register otherwise), then the refresh mode and the
 * frame-done interrupt.  Kernel services are passed in as callbacks.
 * Oracle block i9G (tests/t33_binoracle/i9G.conf).
 */
#ifndef TX_ISP_T33_I9G_H
#define TX_ISP_T33_I9G_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_dpc.h"
#include "tx_isp_t33_dpcpar.h"

struct t33_dpc_init_ops {
	int (*module_register)(u8 ch, u32 mod);		/* tisp_rcfg_module_register(ch, 2, dpc reg_init) */
	void (*warn)(int ret);				/* printk of the failed registration */
	void *(*alloc)(u32 size, u32 flags);		/* private_kmalloc */
	void (*zero)(void *p, u32 n);			/* memset(p, 0, n) */
	void (*reg_write)(u8 ch, u32 mod, u32 where, u32 val);	/* tisp_reg_write_t33 */
	int (*dma_state)(u32 mod);			/* tisp_rcfg_get_dma_state */
	void (*irq_set)(u8 ch, u32 irq);		/* system_irq_func_set(ch, 38, frame done) */
};

#define T33_DPC_INFO_BYTES	688U

/* tisp_dpc_init: gain is the 16.16 value at +928 of the second argument */
static inline int t33_dpc_init_vendor(const struct t33_dpc_init_ops *o, u8 ch, u32 gain, u32 **info,
				      u8 **tparams, u8 cfgmode, u8 *refresh)
{
	const u8 mode = cfgmode != 0;
	u32 *v;
	int ret;

	ret = o->module_register(ch, 2);
	*refresh = 0;
	if (ret < 0)
		o->warn(ret);
	v = (u32 *)o->alloc(T33_DPC_INFO_BYTES, 208);
	info[ch] = v;
	if (!v)
		return -1;
	o->zero(v, T33_DPC_INFO_BYTES);
	v[T33_DPC_GAIN_WORD] = gain;
	v[T33_DPC_SEC_WORD] = (u32)(unsigned long)(tparams[ch] + 15916);
	t33_dpc_bin_para_v(v);
	t33_vb_intp(v, (const u8 *)(unsigned long)v[T33_DPC_SEC_WORD], gain, t33_dpc_intp, ARRAY_SIZE(t33_dpc_intp));
	t33_dpc_para_v(v);
	t33_ctr_para_v(v);
	t33_cac_para_v(v);

#define W(slot, reg, val) o->reg_write(ch, 2, mode ? (slot) : (reg), (val))
	W(6, 0x204c, ((v[0x52] & 1) << 1) | ((v[0x53] & 1) << 2) | (v[0x51] & 1) | ((v[0x54] & 1) << 3) | ((v[0x59] & 1) << 4) | ((v[0x5a] & 1) << 5) | ((v[0x5b] & 1) << 6) | ((v[0x5f] & 1) << 8) | ((v[0x60] & 1) << 9) | ((v[0x63] & 1) << 0xc) | ((v[0x55] & 1) << 0x10) | ((v[0x56] & 1) << 0x11) | ((v[0x57] & 1) << 0x12) | ((v[0x58] & 1) << 0x13) | ((v[0x5c] & 1) << 0x14) | ((v[0x5d] & 1) << 0x15) | ((v[0x5e] & 1) << 0x16) | ((v[0x61] & 1) << 0x18) | ((v[0x62] & 1) << 0x19) | ((v[0x64] & 1) << 0x1c));
	W(10, 0x205c, ((v[0x6c] & 0xfff) << 0x10) | (v[0x6b] & 0xfff));
	W(11, 0x2060, ((v[0x6e] & 0xff) << 8) | (v[0x6d] & 0xff));
	W(36, 0x2064, ((v[0x70] & 0xfff) << 0x10) | (v[0x6f] & 0xfff));
	W(37, 0x2064, ((v[0x72] & 0xfff) << 0x10) | (v[0x71] & 0xfff));
	W(39, 0x206c, ((v[0x74] & 0xfff) << 0x10) | (v[0x73] & 0xfff));
	W(40, 0x206c, ((v[0x76] & 0xfff) << 0x10) | (v[0x75] & 0xfff));
	W(13, 0x2074, ((v[0x78] & 0xfff) << 0x10) | (v[0x77] & 0xfff));
	W(14, 0x2078, ((v[0x7b] & 0xfff) << 0x10) | (v[0x7a] & 0xfff));
	W(15, 0x207c, ((v[0x7d] & 0xff) << 8) | (v[0x7c] & 0xff));
	W(42, 0x2080, ((v[0x7f] & 0xfff) << 0x10) | (v[0x7e] & 0xfff));
	W(43, 0x2080, ((v[0x81] & 0xfff) << 0x10) | (v[0x80] & 0xfff));
	W(45, 0x2088, ((v[0x83] & 0xfff) << 0x10) | (v[0x82] & 0xfff));
	W(46, 0x2088, ((v[0x85] & 0xfff) << 0x10) | (v[0x84] & 0xfff));
	W(17, 0x2090, ((v[0x87] & 0xfff) << 0x10) | (v[0x86] & 0xfff));
	W(18, 0x2094, ((v[0x8d] & 0x1ff) << 0x10) | (v[0x8c] & 1));
	W(19, 0x2098, ((v[0x90] & 3) << 8) | ((v[0x8f] & 0xfff) << 0x10) | (v[0x8e] & 1));
	W(21, 0x20a0, ((v[0x94] & 0xfff) << 0x10) | (v[0x93] & 0xfff));
	W(34, 0x2170, ((v[0x89] & 0x1ff) << 0x10) | (v[0x88] & 3));
	W(3, 0x2040, ((v[0x4a] & 1) << 8) | ((v[0x4b] & 1) << 0xc) | (v[0x48] & 1) | ((v[0x8a] & 0x7ff) << 0x10));
	W(4, 0x2044, ((v[0x4c] & 0x1f) << 8) | ((v[0x6a] & 0x3f) << 0x10) | (v[0x49] & 0x1f) | ((v[0x79] & 0x3f) << 0x18));
	W(5, 0x2048, ((v[0x4e] & 1) << 8) | ((v[0x4f] & 1) << 0x10) | (v[0x4d] & 1) | ((v[0x50] & 3) << 0x18));
	W(8, 0x2054, ((v[0x66] & 3) << 4) | ((v[0x67] & 3) << 8) | (v[0x65] & 3));
	W(9, 0x2058, ((v[0x69] & 3) << 4) | (v[0x68] & 3));
	W(18, 0x2094, ((v[0x8d] & 0x1ff) << 0x10) | (v[0x8c] & 1));
	W(19, 0x2098, ((v[0x90] & 3) << 8) | ((v[0x8f] & 0xfff) << 0x10) | (v[0x8e] & 1));
	W(20, 0x209c, ((v[0x92] & 3) << 8) | ((v[0x95] & 0x3f) << 0x10) | (v[0x91] & 3));
	W(22, 0x20a4, (v[0x96] & 0xff) | (v[0x99] << 0x18) | ((v[0x97] & 0xff) << 8) | ((v[0x98] & 0xff) << 0x10));
	W(24, 0x20b4, ((v[0x9b] & 0x3f) << 8) | ((v[0x9c] & 0x3f) << 0x10) | (v[0x9a] & 0x3f) | ((v[0x9d] & 0x3f) << 0x18));
	W(26, 0x20c4, v[0x47] & 7);
	W(27, 0x20c8, ((v[0x9f] & 0x1ff) << 4) | ((v[0xa0] & 0x1ff) << 0x10) | (v[0x9e] & 1));
	W(28, 0x20cc, ((v[0xa2] & 0x1ff) << 0xc) | (v[0xa1] & 0x1ff));
	W(30, 0x20e0, ((v[0xa4] & 0x1ff) << 0xc) | (v[0xa3] & 0x1ff));
	W(31, 0x20e4, ((v[0xa6] & 0x7ff) << 0xc) | ((v[0xa7] & 0x1f) << 0x18) | (v[0xa5] & 0xfff));
	W(32, 0x20e8, ((v[0xa9] & 0xfff) << 8) | (v[0xa8] & 0x1f));
	W(34, 0x2170, ((v[0x89] & 0x1ff) << 0x10) | (v[0x88] & 3));
	W(1, 0x2010, 1);
#undef W
	*refresh = o->dma_state(2) ? 2 : 1;
	o->irq_set(ch, 38);
	return 0;
}

#endif /* TX_ISP_T33_I9G_H */
