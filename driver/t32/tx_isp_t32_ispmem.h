/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 5: the reserved-memory allocator.
 * Readable C of the vendor functions find_new_buffer (0xc820), isp_mem_init (0xc8b0),
 * isp_malloc_buffer (0xc950) and isp_free_buffer (0xcae4).
 *
 * Pool block (t32_data_d96b0): +0 base address, +4 size (from private_get_isp_priv_mem), +12 20
 * descriptor slots of 20 B { u8 used, u8 slot taken, prev, next, addr, size }, +412 the head of
 * the address-ordered descriptor list.  One mutex (t32_data_d9850).  malloc rounds the size up
 * to 4 KiB and takes the first free block that fits (first fit, split off the rest); free marks
 * the block free and merges every free block with its free, adjacent successor.
 *
 * Vendor behaviour kept: isp_mem_init uses the first slot without a NULL check (crash when all
 * 20 slots are taken); a split that finds no free slot fails the allocation (returns 0).
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispmem_oracle).
 */
#ifndef TX_ISP_T32_ISPMEM_H
#define TX_ISP_T32_ISPMEM_H

#include "tx_isp_t32_ispcore.h"

#define T32_MEM_BASE		0
#define T32_MEM_SIZE		4
#define T32_MEM_SLOTS		12
#define T32_MEM_NSLOTS		20
#define T32_MEM_SLOT_SIZE	20
#define T32_MEM_HEAD		412
/* descriptor fields */
#define T32_MB_USED		0	/* u8 */
#define T32_MB_TAKEN		1	/* u8: the slot holds a descriptor */
#define T32_MB_PREV		4
#define T32_MB_NEXT		8
#define T32_MB_ADDR		12
#define T32_MB_LEN		16
#define T32_MEM_PAGE		4096u

struct t32_mem_ops {
	void (*get_priv_mem)(u32 base_p, u32 size_p);	/* private_get_isp_priv_mem */
	void (*mutex_init)(u32 m);
	void (*lock)(u32 m);
	void (*unlock)(u32 m);
	void (*log)(u32 line, u32 a4, u32 a5, u32 a6);	/* isp_printf(0, fmt, func, line, ...) */
};

struct t32_mem_glob {
	u32 pool;	/* t32_data_d96b0 */
	u32 mutex;	/* t32_data_d9850 */
};

static inline u8 t32_mb_r8(u32 b, u32 off)
{
	return *(volatile u8 *)T32_IC_P(b + off);
}

static inline void t32_mb_w8(u32 b, u32 off, u8 v)
{
	*(volatile u8 *)T32_IC_P(b + off) = v;
}

/* find_new_buffer(): the first free descriptor slot, cleared and taken; 0 when all are taken */
static inline u32 t32_find_new_buffer(const struct t32_mem_glob *g)
{
	unsigned int k;

	for (k = 0; k < T32_MEM_NSLOTS; k++) {
		u32 s = g->pool + T32_MEM_SLOTS + k * T32_MEM_SLOT_SIZE;

		if (!t32_mb_r8(s, T32_MB_TAKEN)) {
			memset(T32_IC_P(s), 0, T32_MEM_SLOT_SIZE);
			t32_mb_w8(s, T32_MB_TAKEN, 1);
			return s;
		}
	}
	return 0;
}

/* isp_mem_init(): one free block over the whole reserved area; returns the descriptor */
static inline u32 t32_isp_mem_init(const struct t32_mem_ops *ops, const struct t32_mem_glob *g)
{
	u32 b;

	ops->get_priv_mem(g->pool + T32_MEM_BASE, g->pool + T32_MEM_SIZE);
	ops->mutex_init(g->mutex);
	b = t32_find_new_buffer(g);
	t32_ic_w32(g->pool, T32_MEM_HEAD, b);
	t32_mb_w8(b, T32_MB_USED, 0);
	t32_ic_w32(b, T32_MB_PREV, 0);
	t32_ic_w32(b, T32_MB_NEXT, 0);
	t32_ic_w32(b, T32_MB_ADDR, t32_ic_r32(g->pool, T32_MEM_BASE));
	t32_ic_w32(b, T32_MB_LEN, t32_ic_r32(g->pool, T32_MEM_SIZE));
	return b;
}

