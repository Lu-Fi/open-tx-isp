/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) register-list core (openimp #99), binary oracle block
 * "rcfgcore" (tests/t33_binoracle/rcfgcore.conf):
 *
 *  - tisp_rcfg_module_register: a block's list is registered per channel:
 *    its word count (fixed per module id, TMO from tisp_t33_tmo_rcfg_reg_num),
 *    "used" flag and reg_init callback into the module-attribute record
 *    (stModuleAttr: 4 channels x 64 records of 0x1c bytes), the channel's
 *    tispRcfgRegNum sum, then the list buffer (kmalloc, zeroed, its DMA
 *    address = KSEG0 + 0x80000000) and the reg_init (the list skeleton,
 *    tx_isp_t33_rcfg*.h); MSCA (23) adds 0x199 to the sum but sizes 0x19a
 *    words, as the vendor; ids 24, 26..28, 32, 34..36 and outside 2..37 are
 *    refused (0x80022001), no memory 0x80022000;
 *  - tisp_rcfg_module_release: the list buffer freed (deinit of every block);
 *  - tisp_rcfg_module_cfg_trig / tisp_rcfg0_dual_module_cfg_trig: the list's
 *    DMA address into its table word (rcfg_buf_info[0], 64 words), table and
 *    list written back, then (cfg_trig) the start bit in 0x22010 / 0x22014;
 *  - tisp_rcfg_t33_write_all_reg (stream-on, lib_tisp_rcfg_cfg_trig, the
 *    cfg-end interrupt): the table words of all lists in the vendor's order,
 *    LDC (25) only when stIspInitPar byte 0x2c of the channel is 1, the start
 *    mask RcfgModule = 0xe0fffffc (0xe2fffffc with LDC) into 0x22010, then
 *    the TOP list (33) by 0x22014 = 2; tisp_get_rcfg_module returns the mask.
 *
 * cfg_trig and write_all end with a tail call system_reg_read(0x22014) (the
 * decompiler shows a register-direct read).  The includer passes the vendor
 * globals and callees in struct t33_rcfgc.
 */
#ifndef TX_ISP_T33_RCFGCORE_H
#define TX_ISP_T33_RCFGCORE_H

#include "tx_isp_t33_blk.h"

#define T33_RC_CH_BYTES		0x700U	/* one channel of stModuleAttr */
#define T33_RC_REC		0x1cU	/* one module record */
#define T33_RC_PADDR		0x04U	/* list DMA address */
#define T33_RC_VADDR		0x08U	/* list buffer */
#define T33_RC_USED		0x10U
#define T33_RC_WORDS		0x14U
#define T33_RC_FN		0x18U	/* reg_init(ch, list) */
#define T33_RC_TRIG		0x22010U
#define T33_RC_TRIG_HI		0x22014U
#define T33_RC_ERR_ID		0x80022001U
#define T33_RC_ERR_MEM		0x80022000U

typedef u32 (*t33_rc_init_fn)(u32 ch, u32 *list);

struct t33_rcfgc {
	u8 *attr;			/* stModuleAttr */
	u16 *regnum;			/* tispRcfgRegNum[4] */
	const u16 *tmo_num;		/* tisp_t33_tmo_rcfg_reg_num */
	const u8 *initpar;		/* stIspInitPar (0x3f0 bytes per channel) */
	u32 ***buf_info;		/* rcfg_buf_info: {list table, its DMA address} */
	u32 *module;			/* RcfgModule */
	u32 *trig_cnt;			/* g_rcfg_trig_start_cnt[4] */
	u8 *refresh;			/* g_rcfgRefresh */
	void *(*alloc)(u32 bytes);			/* private_kmalloc(n, GFP_KERNEL) */
	void (*zero)(void *p, u32 bytes);		/* memset(p, 0, n) */
	void (*free)(void *p);				/* private_kfree */
	void (*sync)(void *p, u32 bytes);		/* private_dma_cache_sync(0, p, n, 0) */
	void (*wr)(u32 reg, u32 val);			/* system_reg_write */
	u32 (*rd)(u32 reg);				/* system_reg_read */
	void (*log)(int level, u32 id);			/* isp_printf / printk */
};

/* tisp_rcfg_module_register's word counts by id - 2 (0: refused; 0xffff: TMO) */
static const u16 t33_rc_words[36] = {
	0x2f, 0x2c, 9, 6, 0x12, 5, 0xffff, 0x90, 0x25, 0x10a, 0x14a, 10, 0x71, 0x1e9, 0x28, 7, 0x35, 0x364,
	10, 0x17, 8, 0x19a, 0, 0x13, 0, 0, 0, 0x2d, 0x16, 0x34, 0, 0xd, 0, 0, 0, 0x46,
};

static inline u32 *t33_rc_w(const struct t33_rcfgc *t, u32 ch, u32 id, u32 off)
{
	return (u32 *)(t->attr + (ch & 0xff) * T33_RC_CH_BYTES + (id & 0xff) * T33_RC_REC + off);
}

/* tisp_rcfg_module_register */
static inline u32 t33_rcfg_module_register(const struct t33_rcfgc *t, u32 ch, u32 id, u32 fn)
{
	u32 k, n, add, *list;

	id &= 0xff;
	k = (id - 2) & 0xff;
	if (k >= 36 || !t33_rc_words[k]) {
		t->log(-1, id);
		return T33_RC_ERR_ID;
	}
	ch &= 0xff;
	n = t33_rc_words[k];
	add = n == 0x19a ? 0x199 : n;
	if (n == 0xffff)
		n = add = *t->tmo_num;
	t->regnum[ch] = (u16)(t->regnum[ch] + add);
	*t33_rc_w(t, ch, id, T33_RC_USED) = 1;
	*t33_rc_w(t, ch, id, T33_RC_FN) = fn;
	*t33_rc_w(t, ch, id, T33_RC_WORDS) = n;
	list = t->alloc(n << 2);
	*t33_rc_w(t, ch, id, T33_RC_VADDR) = (u32)(unsigned long)list;
	if (!list) {
		t->log(2, id);
		return T33_RC_ERR_MEM;
	}
	t->zero(list, *t33_rc_w(t, ch, id, T33_RC_WORDS) << 2);
	*t33_rc_w(t, ch, id, T33_RC_PADDR) = *t33_rc_w(t, ch, id, T33_RC_VADDR) + 0x80000000U;
	if (*t33_rc_w(t, ch, id, T33_RC_PADDR) & 7)
		t->log(-2, id);
	((t33_rc_init_fn)(unsigned long)*t33_rc_w(t, ch, id, T33_RC_FN))(ch,
		(u32 *)(unsigned long)*t33_rc_w(t, ch, id, T33_RC_VADDR));
	t->log(0, id);
	return 0;
}

/* tisp_rcfg_module_release */
static inline u32 t33_rcfg_module_release(const struct t33_rcfgc *t, u32 ch, u32 id)
{
	u32 *v = t33_rc_w(t, ch, id, T33_RC_VADDR);

	if (*v) {
		t->free((void *)(unsigned long)*v);
		*v = 0;
	}
	return 0;
}

/* the table word of list id = its DMA address, table and list written back */
static inline void t33_rc_table(const struct t33_rcfgc *t, u32 ch, u32 id)
{
	u32 *tbl = (*t->buf_info)[0];

	id &= 0xff;
	tbl[id] = *t33_rc_w(t, ch, id, T33_RC_PADDR);
	t->sync(tbl, 0x100);
	t->sync((void *)(unsigned long)*t33_rc_w(t, ch, id, T33_RC_VADDR), *t33_rc_w(t, ch, id, T33_RC_WORDS) << 2);
}

/* tisp_rcfg0_dual_module_cfg_trig */
static inline void t33_rcfg_dual_cfg_trig(const struct t33_rcfgc *t, u32 ch, u32 id)
{
	t33_rc_table(t, ch, id);
}

/* tisp_rcfg_module_cfg_trig */
static inline u32 t33_rcfg_module_cfg_trig(const struct t33_rcfgc *t, u32 ch, u32 id)
{
	t33_rc_table(t, ch, id);
	t->wr((id & 0xff) < 32 ? T33_RC_TRIG : T33_RC_TRIG_HI, 1U << (id & 31));
	t->rd(T33_RC_TRIG);
	return t->rd(T33_RC_TRIG_HI);
}

/* tisp_rcfg_t33_write_all_reg: the lists in the vendor's order */
static const u8 t33_rc_all[23] = { 5, 2, 3, 4, 9, 13, 22, 15, 14, 17, 20, 21, 18, 11, 10, 29, 7, 8, 31, 16, 12, 6, 19 };

static inline u32 t33_rcfg_write_all_reg(const struct t33_rcfgc *t, u32 ch)
{
	u32 i, m, *tbl;

	ch &= 0xff;
	for (i = 0; i < sizeof(t33_rc_all); i++)
		t33_rcfg_dual_cfg_trig(t, ch, t33_rc_all[i]);
	if (t->initpar[ch * 0x3f0 + 0x2c] == 1) {
		t33_rcfg_dual_cfg_trig(t, ch, 25);
		m = 0xe2ff0000U;
	} else {
		m = 0xe0ff0000U;
	}
	*t->module = m | 0xfffc;
	t33_rcfg_dual_cfg_trig(t, ch, 30);
	t33_rcfg_dual_cfg_trig(t, ch, 23);
	t->wr(T33_RC_TRIG, *t->module);
	tbl = (*t->buf_info)[0];
	tbl[33] = *t33_rc_w(t, ch, 33, T33_RC_PADDR);
	t->sync(tbl, 0x100);
	t->sync((void *)(unsigned long)*t33_rc_w(t, ch, 33, T33_RC_VADDR), *t33_rc_w(t, ch, 33, T33_RC_WORDS) << 2);
	t->wr(T33_RC_TRIG_HI, 2);
	t->rd(T33_RC_TRIG);
	return t->rd(T33_RC_TRIG_HI);
}

/* tisp_get_rcfg_module */
static inline u32 t33_get_rcfg_module(const struct t33_rcfgc *t)
{
	return *t->module;
}

/* lib_tisp_rcfg_cfg_trig */
static inline void t33_lib_rcfg_cfg_trig(const struct t33_rcfgc *t, u32 ch)
{
	t33_rcfg_write_all_reg(t, ch & 0xff);
	t->trig_cnt[ch]++;
}

/* tisp_rcfg_cfg_end_interrupt */
static inline void t33_rcfg_cfg_end_interrupt(const struct t33_rcfgc *t, u32 ch)
{
	u32 a, b;

	if (*t->refresh != 1)
		return;
	a = t->rd(T33_RC_TRIG);
	b = t->rd(T33_RC_TRIG_HI);
	if (!a && !b) {
		t33_rcfg_write_all_reg(t, ch & 0xff);
		*t->refresh = 0;
	}
}

#endif
