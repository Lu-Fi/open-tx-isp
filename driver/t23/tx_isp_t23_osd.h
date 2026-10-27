/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T23 ISP-drawn OSD: the MSCA programming of the pictures stored by
 * SetOSDAttr (0x08000181) and SetOSDBlock (0x08000182).
 *
 * Rebuilt as plain C from the behaviour of the stock libt23-firmware 1.3.0
 * routines tisp_msca_api_set_osd and tisp_msca_set_omi_api (called by
 * tisp_msca_Shd_ctrl from the stock ispcore_irq_thread_handle while the
 * core runs).  The request handling (where the pictures are stored in the
 * mscaler object) is in tx_isp_t23_tuning_ext.h; this file only reads that
 * state and emits the register writes.  Driver glue: tx_isp_t23_core.c
 * (t23_osd_commit, called from the core frame-done interrupt), README
 * "ISP OSD".
 *
 * mscaler object (stock .bss, 2316 bytes), OSD part:
 *   @2300 osd_type (0 ARGB8888, 1 ARGB1555), @2301 component order,
 *   @2302 pixel alpha disable                     (low bytes, SetOSDAttr)
 *   @2172 + 16 n  block n: u16 left, top, width, height, u32 picture
 *                 (physical address), u16 stride @+12, u8 enable @+14
 *   @2304         dirty word, bit n: block n not written yet
 *
 * MSCA OSD registers, written through the MSCA shadow port 0xe004 as
 * value/register pairs inside the 0xe000 = 0x101 / 0x01010102 bracket and
 * started with 0xe100 = (pairs - 1) << 4 | 1 (the same port the MSCA curve
 * loader uses):
 *   0x754              component order
 *   0x760 + 16 n       left << 16 | top          of picture n
 *   0x764 + 16 n       width << 16 | height
 *   0x768 + 16 n       picture address
 *   0x76c + 16 n       stride (bytes per line)
 *   0x048              enable bit n per picture
 *   0x750              osd_type | 0x70 | alpha_disable << 8
 * The same block as T41 (0xf0064 / 0xf0600 + 16 n / 0xf0018 / 0xf0060,
 * driver/t41/tx_isp_t41_osd.h) and T40 (0x16xxx).
 *
 * Differences to stock (in the direction of "does what the call says"):
 *  - The enable word follows the stored blocks.  Stock only ever ORs the
 *    enable bytes into a static word, so a hidden picture (enable 0, size
 *    and address zeroed) and every picture after a format change kept its
 *    enable bit.
 *  - Nothing is emitted while the shadow port is busy (0xe100 / 0xe00c
 *    bit 1, checked by the caller) or used by the curve loader: the dirty
 *    bits stay and the next frame retries.  Stock logs and drops the
 *    whole update on busy, leaving the dirty bits for the next frame too.
 */
#ifndef TX_ISP_T23_OSD_H
#define TX_ISP_T23_OSD_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define T23_OSD_BLOCKS		8U
#define T23_OSD_TYPE_OFF	2300U
#define T23_OSD_ORDER_OFF	2301U
#define T23_OSD_ALPHA_OFF	2302U
#define T23_OSD_DIRTY_OFF	2304U
#define T23_OSD_BLOCK_OFF	2172U
#define T23_OSD_BLOCK_STRIDE	16U
#define T23_OSD_EN_BYTE		14U

#define T23_OSD_REG_ORDER	0x754U
#define T23_OSD_REG_CFG		0x750U
#define T23_OSD_REG_ENABLE	0x048U
#define T23_OSD_REG_BLOCK	0x760U
#define T23_OSD_CFG_FIXED	0x70U

/* MSCA shadow port (stock tisp_msca_set_omi_api) */
#define T23_MSCA_SHD_CTRL	0xe000U
#define T23_MSCA_SHD_PORT	0xe004U
#define T23_MSCA_SHD_STATUS	0xe00cU
#define T23_MSCA_SHD_KICK	0xe100U
#define T23_MSCA_SHD_OPEN	0x00000101U
#define T23_MSCA_SHD_CLOSE	0x01010102U
/* the most pairs one OSD update emits: order, 8 x 4, enable, cfg */
#define T23_OSD_MAX_PAIRS	(1U + 4U * T23_OSD_BLOCKS + 2U)

