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

/*
 * Mask (cover) blocks, stored by SetMaskBlock (0x08000183,
 * tx_isp_t23_tuning_ext.h): mscaler @1836 + 16 (4 c + i), channel c 0..2,
 * block i 0..3: u8 enable, u16 left @2, top @4, width @6, height @8,
 * u32 colour @12 (Y << 16 | U << 8 | V); dirty word @2308, bit 4 c + i.
 *
 * stock tisp_msca_api_set_mask (libt23-firmware 1.3.0, called by
 * tisp_msca_set_omi_api after the OSD pictures when the dirty word is not
 * zero) emits, per channel c and per dirty block i, the pairs
 *   left << 16 | top                      -> 0x400 + 64 c + 16 i
 *   colour                                -> 0x408 + 64 c + 16 i
 *   width << 16 | height (0 if disabled)  -> 0x404 + 64 c + 16 i
 * and after the four blocks of each channel the mask word -> 0x044: the
 * channel bits (bit c = one of the channel's blocks enabled) OR-ed into a
 * word that is never cleared.  Kept as stock (audit/mask_commit_emu.py: the
 * write list is identical): a block is removed by its zero size, which on
 * the camera removes the cover (.30, 2026-10-29: 123 -> 235 shown, 123 off).
 */
#define T23_MASK_OFF		1836U
#define T23_MASK_STRIDE		16U
#define T23_MASK_DIRTY_OFF	2308U
#define T23_MASK_CHANNELS	3U
#define T23_MASK_PER_CHANNEL	4U
#define T23_MASK_REG_BLOCK	0x400U
#define T23_MASK_REG_ENABLE	0x044U
/* the most pairs one mask update emits: 12 blocks x 3 + 3 enable words */
#define T23_MASK_MAX_PAIRS	(3U * T23_MASK_CHANNELS * T23_MASK_PER_CHANNEL + T23_MASK_CHANNELS)

static inline const uint8_t *t23_mask_block(const uint8_t *m, unsigned int idx)
{
	return m + T23_MASK_OFF + T23_MASK_STRIDE * idx;
}

static inline unsigned int t23_mask_dirty(const uint8_t *m)
{
	return t23_osd_rd32(m + T23_MASK_DIRTY_OFF) &
	       ((1U << (T23_MASK_CHANNELS * T23_MASK_PER_CHANNEL)) - 1U);
}

static inline unsigned int t23_mask_channel_on(const uint8_t *m, unsigned int c)
{
	unsigned int i;

	for (i = 0; i < T23_MASK_PER_CHANNEL; ++i)
		if (t23_mask_block(m, c * T23_MASK_PER_CHANNEL + i)[0] == 1U)
			return 1U;
	return 0U;
}

/* Output start: an ISP reset may have cleared the mask registers. */
static inline void t23_mask_mark_enabled(uint8_t *m)
{
	unsigned int idx, mask = 0;

	for (idx = 0; idx < T23_MASK_CHANNELS * T23_MASK_PER_CHANNEL; ++idx)
		if (t23_mask_block(m, idx)[0] == 1U)
			mask |= 1U << idx;
	if (mask)
		t23_osd_wr32(m + T23_MASK_DIRTY_OFF,
			     t23_osd_rd32(m + T23_MASK_DIRTY_OFF) | mask);
}

/*
 * tisp_msca_api_set_mask: nothing when no block is dirty, else the pairs
 * above in the stock order; *word is the static mask word (stock mask_en);
 * the dirty bits are cleared when all went out.
 * Returns the number of pairs, 0 for nothing to do, or a negative write
 * error (dirty bits kept).
 */
static inline int t23_mask_emit(uint8_t *m, t23_osd_pair_fn write, void *ctx,
				unsigned int *word)
{
	unsigned int dirty = t23_mask_dirty(m);
	unsigned int c, i;
	int pairs = 0;
	int ret;

	if (!dirty)
		return 0;
	for (c = 0; c < T23_MASK_CHANNELS; ++c) {
		for (i = 0; i < T23_MASK_PER_CHANNEL; ++i) {
			unsigned int idx = c * T23_MASK_PER_CHANNEL + i;
			unsigned int reg = T23_MASK_REG_BLOCK + 64U * c + 16U * i;
			const uint8_t *b = t23_mask_block(m, idx);

			if (!(dirty & (1U << idx)))
				continue;
			ret = write(ctx, (t23_osd_rd16(b + 2) << 16) | t23_osd_rd16(b + 4), reg);
			if (!ret)
				ret = write(ctx, t23_osd_rd32(b + 12), reg + 8U);
			if (!ret)
				ret = write(ctx, b[0] ? (t23_osd_rd16(b + 6) << 16) | t23_osd_rd16(b + 8) : 0U,
					    reg + 4U);
			if (ret)
				return ret;
			pairs += 3;
		}
		*word |= t23_mask_channel_on(m, c) << c;
		ret = write(ctx, *word, T23_MASK_REG_ENABLE);
		if (ret)
			return ret;
		pairs++;
	}
	t23_osd_wr32(m + T23_MASK_DIRTY_OFF, t23_osd_rd32(m + T23_MASK_DIRTY_OFF) & ~dirty);
	return pairs;
}

