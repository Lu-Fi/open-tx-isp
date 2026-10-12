/* SPDX-License-Identifier: MIT */
/*
 * T41 MSCA tuning controls: AutoZoom (0x08000077), MaskBlock (0x08000074)
 * and ScalerLv (0x080000a6), after the stock libt41-firmware 1.2.6
 * (tisp_s/g_autozoom_control, tisp_msca_crop_api,
 * tisp_s/g_mscaler_mask_block_attr, tisp_msca_set_mask,
 * tisp_set_scaler_level_control_set) and the T41 1.2.6 libimp callers.
 * Kept free of kernel state for the host test; the driver glue and the
 * live/deferred policy are in tx_isp_t41_recovered.c (t41_tuning_autozoom
 * and friends) and driver/t41/README.md "MSCA zoom, mask and scaler level".
 *
 * Request layouts (all little endian, copied 1:1 from libimp):
 *  AutoZoom   60 bytes  IMPISPAutoZoom: s32 en[3], left[3], top[3],
 *                       width[3], height[3] (input-side crop window of
 *                       each output, sensor coordinates).
 *  MaskBlock  24 bytes  IMPISPMaskBlockAttr: u8 chx, u8 pinum, u8 en, pad,
 *                       u16 top, u16 left, u16 width, u16 height,
 *                       s32 type, u8 r,g,b, u8 y,u,v, 2 pad.  The kernel
 *                       only uses y,u,v: libimp converts RGB in place.
 *  ScalerLv   12 bytes  IMPISPScalerLvAttr: u8 chx, 3 pad, s32 mode,
 *                       u8 level, 3 pad.
 *
 * Stock per-output descriptor ("msca", 26 bytes per output, 3 outputs):
 *  +0 u8 enable, +4/+6 u16 output size, +8 u16 zoom lock (1 = AutoZoom
 *  owns +10..+16), +10/+12 u16 input crop x/y (register 0xf00a0 + ch * 8),
 *  +14/+16 u16 input crop size (scaling ratio source), +18..+24 output crop.
 */
#ifndef TX_ISP_T41_MSCA_CTL_H
#define TX_ISP_T41_MSCA_CTL_H

#define T41_MSCA_CTL_EINVAL	22
#define T41_MSCA_OUTPUTS	3U
#define T41_MSCA_DESC_STRIDE	26U
#define T41_MSCA_ZOOM_BYTES	60U
#define T41_MSCA_MASK_BYTES	24U
#define T41_MSCA_SCALER_LV_BYTES 12U
#define T41_MSCA_MASK_BLOCKS	4U
/* stock register masks of 0xf00a0 + ch * 8 (tisp_msca_init_chx_cfg) */
#define T41_MSCA_CROP_POS_MASK	0x0fff1fffU
#define T41_MSCA_CROP_X_MAX	0x0fffU
#define T41_MSCA_CROP_Y_MAX	0x1fffU

static inline unsigned int t41_msca_rd16(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static inline void t41_msca_wr16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static inline int t41_msca_rds32(const unsigned char *p)
{
	return (int)((unsigned int)p[0] | ((unsigned int)p[1] << 8) |
		     ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24));
}

static inline void t41_msca_wr32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

/* ---- AutoZoom ---------------------------------------------------------- */

/* field f (0 en, 1 left, 2 top, 3 width, 4 height) of output ch */
static inline int t41_msca_zoom_field(const unsigned char *req,
				      unsigned int f, unsigned int ch)
{
	return t41_msca_rds32(req + (f * T41_MSCA_OUTPUTS + ch) * 4U);
}

/*
 * Window check (not in stock, which programs whatever it gets): an enabled
 * window must be non-empty, inside the sensor input and representable in
 * the crop position register.  The whole request is rejected before
 * anything is stored.  in_w/in_h = 0 (sensor size unknown) rejects every
 * enabled window.
 */
static inline int t41_msca_zoom_check(const unsigned char *req,
				      unsigned int in_w, unsigned int in_h)
{
	unsigned int ch;

	for (ch = 0; ch < T41_MSCA_OUTPUTS; ++ch) {
		int left = t41_msca_zoom_field(req, 1, ch);
		int top = t41_msca_zoom_field(req, 2, ch);
		int w = t41_msca_zoom_field(req, 3, ch);
		int h = t41_msca_zoom_field(req, 4, ch);

		if (t41_msca_zoom_field(req, 0, ch) != 1)
			continue;
		if (left < 0 || top < 0 || w <= 0 || h <= 0)
			return -T41_MSCA_CTL_EINVAL;
		if ((unsigned int)left > T41_MSCA_CROP_X_MAX ||
		    (unsigned int)top > T41_MSCA_CROP_Y_MAX)
			return -T41_MSCA_CTL_EINVAL;
		if ((unsigned int)left + (unsigned int)w > in_w ||
		    (unsigned int)top + (unsigned int)h > in_h)
			return -T41_MSCA_CTL_EINVAL;
	}
	return 0;
}

