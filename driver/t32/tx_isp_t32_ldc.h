/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 lens distortion correction (LDC) from the vendor module (Matt Davis'
 * recovered C, PR #15): lib_tisp_ldc_get_mem_size, tisp_ldc_mem_cfg,
 * tisp_ldc_reg_cfg, tisp_ldc_init, tisp_ldc_deinit, tisp_ldc_dma_cfg,
 * tisp_ldc_dma_stride, tisp_ldc_load_reg_cfg, tisp_ldc_reg_reflash,
 * tisp_ldc_reg_all_reflash, lib_tisp_ldc_intp_handle,
 * tisp_ldc_param_array_get / _set and tisp_ldc_wake_up.
 *
 * Tuning data: the IQ tuning area of the channel (t32_data_dd0a4[ch]) is read
 * at +0xe800 .. +0xec2f (10 words, 4 halfwords, two 512 byte tables); the IQ
 * section id is not derived here.  Register offsets are relative to the LDC
 * block (system_reg_ldc_write adds the base).
 *
 * Per channel the vendor keeps a 3240 byte context with three 1076 byte
 * parameter blocks: block 0 (default), block 1 (loaded from the tuning area)
 * and block 2 (user / active when mode == 1, also the get/set array).
 *
 * NOT ported: system_reg_ldc_write/read themselves (writes are queued in
 * struct t32_front_write, the status read is an argument), the bodies of
 * tisp_func_ldc_dma_cfg / tisp_func_ldc_dma_stride (ops callbacks), the debug
 * print and the allocation failure path (the vendor memsets NULL there, the
 * port returns -12).  The return values of tisp_ldc_deinit / tisp_ldc_wake_up
 * are leftover registers in the vendor code and not reproduced.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_ldc_oracle).
 */
#ifndef TX_ISP_T32_LDC_H
#define TX_ISP_T32_LDC_H

#include "tx_isp_t32_tp.h"

#define T32_LDC_SLOTS		4
#define T32_LDC_CHANS		2
#define T32_LDC_PARAM_BYTES	1076U
#define T32_LDC_TAB_N		255U
#define T32_LDC_GFP		0x24000c0U

/* LDC block registers (block relative) */
#define T32_LDC_REG_ENABLE	0x04U
#define T32_LDC_REG_START	0x08U
#define T32_LDC_REG_CFG_A	0x10U	/* p16 << 16 | p12 */
#define T32_LDC_REG_CFG_B	0x14U	/* p32 << 16 | p28 */
#define T32_LDC_REG_CFG_C	0x1cU	/* p36 << 16 | p40 */
#define T32_LDC_REG_CFG_D	0x20U	/* p44 */
#define T32_LDC_REG_CFG_E	0x24U	/* 0 */
#define T32_LDC_REG_BUF0	0x30U
#define T32_LDC_REG_P20_A	0x34U
#define T32_LDC_REG_BUF0_NEXT	0x38U
#define T32_LDC_REG_P20_B	0x3cU
#define T32_LDC_REG_BUF1	0x40U
#define T32_LDC_REG_P24_A	0x44U
#define T32_LDC_REG_BUF1_NEXT	0x48U
#define T32_LDC_REG_P24_B	0x4cU
#define T32_LDC_REG_P48		0x50U
#define T32_LDC_REG_STATUS	0x68U	/* read by the irq handler */
#define T32_LDC_REG_ACK		0x6cU
#define T32_LDC_REG_IRQ_MASK	0x70U
#define T32_LDC_REG_TAB_A	0x1000U
#define T32_LDC_REG_TAB_B	0x3000U

/* tuning area offsets */
#define T32_LDC_TUN_WORDS	0xe800U
#define T32_LDC_TUN_TAB_A	0xe830U
#define T32_LDC_TUN_TAB_B	0xea30U

struct t32_ldc_param {		/* 1076 bytes */
	u16 hdr[6];
	u32 p12, p16, p20, p24, p28, p32, p36, p40, p44, p48;
	u16 tab_a[256];		/* 255 used */
	u16 tab_b[256];
};

struct t32_ldc_ctx {		/* 3240 bytes, t32_data_e3614[ch] */
	struct t32_ldc_param blk[3];
	u16 mode;		/* +3228 */
	u16 mode2;		/* +3230 */
	u8 rsvd[4];
	u16 width;		/* +3236 */
	u16 height;		/* +3238 */
};

