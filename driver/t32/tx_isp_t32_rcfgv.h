/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 register lists, the vendor flow: readable C of the vendor's
 * tisp_rcfg_init, tisp_rcfg_deinit, tisp_rcfg_reg_init,
 * tisp_single_lsc_rcfg_reg_init, tisp_rcfg_cfg_trig,
 * tisp_single_lsc_rcfg_cfg_trig, tisp_single_lsc_reg_write and
 * tisp_single_lsc_reg_lut_write (list format: tx_isp_t32_rcfg.h).
 *
 * The vendor keeps one root block (kmalloc 2056 B, pointer t32_data_da844):
 * the list table (64 words, kmalloc), its physical address, and per channel
 * 64 list pointers and 64 physical addresses.  Slot 48 is the full register
 * list ("main", 0x8dd0 B, used when the shadow mode t32_data_d03a0 is 1:
 * not a single pipeline), slot 49 the LSC-only list (14428 B, single
 * pipeline).  The reg_init functions write the command headers of the
 * list (the values come later through the shadow writes); cfg_trig puts the
 * list's physical address into table word 48 / 49 and word 0, flushes the
 * caches and starts the list (0x22014 / 0x2201c, then 0x2c).  Pointers are
 * kept as u32 like the vendor (32-bit MIPS, KSEG0: physical = virtual +
 * 0x80000000).
 *
 * The OS calls are hooks (struct t32_rcfgv_ops).  Checked against Matt
 * Davis' recovered C (PR #15 @ 2e2e06bc) by tests/t32_oracle
 * (t32_rcfgv_oracle); single_lsc_reg_write / _lut_write against the
 * disassembly-based versions in env.c (the recovered C lost a load).
 *
 * Vendor behaviour kept: tisp_single_lsc_rcfg_cfg_trig flushes the root's
 * slot array of the channel (root + 8 + ch * 256), not the LSC list; the
 * main list has a 2-word gap after the 0xd500 block (header at word 1682,
 * not 1680) and the 0xdf80 header claims 9 values with the next header after
 * 7 (word 1719).  For a channel other than 0 tisp_rcfg_init returns what the
 * reg_init call left in v0 (an address register of the compiler: list or
 * list + 0x10000); ours returns 0 there (not compared, no caller uses it).
 * Vendor UB, refused by ours (-1 after the vendor's message): a failed
 * kmalloc is reported with isp_printf and then memset (NULL).
 */
#ifndef TX_ISP_T32_RCFGV_H
#define TX_ISP_T32_RCFGV_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
typedef int8_t s8;
#endif
#include "tx_isp_t32_rcfg.h"

#define T32_RCFGV_CHANS		4
#define T32_RCFGV_SLOTS		64
#define T32_RCFGV_SLOT_MAIN	48
#define T32_RCFGV_SLOT_LSC	49
#define T32_RCFGV_MAIN_SZ	0x8dd0U		/* bytes */
#define T32_RCFGV_LSC_SZ	14428U
#define T32_RCFGV_MAIN_LEN	8948U		/* words after word 0 */
#define T32_RCFGV_LSC_LEN	3479U
#define T32_RCFGV_TAB_CLR	256U		/* bytes of the table the vendor clears */
#define T32_RCFGV_GFP		0x024000c0U	/* the vendor's kmalloc flags */
#define T32_RCFGV_KSEG0		0x80000000U
#define T32_RCFGV_TABLE_REG	0x22020U
#define T32_RCFGV_TRIG_REG	0x22014U
#define T32_RCFGV_TRIG2_REG	0x2201cU
#define T32_RCFGV_START_REG	0x2cU
#define T32_RCFGV_MODE_REG	0x2f0U

struct t32_rcfgv_root {				/* 2056 B */
	u32 tab;				/* list table (64 words) */
	u32 tab_phys;
	u32 virt[T32_RCFGV_CHANS][T32_RCFGV_SLOTS];	/* +8 */
	u32 phys[T32_RCFGV_CHANS][T32_RCFGV_SLOTS];	/* +1032 */
};

struct t32_rcfgv_ops {
	void *(*kmalloc)(u32 size, u32 flags);
	void (*kfree)(void *p);
	u32 (*cache_size)(u32 size);			/* lib_tisp_dma_cache_32 */
	void (*dma_sync)(u32 addr, u32 size);		/* private_dma_cache_sync(NULL, addr, size, 0) */
	void (*reg_write)(u32 reg, u32 val);		/* system_reg_write */
	void (*print_nomem)(void);			/* isp_printf(2, "...") */
};

struct t32_rcfgv {
	struct t32_rcfgv_root *root;			/* t32_data_da844 */
	u8 shadow;					/* t32_data_d03a0: 1 = main list */
	const struct t32_rcfgv_ops *ops;
};

static inline void *t32_rcfgv_ptr(u32 a)
{
	return (void *)(uintptr_t)a;
}

static inline u32 t32_rcfgv_addr(const void *p)
{
	return (u32)(uintptr_t)p;
}

/* one list command: register, value count, port (values all to one register), words skipped after it */
struct t32_rcfgv_cmd {
	u32 reg;
	u16 count;
	u8 port;
	s8 gap;		/* words between the values and the next header (vendor list quirks) */
};

static inline u32 t32_rcfgv_hdr(const struct t32_rcfgv_cmd *c)
{
	return (c->port ? 0 : T32_RCFG_CMD) | (u32)(c->count - 1) << T32_RCFG_COUNT_SHIFT |
	       ((c->reg >> 2) & T32_RCFG_OFFSET_MASK);
}

/* write the headers of a command list from word 1 on; returns the word after the last value */
static inline u32 t32_rcfgv_headers(u32 *list, const struct t32_rcfgv_cmd *c, u32 n)
{
	u32 k, w = 1;

	for (k = 0; k < n; k++) {
		list[w] = t32_rcfgv_hdr(&c[k]);
		w += (u32)((int)c[k].count + 1 + c[k].gap);
	}
	return w;
}

/* the full register list of tisp_rcfg_reg_init (0x1c22c): 123 commands */
static const struct t32_rcfgv_cmd t32_rcfgv_main_cmds[] = {
	{0x40, 1, 0, 0}, {0x60, 1, 0, 0}, {0x6c, 2, 0, 0}, {0x78, 1, 0, 0}, {0x100, 1, 0, 0},
	{0xf0010, 1, 0, 0}, {0xf0020, 1, 0, 0}, {0xf0040, 22, 0, 0}, {0xf0100, 31, 0, 0}, {0xf0200, 31, 0, 0}, {0xf0300, 31, 0, 0},
	{0xf0400, 128, 0, 0}, {0xf0740, 6, 0, 0}, {0xf0760, 45, 0, 0}, {0xf0820, 13, 0, 0}, {0xf0870, 13, 0, 0}, {0xf08c0, 13, 0, 0},
	{0xf0910, 13, 0, 0}, {0xf0960, 13, 0, 0}, {0xf09b0, 13, 0, 0},
	{0x2010, 1, 0, 0}, {0x2040, 77, 0, 0}, {0x3010, 2, 0, 0}, {0x3040, 74, 0, 0}, {0x317c, 9, 0, 0}, {0x3200, 8, 0, 0},
	{0x6010, 1, 0, 0}, {0x6040, 2, 0, 0}, {0x8010, 1, 0, 0}, {0x8040, 161, 0, 0}, {0x82d4, 12, 0, 0}, {0xb010, 1, 0, 0}, {0xb040, 7, 0, 0},
	{0x14010, 1, 0, 0}, {0x14040, 2, 0, 0}, {0x14050, 2, 0, 0}, {0x9010, 1, 0, 0}, {0x9040, 22, 0, 0}, {0xf040, 48, 0, 0},
	{0xe010, 1, 0, 0}, {0xe050, 1, 0, 0}, {0xe060, 7, 0, 0}, {0x2603c, 62, 0, 0}, {0x12010, 1, 0, 0}, {0x12040, 1, 0, 0}, {0x12050, 14, 0, 0},
	{0x13010, 1, 0, 0}, {0x13020, 25, 0, 0},
	{0xd014, 7, 0, 0}, {0xd040, 8, 0, 0}, {0xd080, 5, 0, 0}, {0xd100, 44, 0, 0}, {0xd1bc, 14, 0, 0}, {0xd1fc, 8, 0, 0}, {0xd230, 2, 0, 0},
	{0xd24c, 14, 0, 0}, {0xd2a0, 5, 0, 0}, {0xd300, 4, 0, 0}, {0xd318, 4, 0, 0}, {0xd330, 19, 0, 0}, {0xd400, 18, 0, 0}, {0xd450, 5, 0, 0},
	{0xd46c, 10, 0, 0}, {0xd4a0, 3, 0, 0}, {0xd500, 519, 0, 2},
	{0xdf00, 28, 0, 0}, {0xdf80, 9, 0, -2}, {0x11010, 1, 0, 0}, {0x11040, 105, 0, 0}, {0x111ec, 24, 0, 0}, {0x17010, 1, 0, 0}, {0x17040, 23, 0, 0},
	{0x170b8, 1, 0, 0}, {0x16010, 1, 0, 0}, {0x16040, 48, 0, 0}, {0x18010, 1, 0, 0}, {0x18040, 55, 0, 0}, {0x18124, 3, 0, 0},
	{0xc010, 1, 0, 0}, {0xc040, 18, 0, 0}, {0xc090, 6, 0, 0}, {0x25040, 13, 0, 0}, {0x15010, 1, 0, 0},
	{0x32040, 1, 0, 0}, {0x32044, 128, 1, 0}, {0x32040, 1, 0, 0}, {0x32080, 1, 0, 0}, {0x32084, 128, 1, 0}, {0x32080, 1, 0, 0},
	{0x32060, 1, 0, 0}, {0x32064, 128, 1, 0}, {0x32060, 1, 0, 0},
	{0xa010, 1, 0, 0}, {0xa040, 122, 0, 0}, {0xa228, 1, 0, 0}, {0x320a0, 1, 0, 0}, {0x320a4, 180, 1, 0}, {0x320a0, 1, 0, 0}, {0xa228, 1, 0, 0},
	{0x5010, 1, 0, 0}, {0x5040, 13, 0, 0}, {0x32020, 1, 0, 0}, {0x32024, 3456, 1, 0}, {0x32020, 1, 0, 0},
	{0x19060, 5, 0, 0}, {0x32260, 1, 0, 0}, {0x32264, 1500, 1, 0}, {0x32260, 1, 0, 0},
	{0x7010, 1, 0, 0}, {0x7040, 44, 0, 0}, {0x7100, 6, 0, 0}, {0x7120, 1, 0, 0}, {0x7130, 284, 0, 0},
	{0x10010, 1, 0, 0}, {0x10040, 6, 0, 0}, {0x10090, 8, 0, 0},
	{0x320c0, 1, 0, 0}, {0x320c4, 420, 1, 0}, {0x320c0, 1, 0, 0}, {0x32100, 1, 0, 0}, {0x32104, 420, 1, 0}, {0x32100, 1, 0, 0},
	{0x2f4, 1, 0, 0},
};

/* the LSC list of tisp_single_lsc_rcfg_reg_init (0x1c1d0) */
static const struct t32_rcfgv_cmd t32_rcfgv_lsc_cmds[] = {
	{0x5010, 1, 0, 0}, {0x5040, 13, 0, 0}, {0x32020, 1, 0, 0}, {0x32024, 3456, 1, 0}, {0x32020, 1, 0, 0}, {0x2f4, 1, 0, 0},
};

#define T32_RCFGV_N(a)	(sizeof(a) / sizeof((a)[0]))

/* tisp_rcfg_reg_init: length, headers, 0x2f4 = 1 (the last value) */
static inline void t32_rcfgv_main_reg_init(u32 *list)
{
	u32 end;

	list[0] = T32_RCFGV_MAIN_LEN;
	end = t32_rcfgv_headers(list, t32_rcfgv_main_cmds, T32_RCFGV_N(t32_rcfgv_main_cmds));
	list[end - 1] = 1;
}

/* tisp_single_lsc_rcfg_reg_init: length, headers, 0x2f4 = 0 (the last value) */
static inline void t32_rcfgv_lsc_reg_init(u32 *list)
{
	u32 end;

	list[0] = T32_RCFGV_LSC_LEN;
	end = t32_rcfgv_headers(list, t32_rcfgv_lsc_cmds, T32_RCFGV_N(t32_rcfgv_lsc_cmds));
	list[end - 1] = 0;
}

static inline u32 *t32_rcfgv_list(const struct t32_rcfgv *r, u32 ch, u32 slot)
{
	return t32_rcfgv_ptr(r->root->virt[ch][slot]);
}

/* tisp_single_lsc_reg_write / _reg_lut_write (0x1c800 / 0x1c84c): store into the LSC list image */
static inline void t32_rcfgv_lsc_reg_write(const struct t32_rcfgv *r, u32 ch, u32 idx, u32 val)
{
	t32_rcfgv_list(r, ch, T32_RCFGV_SLOT_LSC)[idx] = val;
}

static inline void t32_rcfgv_lsc_reg_lut_write(const struct t32_rcfgv *r, u32 ch, u32 idx, u32 val, u32 off)
{
	t32_rcfgv_list(r, ch, T32_RCFGV_SLOT_LSC)[idx + off] = val;
}

/* allocate, clear and register one list of a channel, then write its headers (0 / -1) */
static inline long t32_rcfgv_new_list(struct t32_rcfgv *r, u32 ch, u32 slot)
{
	u32 size = slot == T32_RCFGV_SLOT_MAIN ? T32_RCFGV_MAIN_SZ : T32_RCFGV_LSC_SZ;
	u32 *list = r->ops->kmalloc(size, T32_RCFGV_GFP);

	if (!list) {
		r->ops->print_nomem();
		return -1;	/* vendor: memset (NULL) */
	}
	memset(list, 0, size);
	r->root->virt[ch][slot] = t32_rcfgv_addr(list);
	r->root->phys[ch][slot] = t32_rcfgv_addr(list) + T32_RCFGV_KSEG0;
	if (slot == T32_RCFGV_SLOT_MAIN)
		t32_rcfgv_main_reg_init(list);
	else
		t32_rcfgv_lsc_reg_init(list);
	return 0;
}

/*
 * tisp_rcfg_init (0x1cbc4): pipes = the u32 at +900 of the init parameters
 * (1 = single pipeline: LSC list, shadow mode 0; else main list, mode 1).
 * Channel 0 also allocates the root and the table and writes the mode to
 * 0x2f0.
 */
static inline long t32_rcfgv_init(struct t32_rcfgv *r, u32 ch, u32 pipes)
{
	u32 slot, tab_sz;
	long ret;
	u32 *tab;

	r->shadow = pipes == 1 ? 0 : 1;
	slot = r->shadow == 1 ? T32_RCFGV_SLOT_MAIN : T32_RCFGV_SLOT_LSC;
	if (ch != 0)
		return t32_rcfgv_new_list(r, ch, slot);
	r->root = r->ops->kmalloc(sizeof(*r->root), T32_RCFGV_GFP);
	if (!r->root) {
		r->ops->print_nomem();
		return -1;	/* vendor: memset (NULL) */
	}
	memset(r->root, 0, sizeof(*r->root));
	tab_sz = r->ops->cache_size(slot == T32_RCFGV_SLOT_MAIN ? T32_RCFGV_MAIN_SZ : T32_RCFGV_LSC_SZ);
	tab = r->ops->kmalloc(tab_sz, T32_RCFGV_GFP);
	if (!tab) {
		r->ops->print_nomem();
		return -1;	/* vendor: memset (NULL) */
	}
	memset(tab, 0, T32_RCFGV_TAB_CLR);
	r->root->tab = t32_rcfgv_addr(tab);
	r->root->tab_phys = t32_rcfgv_addr(tab) + T32_RCFGV_KSEG0;
	ret = t32_rcfgv_new_list(r, 0, slot);
	if (ret)
		return ret;
	r->ops->reg_write(T32_RCFGV_MODE_REG, r->shadow == 1 ? 1 : 0);
	return 0;
}

/* tisp_rcfg_deinit (0x1ce38): free every list, the table and the root */
static inline int t32_rcfgv_deinit(struct t32_rcfgv *r)
{
	u32 ch, k;

	if (!r->root)
		return 0;
	for (ch = 0; ch < T32_RCFGV_CHANS; ch++)
		for (k = 0; k < T32_RCFGV_SLOTS; k++)
			if (r->root->virt[ch][k]) {
				r->ops->kfree(t32_rcfgv_ptr(r->root->virt[ch][k]));
				r->root->virt[ch][k] = 0;
			}
	if (r->root->tab) {
		r->ops->kfree(t32_rcfgv_ptr(r->root->tab));
		r->root->tab = 0;
	}
	r->ops->kfree(r->root);
	r->root = NULL;
	return 0;
}

/* table word slot and word 0 = the list, flush table and list, start the list (trigger bit) */
static inline void t32_rcfgv_start(const struct t32_rcfgv *r, u32 ch, u32 slot, u32 bit, u32 list_sz,
				   u32 flush_addr)
{
	u32 *tab = t32_rcfgv_ptr(r->root->tab);

	tab[slot] = r->root->phys[ch][slot];
	tab[0] = r->root->phys[ch][slot];
	r->ops->reg_write(T32_RCFGV_TABLE_REG, r->root->tab_phys);
	r->ops->dma_sync(r->root->tab, T32_RCFGV_TAB_CLR);
	r->ops->dma_sync(flush_addr, r->ops->cache_size(list_sz));
	r->ops->reg_write(T32_RCFGV_TRIG_REG, bit << 16);
	r->ops->reg_write(T32_RCFGV_TRIG2_REG, bit << 16);
	r->ops->reg_write(T32_RCFGV_START_REG, bit);
}

/* tisp_rcfg_cfg_trig (0x1c9e0): only in shadow mode 1; else 1 */
static inline int t32_rcfgv_cfg_trig(const struct t32_rcfgv *r, u32 ch)
{
	if (r->shadow != 1)
		return 1;
	t32_rcfgv_start(r, ch, T32_RCFGV_SLOT_MAIN, 1, T32_RCFGV_MAIN_SZ,
			r->root->virt[ch][T32_RCFGV_SLOT_MAIN]);
	return 0;
}

/* tisp_single_lsc_rcfg_cfg_trig (0x1cae0): flushes the channel's slot array (vendor), see above */
static inline int t32_rcfgv_lsc_cfg_trig(const struct t32_rcfgv *r, u32 ch)
{
	t32_rcfgv_start(r, ch, T32_RCFGV_SLOT_LSC, 2, T32_RCFGV_LSC_SZ, t32_rcfgv_addr(r->root->virt[ch]));
	return 0;
}

#endif /* TX_ISP_T32_RCFGV_H */