/*
 * Stock tisp_s_autozoom_control: en == 1 locks the descriptor (+8 = 1) and
 * stores the window; any other value stores the full input (0, 0, in_w,
 * in_h) and leaves the lock byte as it was.  Returns a bit mask of the
 * outputs whose descriptor changed.
 */
static inline unsigned int t41_msca_zoom_store(unsigned char *desc,
					       const unsigned char *req,
					       unsigned int in_w,
					       unsigned int in_h)
{
	unsigned int changed = 0;
	unsigned int ch;

	for (ch = 0; ch < T41_MSCA_OUTPUTS; ++ch) {
		unsigned char *d = desc + ch * T41_MSCA_DESC_STRIDE;
		unsigned char before[10];
		unsigned int i;

		for (i = 0; i < sizeof(before); ++i)
			before[i] = d[8 + i];
		if (t41_msca_zoom_field(req, 0, ch) == 1) {
			t41_msca_wr16(d + 8, 1);
			t41_msca_wr16(d + 10, (unsigned int)t41_msca_zoom_field(req, 1, ch));
			t41_msca_wr16(d + 12, (unsigned int)t41_msca_zoom_field(req, 2, ch));
			t41_msca_wr16(d + 14, (unsigned int)t41_msca_zoom_field(req, 3, ch));
			t41_msca_wr16(d + 16, (unsigned int)t41_msca_zoom_field(req, 4, ch));
		} else {
			t41_msca_wr16(d + 10, 0);
			t41_msca_wr16(d + 12, 0);
			t41_msca_wr16(d + 14, in_w);
			t41_msca_wr16(d + 16, in_h);
		}
		for (i = 0; i < sizeof(before); ++i)
			if (before[i] != d[8 + i]) {
				changed |= 1U << ch;
				break;
			}
	}
	return changed;
}

/*
 * Stock tisp_g_autozoom_control: left/top/width/height of every output from
 * the descriptor; the en words are not written (stock returns its stack
 * buffer there, the open driver returns what the caller passed in).
 */
static inline void t41_msca_zoom_get(const unsigned char *desc,
				     unsigned char *req)
{
	unsigned int ch;

	for (ch = 0; ch < T41_MSCA_OUTPUTS; ++ch) {
		const unsigned char *d = desc + ch * T41_MSCA_DESC_STRIDE;
		unsigned int f;

		for (f = 1; f <= 4; ++f)
			t41_msca_wr32(req + (f * T41_MSCA_OUTPUTS + ch) * 4U,
				      t41_msca_rd16(d + 8 + f * 2U));
	}
}

/* ---- MaskBlock --------------------------------------------------------- */

struct t41_msca_mask_entry {
	unsigned int en;
	unsigned int left, top, width, height;
	unsigned int color;	/* y << 16 | u << 8 | v */
};

struct t41_msca_mask_state {
	struct t41_msca_mask_entry block[T41_MSCA_OUTPUTS * T41_MSCA_MASK_BLOCKS];
	unsigned int dirty;	/* bit ch * 4 + pinum: not written yet */
	unsigned int global_en;	/* 0xf0014 word, bit ch (stock only ORs) */
};

/*
 * Stock tisp_s_mscaler_mask_block_attr (it does not range-check chx/pinum;
 * the open driver rejects chx >= 3 / pinum >= 4 instead of writing past the
 * table).  en == 1 enables the block and stores window and colour; any
 * other value only disables it (window and colour keep their old values,
 * the block is still written again with size 0).
 */
static inline int t41_msca_mask_set(struct t41_msca_mask_state *s,
				    const unsigned char *req)
{
	unsigned int ch = req[0], pi = req[1];
	struct t41_msca_mask_entry *e;

	if (ch >= T41_MSCA_OUTPUTS || pi >= T41_MSCA_MASK_BLOCKS)
		return -T41_MSCA_CTL_EINVAL;
	e = &s->block[ch * T41_MSCA_MASK_BLOCKS + pi];
	s->dirty |= 1U << (ch * T41_MSCA_MASK_BLOCKS + pi);
	e->en = req[2] == 1;
	if (!e->en)
		return 0;
	e->top = t41_msca_rd16(req + 4);
	e->left = t41_msca_rd16(req + 6);
	e->width = t41_msca_rd16(req + 8);
	e->height = t41_msca_rd16(req + 10);
	e->color = ((unsigned int)req[19] << 16) |
		   ((unsigned int)req[20] << 8) | req[21];
	return 0;
}