struct t32_ldc_mem {		/* 76 bytes, t32_data_dc074[ch] */
	u8 rsvd[56];
	u32 buf[2];		/* +56, +60 */
	u32 y_bytes;		/* +64 */
	u32 c_bytes;		/* +68 */
	u32 total_bytes;	/* +72 */
};

struct t32_ldc_chan {		/* 904 bytes, t32_data_dc284[ch] */
	u8 rsvd0[40];
	u8 enable;		/* +40 */
	u8 rsvd1[859];
	u32 state;		/* +900 */
};

struct t32_ldc_cfg {		/* API struct of init / load_reg_cfg */
	u32 width, height;
	u8 rsvd[32];
	u8 enable;		/* +40 */
};

struct t32_ldc_ops {
	void *(*kmalloc)(u32 size, u32 flags);
	void (*kfree)(void *p);
	void (*dma_cfg)(u32 w0, u32 h0, u32 buf, u32 w);	/* tisp_func_ldc_dma_cfg */
	void (*dma_stride)(u32 w);				/* tisp_func_ldc_dma_stride */
	u32 (*reg_read)(u32 reg);				/* system_reg_ldc_read */
};

struct t32_ldc_dev {
	struct t32_ldc_ctx *ctx[T32_LDC_SLOTS];		/* t32_data_e3614 */
	struct t32_ldc_mem mem[T32_LDC_SLOTS];		/* t32_data_dc074 */
	struct t32_ldc_chan chan[T32_LDC_CHANS];	/* t32_data_dc284 */
	const u8 *tuning[T32_LDC_SLOTS];		/* t32_data_dd0a4 */
	u32 loaded;					/* t32_data_e3a70 */
	u32 last_status;				/* t32_data_e3a6c */
	u32 irq_count;					/* t32_data_e3610 */
	const struct t32_ldc_ops *ops;
};

static inline u32 t32_ldc_le32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* lib_tisp_ldc_get_mem_size: always fills slot 0 */
static inline u64 t32_ldc_get_mem_size(struct t32_ldc_dev *dev, u32 w, u32 h)
{
	struct t32_ldc_mem *m0 = &dev->mem[0];
	u32 px = w * h;
	u32 y = (px + 7U) >> 3 << 3;
	u32 c = ((px >> 1) + 7U) >> 3 << 3;
	u32 total = y + c;

	m0->total_bytes = total;
	m0->y_bytes = y;
	m0->c_bytes = c;
	return ((u64)c << 32) | (u32)(total << 1);
}

/* tisp_ldc_mem_cfg: the "next" addresses use the total of slot 0 */
static inline void t32_ldc_mem_cfg(struct t32_ldc_dev *dev, u32 ch,
				   struct t32_front_write *w, unsigned int *n)
{
	const struct t32_ldc_mem *m = &dev->mem[ch];
	u32 total = dev->mem[0].total_bytes;

	T32_FWR(w, *n, T32_LDC_REG_BUF0, m->buf[0]);
	T32_FWR(w, *n, T32_LDC_REG_BUF1, m->buf[1]);
	T32_FWR(w, *n, T32_LDC_REG_BUF0_NEXT, m->buf[0] + total);
	T32_FWR(w, *n, T32_LDC_REG_BUF1_NEXT, m->buf[1] + total);
}

/* tisp_ldc_reg_cfg */
static inline void t32_ldc_reg_cfg(struct t32_ldc_dev *dev, u32 ch,
				   struct t32_front_write *w, unsigned int *n)
{
	const struct t32_ldc_ctx *ctx = dev->ctx[ch];
	const struct t32_ldc_param *p;
	unsigned int i;

	p = ctx->mode == 1U ? &ctx->blk[2] : &ctx->blk[0];
	T32_FWR(w, *n, T32_LDC_REG_CFG_A, p->p16 << 16 | p->p12);
	T32_FWR(w, *n, T32_LDC_REG_CFG_B, p->p32 << 16 | p->p28);
	T32_FWR(w, *n, T32_LDC_REG_CFG_C, p->p36 << 16 | p->p40);
	T32_FWR(w, *n, T32_LDC_REG_CFG_D, p->p44);
	T32_FWR(w, *n, T32_LDC_REG_CFG_E, 0);
	T32_FWR(w, *n, T32_LDC_REG_P20_A, p->p20);
	T32_FWR(w, *n, T32_LDC_REG_P20_B, p->p20);
	T32_FWR(w, *n, T32_LDC_REG_P24_A, p->p24);
	T32_FWR(w, *n, T32_LDC_REG_P24_B, p->p24);
	T32_FWR(w, *n, T32_LDC_REG_P48, p->p48);
	for (i = 0; i < T32_LDC_TAB_N; i++) {
		T32_FWR(w, *n, T32_LDC_REG_TAB_A + 4U * i, (u32)(int)(short)p->tab_a[i]);
		T32_FWR(w, *n, T32_LDC_REG_TAB_B + 4U * i, (u32)(int)(short)p->tab_b[i]);
	}
}

