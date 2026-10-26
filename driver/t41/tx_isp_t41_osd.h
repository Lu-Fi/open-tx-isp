/* SPDX-License-Identifier: MIT */
/*
 * T41 ISP-drawn OSD: OSD attribute (0x080000a1) and OSD block (0x080000a2),
 * rebuilt as plain C from the behaviour of the stock libt41-firmware 1.2.6
 * routines tisp_s/g_osd_attr, tisp_s/g_osd_block_attr and
 * tisp_msca_set_osd (called by tisp_msca_cfg_shd_ram from the per-frame
 * tisp_msca_Shd_ctrl) and of their libimp 1.2.6 callers
 * IMP_ISP_Tuning_Set/GetOSDAttr, Set/GetOSDBlock and OsdnotifyIsp.
 * Kept free of kernel state for the host test; the driver glue is in
 * tx_isp_t41_recovered.c (t41_tuning_osd_attr / _block,
 * t41_msca_commit_frame_shadow) and driver/t41/README.md "ISP OSD".
 *
 * Request layouts (little endian, copied 1:1 from libimp):
 *  OSD attribute 12 bytes IMPISPOSDAttr: s32 osd_type (0 ARGB8888,
 *                         1 ARGB1555), s32 osd_argb_type (component
 *                         order, 0..5 / 8..13), s32 osd_pixel_alpha_disable.
 *                         Stock keeps the low byte of each.
 *  OSD block     20 bytes IMPISPOSDBlockAttr: u8 pinum (0..7), u8 enable,
 *                         u16 left, top, width, height, 2 pad, u32 picture
 *                         (physical address; libimp OsdnotifyIsp converts
 *                         its rmem copy), u16 stride (bytes per line), pad.
 *
 * MSCA OSD registers (written through the MSCA shadow port 0xf8004 as
 * value/register pairs, inside the 0xf8000 = 0x101 / 0x01010102 bracket):
 *  0xf0064               component order (osd_argb_type)
 *  0xf0600 + 16 * n      left << 16 | top          of picture n
 *  0xf0604 + 16 * n      width << 16 | height
 *  0xf0608 + 16 * n      picture address
 *  0xf060c + 16 * n      stride
 *  0xf0018               enable bit n per picture
 *  0xf0060               osd_type | 0x70 | alpha_disable << 8
 * The same register block exists on T40 at 0x16000 (stock T40
 * Tzn_Msca_Shd_osd: 0x16600 + 16 * n, 0x16060 | 0x70); T40 programs one
 * picture per call with its output channel in the request, T41 has one bank
 * and splits format and picture into the two controls above.
 *
 * Differences to stock (all in the direction of "does what the call says"):
 *  - pinum >= 8 is refused (stock indexes past its table).
 *  - Stock first compares the STORED width/height of the block with the
 *    limits (a stale value from an earlier, larger region makes it log and
 *    return 0 without storing); here only the new request is checked.
 *  - A request outside the limits returns -ERANGE (stock logs, the s_ctrl
 *    drops its -1 and the user call returns 0).
 *  - The enable word 0xf0018 follows the blocks: stock only ever ORs bits
 *    in, so a hidden picture (or all pictures after a format change) kept
 *    its enable bit.
 *  - GetOSDBlock reports the requested block: stock clears the request
 *    before it reads pinum, so it always reports block 0.
 */
#ifndef TX_ISP_T41_OSD_H
#define TX_ISP_T41_OSD_H

#define T41_OSD_EINVAL		22
#define T41_OSD_ERANGE		34
#define T41_OSD_ATTR_BYTES	12U
#define T41_OSD_BLOCK_BYTES	20U
#define T41_OSD_BLOCKS		8U
#define T41_OSD_REG_ORDER	0xf0064U
#define T41_OSD_REG_CFG		0xf0060U
#define T41_OSD_REG_ENABLE	0xf0018U
#define T41_OSD_REG_BLOCK	0xf0600U
#define T41_OSD_CFG_FIXED	0x70U
#define T41_OSD_KSEG0		0x80000000U

struct t41_osd_block {
	unsigned int en;
	unsigned int left, top, width, height;
	unsigned int addr, stride;
};

struct t41_osd_state {
	unsigned int type, order, alpha_dis;	/* low bytes, as stock */
	struct t41_osd_block block[T41_OSD_BLOCKS];
	unsigned int dirty;	/* bit n: block n not written yet */
};