/*
 * Stock tisp_g_mscaler_mask_block_attr: en is always reported 0; window
 * and y,u,v of an enabled block, zeros for a disabled one; type and r,g,b
 * are left alone (libimp fills them from its own cache).
 */
static inline int t41_msca_mask_get(const struct t41_msca_mask_state *s,
				    unsigned char *req)
{
	unsigned int ch = req[0], pi = req[1];
	const struct t41_msca_mask_entry *e;

	if (ch >= T41_MSCA_OUTPUTS || pi >= T41_MSCA_MASK_BLOCKS)
		return -T41_MSCA_CTL_EINVAL;
	e = &s->block[ch * T41_MSCA_MASK_BLOCKS + pi];
	req[2] = 0;
	if (e->en) {
		t41_msca_wr16(req + 4, e->top);
		t41_msca_wr16(req + 6, e->left);
		t41_msca_wr16(req + 8, e->width);
		t41_msca_wr16(req + 10, e->height);
		req[19] = (unsigned char)(e->color >> 16);
		req[20] = (unsigned char)(e->color >> 8);
		req[21] = (unsigned char)e->color;
	} else {
		t41_msca_wr32(req + 4, 0);
		t41_msca_wr32(req + 8, 0);
		req[19] = req[20] = req[21] = 0;
	}
	return 0;
}

/*
 * Output start: an ISP reset may have cleared the mask registers, so the
 * enabled blocks of the starting outputs are written again (open driver;
 * stock keeps them in its table and rewrites only on change).
 */
static inline void t41_msca_mask_mark_enabled(struct t41_msca_mask_state *s,
					      unsigned int chan_mask)
{
	unsigned int i;

	for (i = 0; i < T41_MSCA_OUTPUTS * T41_MSCA_MASK_BLOCKS; ++i)
		if ((chan_mask & (1U << (i / T41_MSCA_MASK_BLOCKS))) &&
		    s->block[i].en)
			s->dirty |= 1U << i;
}

typedef int (*t41_msca_pair_fn)(void *ctx, unsigned int value,
				unsigned int reg);

/*
 * Stock tisp_msca_set_mask, limited to the outputs in chan_mask (stock: all
 * three, every frame with a dirty block): per dirty block of an output the
 * (value, register) pairs
 *   left << 16 | top          -> 0xf0138 + ch * 0x100 + pinum * 12
 *   y << 16 | u << 8 | v      -> 0xf0140 + ...
 *   en ? width << 16 | height : 0 -> 0xf013c + ...
 * then the output's "any block enabled" bit ORed into the 0xf0014 word.
 * The dirty bits of the emitted outputs are cleared.  Returns the number
 * of pairs, or a negative write error (then nothing is marked clean).
 */
static inline int t41_msca_mask_emit(struct t41_msca_mask_state *s,
				     unsigned int chan_mask,
				     t41_msca_pair_fn write, void *ctx)
{
	unsigned int global = s->global_en;
	unsigned int dirty = s->dirty;
	unsigned int ch, pi;
	int pairs = 0;
	int ret;

	for (ch = 0; ch < T41_MSCA_OUTPUTS; ++ch) {
		unsigned int base = 0xf0138U + ch * 0x100U;
		unsigned int any = 0;

		if (!(chan_mask & (1U << ch)))
			continue;
		for (pi = 0; pi < T41_MSCA_MASK_BLOCKS; ++pi) {
			const struct t41_msca_mask_entry *e =
				&s->block[ch * T41_MSCA_MASK_BLOCKS + pi];
			unsigned int bit = 1U << (ch * T41_MSCA_MASK_BLOCKS + pi);
			unsigned int reg = base + pi * 12U;

			any |= e->en;
			if (!(dirty & bit))
				continue;
			dirty &= ~bit;
			ret = write(ctx, (e->left << 16) | (e->top & 0xffffU), reg);
			if (!ret)
				ret = write(ctx, e->color, reg + 8U);
			if (!ret)
				ret = write(ctx, e->en ? (e->width << 16) |
					    (e->height & 0xffffU) : 0, reg + 4U);
			if (ret)
				return ret;
			pairs += 3;
		}
		global |= any << ch;
		ret = write(ctx, global, 0xf0014U);
		if (ret)
			return ret;
		pairs++;
	}
	s->dirty = dirty;
	s->global_en = global;
	return pairs;
}

