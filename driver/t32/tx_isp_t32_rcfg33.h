/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) per-block register lists.
 *
 * The T33 vendor module programs almost every ISP block through the RCFG
 * lists of tx_isp_t32_rcfg.h, one list per block ("module id"): the list of
 * module N is table word N of the table at 0x22020, written by setting bit
 * N of 0x22010 (0x22014 for N >= 32).  Each block's list holds the block's
 * registers in ascending order (one command per run of consecutive
 * registers).  At stream-on all lists are started at once by writing their
 * mask to 0x22010, then the top list (33) is started with 0x22014 bit 1;
 * the driver waits until both read 0.  The core init sets the table and
 * 0x22024 = 1, 0x22018 = 0x2201c = ~0.  (QEMU trace of the vendor module
 * with libimp 2.0.2.1, T33 plan step 5 / openimp #388.)
 *
 * Module ids seen in the trace and the block each list carries:
 *   2 0x02xxx   3 0x26xxx   4 0x03xxx   5 0x04xxx   6 0x05xxx   7 0x06xxx
 *   8 0x07xxx+0x32 port     9 0x08xxx  10 0x09xxx  11 0x32 port
 *  12 0x0axxx+0x32 port    13 CSC 0x0bxxx          14 0x11xxx  15 0x0dxxx
 *  16 0x0cxxx  17 0x0exxx  18 0x0fxxx  19 0x10xxx+0x32 port   20 0x13xxx
 *  21 CDNS 0x12xxx         22 CSCCR 0x14xxx        23 MSCA 0xf0/0xf2xxx
 *  29 AWB 0x16xxx          30 AE 0x17xxx           31 0x18xxx
 *  33 top 0x00xxx
 * This file has the list builder and the blocks done so far (top, CSC,
 * CSCCR); the other blocks are open (#388).
 */
#ifndef TX_ISP_T32_RCFG33_H
#define TX_ISP_T32_RCFG33_H

#include "tx_isp_t32_rcfg.h"
#include "tx_isp_t32_front.h"

#define T33_RCFG_TRIG		0x22010U	/* bit N: start list N (N < 32) */
#define T33_RCFG_TRIG_HI	0x22014U	/* bit N: start list 32 + N */
#define T33_RCFG_TABLE		0x22020U
#define T33_RCFG_CTRL		0x22024U	/* 1 after the table is set */
#define T33_RCFG_MASK0		0x22018U
#define T33_RCFG_MASK1		0x2201cU
#define T33_RCFG_MODE		0x002f0U	/* 0 (single sensor) */

#define T33_MOD_CSC		13
#define T33_MOD_CDNS		21
#define T33_MOD_CSCCR		22
#define T33_MOD_AWB		29
#define T33_MOD_AE		30
#define T33_MOD_TOP		33
#define T33_MODULES		40

#define T33_SLOT_WORDS		1024U	/* list capacity per module (vendor max seen: 868) */

/*
 * Build a module's list from the block's writes: block id registers
 * (offset 0x000 of a 4 KiB block, written by the T32 code) are dropped, as
 * the T33 vendor lists carry none; the rest go in ascending register order
 * (stable).  Returns the list length in words, -1 on overflow or too many
 * writes.
 */
#define T33_MOD_MAX_WRITES	256

static inline int t33_rcfg_build(struct t32_rcfg_list *l, u32 *buf, u32 cap,
				 const struct t32_front_write *w, unsigned int n)
{
	static struct t32_front_write s[T33_MOD_MAX_WRITES];	/* callers are serialised (stream-on) */
	struct t32_front_write t;
	unsigned int i, j, m = 0;

	if (n > T33_MOD_MAX_WRITES)
		return -1;
	for (i = 0; i < n; i++)
		if (w[i].reg & 0xfff)
			s[m++] = w[i];
	for (i = 1; i < m; i++) {
		t = s[i];
		for (j = i; j > 0 && s[j - 1].reg > t.reg; j--)
			s[j] = s[j - 1];
		s[j] = t;
	}
	t32_rcfg_init(l, buf, cap);
	for (i = 0; i < m; i++)
		t32_rcfg_add(l, s[i].reg, s[i].val);
	return l->overflow ? -1 : (int)l->words[0];
}

/*
 * tisp_top_init + tisp_top_reg_cfg + tisp_top_restart_cfg of the T33
 * module, pipeline 0: sec = IQ section 0 (108 bytes; the T32's fields 8
 * bytes later).  lw: the top list (module 33), dw: direct writes before
 * the lists.  bayer from t32_mbus_to_bayer; direct: IVDC direct mode
 * (0x100 = 5, buffered 4: vendor traces with and without direct_mode).
 */
#define T33_FW(w, n, r, v) do { (w)[(n)].reg = (r); (w)[(n)].val = (v); (n)++; } while (0)
#define T33_TOP_BYTES		108U
#define T33_TOP_LIST_WRITES	7
#define T33_TOP_DIRECT_WRITES	4

static inline void t33_top_init(const u8 *sec, u32 width, u32 height, u32 bayer, int direct,
				struct t32_front_write *lw, struct t32_front_write *dw)
{
	u32 size = width << 16 | height, en = 0, ctl = 0, sta = 0, fmt;
	u32 idx = bayer & 0x1f, deir = idx < 4 ? 0 : 1;
	unsigned int n = 0, i;

	for (i = 0; i < 32; i++) {
		en |= (u32)(sec[0xc + i] & 1) << i;
		ctl |= (u32)(sec[0x2c + i] & 1) << i;
	}
	for (i = 0; i < 10; i++)
		sta |= (u32)sec[0x4c + i] << i;
	fmt = deir << 20 | idx | ((bayer & 0x3ffff) >> 16) << 8;
	T33_FW(lw, n, 0x40, en);
	T33_FW(lw, n, 0x60, sta);
	T33_FW(lw, n, 0x6c, ctl);
	T33_FW(lw, n, 0x70, size);
	T33_FW(lw, n, 0x78, fmt);
	T33_FW(lw, n, 0x100, direct ? 5 : 4);
	T33_FW(lw, n, 0x2f4, 5);
	n = 0;
	T33_FW(dw, n, 0x80, size);
	T33_FW(dw, n, 0x88, fmt);
	T33_FW(dw, n, 0x48, 0xffffffff);
	T33_FW(dw, n, 0x20, 0x7fffff2f);
}

#undef T33_FW

/* the T33 CSCCR list ends in 0x14010 = 3 (the T32 module writes ~0) */
#define T33_CSCCR_EN		3U

#endif /* TX_ISP_T32_RCFG33_H */