static inline unsigned int t41_osd_rd16(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static inline unsigned int t41_osd_rd32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
	       ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static inline void t41_osd_wr16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static inline void t41_osd_wr32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

/*
 * tisp_s_osd_attr: a new osd_type switches every picture off and marks all
 * eight dirty (the pictures must be sent again in the new format); then the
 * low bytes of the three words are kept.
 */
static inline void t41_osd_attr_set(struct t41_osd_state *s,
				    const unsigned char *req)
{
	unsigned int type = t41_osd_rd32(req) & 0xffU;
	unsigned int i;

	if (s->type != type) {
		for (i = 0; i < T41_OSD_BLOCKS; ++i)
			s->block[i].en = 0;
		s->dirty = 0xffU;
	}
	s->type = type;
	s->order = t41_osd_rd32(req + 4) & 0xffU;
	s->alpha_dis = t41_osd_rd32(req + 8) & 0xffU;
}

/* tisp_g_osd_attr */
static inline void t41_osd_attr_get(const struct t41_osd_state *s,
				    unsigned char *req)
{
	t41_osd_wr32(req, s->type);
	t41_osd_wr32(req + 4, s->order);
	t41_osd_wr32(req + 8, s->alpha_dis);
}

/*
 * tisp_s_osd_block_attr.  lim_w/lim_h: the output-crop size of MSCA output
 * 0 (stock msca descriptor +22/+24), i.e. the main stream size.  enable == 1
 * stores the picture (left + width <= lim_w, top + height <= lim_h); any
 * other value switches it off and clears it.  The block is marked dirty in
 * both cases.
 */
static inline int t41_osd_block_set(struct t41_osd_state *s,
				    const unsigned char *req,
				    unsigned int lim_w, unsigned int lim_h)
{
	unsigned int n = req[0];
	struct t41_osd_block *b;

	if (n >= T41_OSD_BLOCKS)
		return -T41_OSD_EINVAL;
	b = &s->block[n];
	if (req[1] == 1U) {
		unsigned int left = t41_osd_rd16(req + 2);
		unsigned int top = t41_osd_rd16(req + 4);
		unsigned int width = t41_osd_rd16(req + 6);
		unsigned int height = t41_osd_rd16(req + 8);

		if (left + width > lim_w || top + height > lim_h)
			return -T41_OSD_ERANGE;
		b->en = 1;
		b->left = left;
		b->top = top;
		b->width = width;
		b->height = height;
		b->addr = t41_osd_rd32(req + 12);
		b->stride = t41_osd_rd16(req + 16);
	} else {
		b->en = 0;
		b->left = b->top = b->width = b->height = 0;
		b->addr = b->stride = 0;
	}
	s->dirty |= 1U << n;
	return 0;
}

/*
 * tisp_g_osd_block_attr for the block req[0]: zeros for a switched-off
 * block, else enable 1, the geometry, the address as a KSEG0 address
 * (stock adds 0x80000000) and the stride.
 */
static inline int t41_osd_block_get(const struct t41_osd_state *s,
				    unsigned char *req)
{
	unsigned int n = req[0];
	const struct t41_osd_block *b;

	if (n >= T41_OSD_BLOCKS)
		return -T41_OSD_EINVAL;
	b = &s->block[n];
	{
		unsigned int i;

		for (i = 1; i < T41_OSD_BLOCK_BYTES; ++i)
			req[i] = 0;
	}
	if (!b->en)
		return 0;
	req[1] = 1;
	t41_osd_wr16(req + 2, b->left);
	t41_osd_wr16(req + 4, b->top);
	t41_osd_wr16(req + 6, b->width);
	t41_osd_wr16(req + 8, b->height);
	t41_osd_wr32(req + 12, b->addr + T41_OSD_KSEG0);
	t41_osd_wr16(req + 16, b->stride);
	return 0;
}

/* Output start: an ISP reset may have cleared the OSD registers. */
static inline void t41_osd_mark_enabled(struct t41_osd_state *s)
{
	unsigned int i;

	for (i = 0; i < T41_OSD_BLOCKS; ++i)
		if (s->block[i].en)
			s->dirty |= 1U << i;
}

static inline unsigned int t41_osd_enable_mask(const struct t41_osd_state *s)
{
	unsigned int i, mask = 0;

	for (i = 0; i < T41_OSD_BLOCKS; ++i)
		if (s->block[i].en)
			mask |= 1U << i;
	return mask;
}

typedef int (*t41_osd_pair_fn)(void *ctx, unsigned int value,
			       unsigned int reg);

/*
 * tisp_msca_set_osd: nothing when no block is dirty, else the
 * (value, register) pairs
 *   order -> 0xf0064,
 *   per dirty block n: left << 16 | top, width << 16 | height, address,
 *   stride -> 0xf0600 + 16 * n + 0/4/8/12,
 *   enable mask -> 0xf0018,
 *   type | 0x70 | alpha_disable << 8 -> 0xf0060.
 * The dirty bits are cleared when all pairs went out.  Returns the number
 * of pairs, 0 for nothing to do, or a negative write error.
 */
static inline int t41_osd_emit(struct t41_osd_state *s, t41_osd_pair_fn write,
			       void *ctx)
{
	unsigned int n;
	int pairs = 0;
	int ret;

	if (!(s->dirty & 0xffU))
		return 0;
	ret = write(ctx, s->order, T41_OSD_REG_ORDER);
	if (ret)
		return ret;
	pairs++;
	for (n = 0; n < T41_OSD_BLOCKS; ++n) {
		const struct t41_osd_block *b = &s->block[n];
		unsigned int reg = T41_OSD_REG_BLOCK + 16U * n;

		if (!(s->dirty & (1U << n)))
			continue;
		ret = write(ctx, (b->left << 16) | (b->top & 0xffffU), reg);
		if (!ret)
			ret = write(ctx, (b->width << 16) | (b->height & 0xffffU),
				    reg + 4U);
		if (!ret)
			ret = write(ctx, b->addr, reg + 8U);
		if (!ret)
			ret = write(ctx, b->stride & 0xffffU, reg + 12U);
		if (ret)
			return ret;
		pairs += 4;
	}
	ret = write(ctx, t41_osd_enable_mask(s), T41_OSD_REG_ENABLE);
	if (!ret)
		ret = write(ctx, (s->type & 0xffU) | T41_OSD_CFG_FIXED |
			    ((s->alpha_dis & 0xffU) << 8), T41_OSD_REG_CFG);
	if (ret)
		return ret;
	pairs += 2;
	s->dirty = 0;
	return pairs;
}

#endif /* TX_ISP_T41_OSD_H */