/*
 * Draw blocks (lines / windows), stored by SetDrawBlock (0x08000180,
 * tx_isp_t23_tuning_ext.h): six 24-byte entries at mscaler @2028 + 24 n:
 *   +0 u8 type (0 window, 1 range window, 2 line), +4 u8 flags,
 *   +6 u16 x0, +8 y0, +10 x1, +12 y1, +16 u32 colour (24 bit), +20 u8 alpha
 *   (line: also the thickness of a horizontal / vertical line), +21 u8 line
 *   width, +22 u16 extra (type 1); dirty word @2312, bit n.
 *
 * stock tisp_msca_api_set_line (libt23-firmware 1.3.0, after the masks in
 * tisp_msca_set_omi_api) emits per dirty block n of type 0..2 four pairs
 *   start x << 16 | y          -> 0x4c0 + 16 n
 *   end / size                 -> 0x4c4 + 16 n
 *   colour | alpha << 24       -> 0x4c8 + 16 n
 *   control                    -> 0x4cc + 16 n
 * type 0: (x0,y0), (x1,y1), control = width << 8 | flags;
 * type 1: the same points, control = extra << 16 | width << 8 |
 *         type << 4 | flags;
 * type 2: start = (smaller-x end point's x, min y), size = |dx| << 16 |
 *         dy (16-bit signed; for a horizontal or vertical line |dx| / |dy|
 *         with a zero replaced by the alpha byte, control = width << 8 |
 *         flags); a sloped line: control = (flags | 0x20) | width << 8 |
 *         (octant << 9 | slope * 256) << 16, octant / slope 0 and dy
 *         unsigned-folded only when the flag byte is not 0.  Other types: the dirty bit is cleared,
 *         nothing written (stock).
 */
#define T23_DRAW_OFF		2028U
#define T23_DRAW_STRIDE		24U
#define T23_DRAW_DIRTY_OFF	2312U
#define T23_DRAW_BLOCKS		6U
#define T23_DRAW_REG_BLOCK	0x4c0U
#define T23_DRAW_MAX_PAIRS	(4U * T23_DRAW_BLOCKS)

static inline unsigned int t23_draw_dirty(const uint8_t *m)
{
	return t23_osd_rd32(m + T23_DRAW_DIRTY_OFF) & ((1U << T23_DRAW_BLOCKS) - 1U);
}

static inline int t23_draw_emit(uint8_t *m, t23_osd_pair_fn write, void *ctx)
{
	unsigned int dirty = t23_draw_dirty(m), n;
	int pairs = 0;

	if (!dirty)
		return 0;
	for (n = 0; n < T23_DRAW_BLOCKS; ++n) {
		const uint8_t *e = m + T23_DRAW_OFF + T23_DRAW_STRIDE * n;
		unsigned int reg = T23_DRAW_REG_BLOCK + 16U * n;
		unsigned int x0 = t23_osd_rd16(e + 6), y0 = t23_osd_rd16(e + 8);
		unsigned int x1 = t23_osd_rd16(e + 10), y1 = t23_osd_rd16(e + 12);
		unsigned int colour = (t23_osd_rd32(e + 16) & 0xffffffU) | ((unsigned int)e[20] << 24);
		unsigned int start, size, ctrl;
		int ret;

		if (!(dirty & (1U << n)))
			continue;
		t23_osd_wr32(m + T23_DRAW_DIRTY_OFF, t23_osd_rd32(m + T23_DRAW_DIRTY_OFF) & ~(1U << n));
		if (e[0] == 0U) {
			start = x0 << 16 | y0;
			size = x1 << 16 | y1;
			ctrl = (unsigned int)e[21] << 8 | e[4];
		} else if (e[0] == 1U) {
			start = x0 << 16 | y0;
			size = x1 << 16 | y1;
			ctrl = t23_osd_rd16(e + 22) << 16 | (unsigned int)e[21] << 8 |
			       (unsigned int)e[0] << 4 | e[4];
		} else if (e[0] == 2U) {
			int32_t dx = (int16_t)(uint16_t)(x1 - x0), dy = (int16_t)(uint16_t)(y1 - y0);
			unsigned int sx = x0, sy = y1 < y0 ? y1 : y0;

			if (x1 < x0) {
				/* stock negates the 16-bit masked differences */
				dx = (int16_t)(uint16_t)(0U - ((x1 - x0) & 0xffffU));
				dy = (int16_t)(uint16_t)(0U - ((y1 - y0) & 0xffffU));
				sx = x1;
			}
			if (dy == 0 || dx == 0) {
				dy = (int16_t)(dy < 0 ? -dy : dy);
				if (dy == 0)
					dy = e[20];
				if (dx == 0)
					dx = e[20];
				ctrl = (unsigned int)e[21] << 8 | e[4];
			} else {
				unsigned int oct = 0, slope = 0;

				if (e[4] == 0U)
					;       /* stock: no octant / slope, dy keeps its sign */
				else if (dy < 0) {
					dy = (int16_t)-dy;
					if (dx < dy) { slope = (unsigned int)((dx << 8) / dy); oct = 3; }
					else { slope = (unsigned int)((dy << 8) / dx); oct = 1; }
				} else if (dx < dy) {
					slope = (unsigned int)((dx << 8) / dy); oct = 2;
				} else {
					slope = (unsigned int)((dy << 8) / dx); oct = 0;
				}
				ctrl = ((e[4] | 0x20U) & 0xffU) | (unsigned int)e[21] << 8 |
				       ((oct << 9 | (slope & 0xffffU)) << 16);
			}
			start = sx << 16 | (sy & 0xffffU);
			size = ((unsigned int)dx << 16) | (unsigned int)dy;
		} else {
			continue;
		}
		ret = write(ctx, start, reg);
		if (!ret)
			ret = write(ctx, size, reg + 4U);
		if (!ret)
			ret = write(ctx, colour, reg + 8U);
		if (!ret)
			ret = write(ctx, ctrl, reg + 12U);
		if (ret)
			return ret;
		pairs += 4;
	}
	return pairs;
}

#endif /* TX_ISP_T23_OSD_H */