/* tisp_ldc_init */
static inline int t32_ldc_init(struct t32_ldc_dev *dev, u32 ch, const struct t32_ldc_cfg *cfg)
{
	struct t32_ldc_ctx *ctx, *c0;
	u32 w0, h0;

	if (cfg->enable != 1U)
		return 0;
	ch &= 0xff;
	ctx = dev->ops->kmalloc(sizeof(*ctx), T32_LDC_GFP);
	dev->ctx[ch] = ctx;
	if (!ctx)
		return -12;
	memset(ctx, 0, sizeof(*ctx));
	ctx->width = (u16)cfg->width;
	ctx->height = (u16)cfg->height;
	/* the maximum is kept in channel 0's context */
	c0 = dev->ctx[0];
	w0 = c0->width;
	w0 = cfg->width < w0 ? w0 : cfg->width;
	c0->width = (u16)w0;
	h0 = c0->height;
	h0 = cfg->height < h0 ? h0 : cfg->height;
	c0->height = (u16)h0;
	return 0;
}

/* tisp_ldc_deinit */
static inline void t32_ldc_deinit(struct t32_ldc_dev *dev, u32 ch,
				  struct t32_front_write *w, unsigned int *n)
{
	ch &= 0xff;
	if (dev->chan[ch].enable != 1U)
		return;
	T32_FWR(w, *n, T32_LDC_REG_IRQ_MASK, 0xffffffffU);
	if (dev->ctx[ch]) {
		dev->ops->kfree(dev->ctx[ch]);
		dev->ctx[ch] = NULL;
	}
}

/* tisp_ldc_dma_cfg: width / height of context 0, buffer and width of the channel */
static inline int t32_ldc_dma_cfg(struct t32_ldc_dev *dev, u32 ch)
{
	u32 w0, h0;

	ch &= 0xff;
	if (dev->chan[ch].enable != 1U)
		return 0;
	w0 = dev->ctx[0]->width;
	h0 = dev->ctx[0]->height;
	dev->ops->dma_cfg(w0, h0, dev->mem[ch].buf[0], dev->ctx[ch]->width);
	return 0;
}

/* tisp_ldc_dma_stride */
static inline int t32_ldc_dma_stride(struct t32_ldc_dev *dev, u32 ch)
{
	ch &= 0xff;
	if (dev->chan[ch].enable != 1U)
		return 0;
	dev->ops->dma_stride(dev->ctx[ch]->width);
	return 0;
}

/* tisp_ldc_load_reg_cfg */
static inline int t32_ldc_load_reg_cfg(struct t32_ldc_dev *dev, u32 ch,
				       const struct t32_ldc_cfg *cfg,
				       struct t32_front_write *w, unsigned int *n)
{
	struct t32_ldc_ctx *ctx;
	struct t32_ldc_param *b;
	const u8 *t;