/* ---- ScalerLv ---------------------------------------------------------- */

struct t41_msca_scaler_lv {
	unsigned int valid;	/* a FITTING_CURVE level is set */
	unsigned int level;	/* 0..128 */
};

/* 0xf0028 from the six mode bytes (tisp_msca_scaling_algorithm) */
static inline unsigned int t41_msca_scale_mode(const unsigned char *params)
{
	return (params[0xdf] & 1U) | ((unsigned int)(params[0xe1] & 1U) << 1) |
	       ((unsigned int)(params[0xe3] & 1U) << 2) |
	       ((unsigned int)(params[0xe0] & 1U) << 4) |
	       ((unsigned int)(params[0xe2] & 1U) << 5) |
	       ((unsigned int)(params[0xe4] & 1U) << 6);
}

/*
 * Stock tisp_set_scaler_level_control_set, state part:
 *  chx >= 3 -> error (stock -1);
 *  mode 1 (FIXED_WEIGHT): both mode bytes of the output = 1;
 *  mode 0 (FITTING_CURVE): level > 128 -> error, else both bytes = 0 and
 *    the level goes into the low 16 bits of 0xf0708 + ch * 8;
 *  any other mode: nothing changes (stock still requests an update).
 * Stock writes the level only into the register, so the next channel
 * reload drops it; the open driver keeps it in lv[] and puts it back at
 * every reload (t41_msca_scaler_lv_curve) - that is the "store, apply at
 * stream start" part.  Returns the output index or a negative error.
 */
static inline int t41_msca_scaler_lv_set(unsigned char *params,
					 struct t41_msca_scaler_lv *lv,
					 const unsigned char *req)
{
	unsigned int ch = req[0];
	int mode = t41_msca_rds32(req + 4);
	unsigned int level = req[8];

	if (ch >= T41_MSCA_OUTPUTS)
		return -T41_MSCA_CTL_EINVAL;
	if (mode == 1) {
		params[0xdf + ch * 2U] = 1;
		params[0xe0 + ch * 2U] = 1;
		lv[ch].valid = 0;
	} else if (mode == 0) {
		if (level > 128)
			return -T41_MSCA_CTL_EINVAL;
		params[0xdf + ch * 2U] = 0;
		params[0xe0 + ch * 2U] = 0;
		lv[ch].valid = 1;
		lv[ch].level = level;
	}
	return (int)ch;
}

/* 0xf0708 + ch * 8 with a stored level (stock keeps bits 16..24, 28..31) */
static inline unsigned int t41_msca_scaler_lv_curve(unsigned int curve_control,
						     const struct t41_msca_scaler_lv *lv)
{
	if (!lv || !lv->valid)
		return curve_control;
	return (curve_control & 0xf1ff0000U) | (lv->level << 8) | lv->level;
}

/* ---- live or deferred -------------------------------------------------- */

enum t41_msca_live_step {
	T41_MSCA_IDLE = 0,	/* output not streaming: store, next start applies */
	T41_MSCA_DEFER = 1,	/* streaming, live update not proven safe: store */
	T41_MSCA_LIVE = 2,	/* reprogram now: output off, latch, output on */
};

/*
 * A streaming output is reprogrammed only with the latch-while-off start
 * sequence (t41_msca_cfg_update=2 with t41_msca_stop_disable=1) and only
 * inside the size envelope that was stable on the device (output
 * <= max_w x max_h, 768x432 by default; 960x540 and larger hung the SoC
 * when started mid-stream, driver/t41/README.md "Known issues").
 */
static inline enum t41_msca_live_step
t41_msca_live_plan(int running, int cfg_update, int stop_disable,
		   unsigned int out_w, unsigned int out_h,
		   unsigned int max_w, unsigned int max_h)
{
	if (!running)
		return T41_MSCA_IDLE;
	if (cfg_update < 2 || stop_disable <= 0)
		return T41_MSCA_DEFER;
	if (!out_w || !out_h || out_w > max_w || out_h > max_h)
		return T41_MSCA_DEFER;
	return T41_MSCA_LIVE;
}

#endif /* TX_ISP_T41_MSCA_CTL_H */