/* isp_malloc_buffer(size): address of a 4 KiB rounded block, 0 on failure */
static inline u32 t32_isp_malloc_buffer(const struct t32_mem_ops *ops, const struct t32_mem_glob *g, u32 size)
{
	u32 len = (size + T32_MEM_PAGE - 1) & ~(T32_MEM_PAGE - 1), b, n;

	ops->log(57, t32_ic_r32(g->pool, T32_MEM_BASE), size, 0);
	if (!t32_ic_r32(g->pool, T32_MEM_BASE) || !size)
		return 0;
	ops->lock(g->mutex);
	b = t32_ic_r32(g->pool, T32_MEM_HEAD);
	ops->log(66, b, len, size);
	for (; b; b = t32_ic_r32(b, T32_MB_NEXT))
		if (!t32_mb_r8(b, T32_MB_USED) && t32_ic_r32(b, T32_MB_LEN) >= len)
			break;
	if (b && t32_ic_r32(b, T32_MB_LEN) > len) {	/* split: the rest becomes a new free block */
		n = t32_find_new_buffer(g);
		if (!n) {
			ops->unlock(g->mutex);
			return 0;
		}
		t32_ic_w32(n, T32_MB_ADDR, t32_ic_r32(b, T32_MB_ADDR) + len);
		t32_ic_w32(n, T32_MB_PREV, b);
		t32_ic_w32(n, T32_MB_LEN, t32_ic_r32(b, T32_MB_LEN) - len);
		if (t32_ic_r32(b, T32_MB_NEXT))
			t32_ic_w32(t32_ic_r32(b, T32_MB_NEXT), T32_MB_PREV, n);
		t32_ic_w32(n, T32_MB_NEXT, t32_ic_r32(b, T32_MB_NEXT));
		t32_ic_w32(b, T32_MB_NEXT, n);
	}
	if (b) {
		t32_ic_w32(b, T32_MB_LEN, len);
		t32_mb_w8(b, T32_MB_USED, 1);
	}
	ops->unlock(g->mutex);
	return b ? t32_ic_r32(b, T32_MB_ADDR) : 0;
}

/* isp_free_buffer(addr) */
static inline void t32_isp_free_buffer(const struct t32_mem_ops *ops, const struct t32_mem_glob *g, u32 addr)
{
	u32 b, nx;

	ops->lock(g->mutex);
	for (b = t32_ic_r32(g->pool, T32_MEM_HEAD); b; b = t32_ic_r32(b, T32_MB_NEXT))
		if (t32_mb_r8(b, T32_MB_USED) && t32_ic_r32(b, T32_MB_ADDR) == addr) {
			t32_mb_w8(b, T32_MB_USED, 0);
			break;
		}
	b = t32_ic_r32(g->pool, T32_MEM_HEAD);
	while (b) {
		nx = t32_ic_r32(b, T32_MB_NEXT);
		if (t32_mb_r8(b, T32_MB_USED) || !nx || t32_mb_r8(nx, T32_MB_USED) ||
		    t32_ic_r32(b, T32_MB_ADDR) + t32_ic_r32(b, T32_MB_LEN) != t32_ic_r32(nx, T32_MB_ADDR)) {
			b = nx;
			continue;
		}
		/* merge the free successor into b */
		t32_ic_w32(b, T32_MB_LEN, t32_ic_r32(nx, T32_MB_LEN) + t32_ic_r32(b, T32_MB_LEN));
		t32_ic_w32(b, T32_MB_NEXT, t32_ic_r32(nx, T32_MB_NEXT));
		if (t32_ic_r32(nx, T32_MB_NEXT))
			t32_ic_w32(t32_ic_r32(nx, T32_MB_NEXT), T32_MB_PREV, b);
		memset(T32_IC_P(nx), 0, T32_MEM_SLOT_SIZE);
		t32_mb_w8(nx, T32_MB_TAKEN, 0);
	}
	ops->unlock(g->mutex);
}

#endif /* TX_ISP_T32_ISPMEM_H */