	if (cfg->enable != 1U)
		return 0;
	ch &= 0xff;
	ctx = dev->ctx[ch];
	dev->loaded = 1U;
	t = dev->tuning[ch];
	b = &ctx->blk[1];
	b->hdr[1] = (u16)t32_ldc_le32(t + 0xe800U);
	b->hdr[2] = (u16)t32_ldc_le32(t + 0xe804U);
	b->hdr[3] = (u16)t32_ldc_le32(t + 0xe828U);
	b->hdr[4] = (u16)t32_ldc_le32(t + 0xe82cU);
	b->p12 = t32_ldc_le32(t + 0xe800U);
	b->p16 = t32_ldc_le32(t + 0xe804U);
	b->p20 = t32_ldc_le32(t + 0xe808U);
	b->p24 = t32_ldc_le32(t + 0xe80cU);
	b->p28 = t32_ldc_le32(t + 0xe810U);
	b->p32 = t32_ldc_le32(t + 0xe814U);
	b->p36 = t32_ldc_le32(t + 0xe818U);
	b->p40 = t32_ldc_le32(t + 0xe81cU);
	b->p44 = t32_ldc_le32(t + 0xe820U);
	b->p48 = t32_ldc_le32(t + 0xe824U);
	memcpy(b->tab_a, t + T32_LDC_TUN_TAB_A, sizeof(b->tab_a));
	memcpy(b->tab_b, t + T32_LDC_TUN_TAB_B, sizeof(b->tab_b));
	if (ctx->mode == 1U && ctx->mode2 != 1U)
		memcpy(&ctx->blk[2], &ctx->blk[1], T32_LDC_PARAM_BYTES);
	t32_ldc_reg_cfg(dev, ch, w, n);
	t32_ldc_mem_cfg(dev, ch, w, n);
	T32_FWR(w, *n, T32_LDC_REG_START, 1);
	T32_FWR(w, *n, T32_LDC_REG_IRQ_MASK, 0);
	T32_FWR(w, *n, T32_LDC_REG_ENABLE, 1);
	return t32_ldc_dma_cfg(dev, ch);
}

/* tisp_ldc_reg_reflash: only once channel 0's state reached 2 */
static inline int t32_ldc_reg_reflash(struct t32_ldc_dev *dev, u32 ch,
				      struct t32_front_write *w, unsigned int *n)
{
	ch &= 0xff;
	if (dev->chan[ch].enable != 1U)
		return 0;
	if (dev->chan[0].state < 2U)
		return 0;
	t32_ldc_reg_cfg(dev, ch, w, n);
	t32_ldc_mem_cfg(dev, ch, w, n);
	return t32_ldc_dma_stride(dev, ch);
}

/* tisp_ldc_reg_all_reflash */
static inline int t32_ldc_reg_all_reflash(struct t32_ldc_dev *dev, u32 ch,
					  struct t32_front_write *w, unsigned int *n)
{
	ch &= 0xff;
	if (dev->chan[ch].enable != 1U)
		return 0;
	t32_ldc_reg_cfg(dev, ch, w, n);
	t32_ldc_mem_cfg(dev, ch, w, n);
	return t32_ldc_dma_cfg(dev, ch);
}

/* lib_tisp_ldc_intp_handle: reads T32_LDC_REG_STATUS through ops->reg_read */
static inline u32 t32_ldc_intp_handle(struct t32_ldc_dev *dev,
				      struct t32_front_write *w, unsigned int *n)
{
	u32 status = dev->ops->reg_read(T32_LDC_REG_STATUS);

	dev->last_status = status;
	T32_FWR(w, *n, T32_LDC_REG_ACK, status);
	dev->irq_count++;
	return status;
}

/* tisp_ldc_param_array_get: channel not masked, block 2 */
static inline int t32_ldc_param_array_get(struct t32_ldc_dev *dev, u32 ch, u8 *buf, u32 *len)
{
	if (dev->chan[ch].enable != 1U)
		return 0;
	memcpy(buf, &dev->ctx[ch]->blk[2], T32_LDC_PARAM_BYTES);
	*len = T32_LDC_PARAM_BYTES;
	return 0;
}

/* tisp_ldc_param_array_set */
static inline int t32_ldc_param_array_set(struct t32_ldc_dev *dev, u32 ch, const u8 *buf,
					  struct t32_front_write *w, unsigned int *n)
{
	if (dev->chan[ch].enable != 1U)
		return 0;
	memcpy(&dev->ctx[ch]->blk[2], buf, T32_LDC_PARAM_BYTES);
	t32_ldc_reg_cfg(dev, ch, w, n);
	return 0;
}

/* tisp_ldc_wake_up */
static inline void t32_ldc_wake_up(struct t32_ldc_dev *dev, u32 ch,
				   struct t32_front_write *w, unsigned int *n)
{
	if (dev->loaded == 1U) {
		t32_ldc_reg_cfg(dev, ch, w, n);
		t32_ldc_mem_cfg(dev, ch, w, n);
		T32_FWR(w, *n, T32_LDC_REG_START, 1);
	}
	T32_FWR(w, *n, T32_LDC_REG_IRQ_MASK, 0);
	T32_FWR(w, *n, T32_LDC_REG_ACK, dev->last_status);
}

#endif /* TX_ISP_T32_LDC_H */