static inline unsigned int t23_osd_rd16(const uint8_t *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static inline unsigned int t23_osd_rd32(const uint8_t *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
	       ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static inline void t23_osd_wr32(uint8_t *p, unsigned int v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static inline const uint8_t *t23_osd_block(const uint8_t *m, unsigned int n)
{
	return m + T23_OSD_BLOCK_OFF + T23_OSD_BLOCK_STRIDE * n;
}

static inline unsigned int t23_osd_dirty(const uint8_t *m)
{
	return t23_osd_rd32(m + T23_OSD_DIRTY_OFF) & 0xffU;
}

static inline unsigned int t23_osd_enable_mask(const uint8_t *m)
{
	unsigned int n, mask = 0;

	for (n = 0; n < T23_OSD_BLOCKS; ++n)
		if (t23_osd_block(m, n)[T23_OSD_EN_BYTE] == 1U)
			mask |= 1U << n;
	return mask;
}

/* Output start: an ISP reset may have cleared the OSD registers. */
static inline void t23_osd_mark_enabled(uint8_t *m)
{
	unsigned int mask = t23_osd_enable_mask(m);

	if (mask)
		t23_osd_wr32(m + T23_OSD_DIRTY_OFF,
			     t23_osd_rd32(m + T23_OSD_DIRTY_OFF) | mask);
}

typedef int (*t23_osd_pair_fn)(void *ctx, unsigned int value,
			       unsigned int reg);

/*
 * tisp_msca_api_set_osd: nothing when no block is dirty, else the
 * (value, register) pairs
 *   order -> 0x754,
 *   per dirty block n: left << 16 | top, width << 16 | height, address,
 *   stride -> 0x760 + 16 n + 0/4/8/12,
 *   enable mask -> 0x048,
 *   type | 0x70 | alpha_disable << 8 -> 0x750.
 * The dirty bits 0..7 are cleared when all pairs went out (bits above 7
 * are kept, as stock).  Returns the number of pairs, 0 for nothing to do,
 * or a negative write error (dirty bits kept).
 */
static inline int t23_osd_emit(uint8_t *m, t23_osd_pair_fn write, void *ctx)
{
	unsigned int dirty = t23_osd_dirty(m);
	unsigned int n;
	int pairs = 0;
	int ret;

	if (!dirty)
		return 0;
	ret = write(ctx, m[T23_OSD_ORDER_OFF], T23_OSD_REG_ORDER);
	if (ret)
		return ret;
	pairs++;
	for (n = 0; n < T23_OSD_BLOCKS; ++n) {
		const uint8_t *b = t23_osd_block(m, n);
		unsigned int reg = T23_OSD_REG_BLOCK + 16U * n;

		if (!(dirty & (1U << n)))
			continue;
		ret = write(ctx, (t23_osd_rd16(b) << 16) | t23_osd_rd16(b + 2),
			    reg);
		if (!ret)
			ret = write(ctx, (t23_osd_rd16(b + 4) << 16) |
					 t23_osd_rd16(b + 6), reg + 4U);
		if (!ret)
			ret = write(ctx, t23_osd_rd32(b + 8), reg + 8U);
		if (!ret)
			ret = write(ctx, t23_osd_rd16(b + 12), reg + 12U);
		if (ret)
			return ret;
		pairs += 4;
	}
	ret = write(ctx, t23_osd_enable_mask(m), T23_OSD_REG_ENABLE);
	if (!ret)
		ret = write(ctx, ((m[T23_OSD_TYPE_OFF] | T23_OSD_CFG_FIXED) &
				  0xffU) |
				 ((unsigned int)m[T23_OSD_ALPHA_OFF] << 8),
			    T23_OSD_REG_CFG);
	if (ret)
		return ret;
	pairs += 2;
	t23_osd_wr32(m + T23_OSD_DIRTY_OFF,
		     t23_osd_rd32(m + T23_OSD_DIRTY_OFF) & ~0xffU);
	return pairs;
}

#endif /* TX_ISP_T23_OSD_H */
