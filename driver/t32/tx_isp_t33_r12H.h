/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ISP core interrupt top half / dispatch and VIC interrupt handler of the ISP library
 * (vendor lib_tisp_intp_handler, lib_tisp_intp_process, lib_tisp_vic_intp_handle, lib_tisp_vic_deinit).
 * Register access and OS calls are hooks (struct t33_irq_ops); the vendor statics are passed by pointer.
 */
#ifndef TX_ISP_T33_R12H_H
#define TX_ISP_T33_R12H_H

#include "tx_isp_t33_blk.h"

struct t33_irq_ops {
	u32 (*reg_read)(u32 reg);		/* system_reg_read */
	void (*reg_write)(u32 reg, u32 val);	/* system_reg_write */
	void (*tmo_overflow)(void);		/* tisp_tmo_s0s1_overflow_process */
	void (*complete)(void *c);		/* private_complete */
	void (*vfree)(void *p);			/* private_vfree */
};

/* lib_tisp_intp_process: handlers of the set bits of st[0] (bits 0..31), st[3] (32..63), st[4] (64..95) */
static inline u32 t33_lib_intp_process(const u32 *st, u32 (*const *handler)(u32), u32 *cnt, u32 ch)
{
	unsigned int i;
	u32 word, r, ret = 1;

	ch &= 0xff;
	for (i = 0; i < 96; i++) {
		word = i < 32 ? st[0] : i < 64 ? st[3] : st[4];
		if (!(word & (1U << (i & 31))) || !handler[i])
			continue;
		if (i < 32)
			cnt[i]++;
		r = handler[i](ch);
		if (r != 1)
			ret = r;
	}
	return ret;
}

struct t33_intp_glob {
	u32 *info;		/* stIspIntpInfo, 7 words */
	u32 *err_num;		/* g_ispErrorNum */
	u32 *err_value;		/* g_ispErrorvalue */
	u32 *err_bits;		/* g_ispError[32] */
	u32 *rcfg0;		/* g_intpRcfg0[32] */
	u32 *rcfg1;		/* g_intpRcfg1[32] */
	u32 *back0;		/* g_intpBack0[32] */
	u32 *comm1;		/* g_intpComm1[32] */
	u32 *frame_num;		/* g_frameNum */
	u32 *dma;		/* g_dmaInfo[2] */
};

static inline void t33_intp_bits_add(u32 *cnt, u32 bits)
{
	unsigned int i;

	for (i = 0; i < 32; i++)
		cnt[i] += (bits >> i) & 1;
}

static inline u32 t33_intp_ack(const struct t33_irq_ops *ops, u32 stat, u32 ack)
{
	u32 v = ops->reg_read(stat);

	ops->reg_write(ack, v);
	return v;
}

/* lib_tisp_intp_handler; init_mode = stIspInitPar word 904 */
static inline u32 *t33_lib_intp_handler(const struct t33_irq_ops *ops, const struct t33_intp_glob *g,
					u32 init_mode, u32 *out)
{
	unsigned int i;
	u32 v;

	for (i = 0; i < 7; i++)
		g->info[i] = 0;
	g->info[0] = t33_intp_ack(ops, 0x30028, 0x30024);
	g->info[1] = t33_intp_ack(ops, 0x30038, 0x30034);
	g->info[3] = t33_intp_ack(ops, 0x300a8, 0x300a4);
	g->info[4] = t33_intp_ack(ops, 0x300b8, 0x300b4);
	g->info[5] = t33_intp_ack(ops, 0x300c8, 0x300c4);
	g->info[6] = t33_intp_ack(ops, 0x300d8, 0x300d4);
	v = ops->reg_read(0x30068);
	g->info[2] = v;
	if (v) {
		ops->reg_write(0x30064, v);
		(*g->err_num)++;
		*g->err_value = g->info[2];
		if (init_mode < 3)
			ops->tmo_overflow();
		t33_intp_bits_add(g->err_bits, g->info[2]);
	}
	t33_intp_bits_add(g->rcfg0, g->info[5]);
	t33_intp_bits_add(g->rcfg1, g->info[6]);
	t33_intp_bits_add(g->back0, g->info[3]);
	t33_intp_bits_add(g->comm1, g->info[1]);
	if (g->info[0] & 1)
		(*g->frame_num)++;
	if (g->info[4] & 0x10)
		g->dma[0]++;
	if (g->info[4] & 0x20)
		g->dma[1]++;
	for (i = 0; i < 7; i++)
		out[i] = g->info[i];
	return out;
}

struct t33_vic_intp_res {
	u8 frame[4];
	u32 status[2];
	u32 dma_status[2];
};

struct t33_vic_intp_glob {
	u32 **cfg;		/* stVicCacheAttr[2]: counter blocks */
	u32 *err_cnt;		/* vic_error_count[2] */
	u8 *raw_num;		/* saveRawNum */
	u8 *raw_idx;		/* saveRawIndex */
	u32 raw_size;		/* saveRawImageSize */
	u32 raw_paddr;		/* saveRawPaddr */
	void *compl_;		/* snapCompl */
	u32 save_flag;		/* byte 4 of pSaveInfo */
};

/* lib_tisp_vic_intp_handle */
static inline struct t33_vic_intp_res *t33_lib_vic_intp_handle(const struct t33_irq_ops *ops,
							       const struct t33_vic_intp_glob *g,
							       struct t33_vic_intp_res *out)
{
	struct t33_vic_intp_res res;
	u32 vic, base, st, dma, r, b, idx;
	u32 *cnt;
	u8 n;

	for (b = 0; b < 4; b++)	/* vendor: stack contents unless status bit 4 sets the byte; caller supplies them */
		res.frame[b] = out->frame[b];
	for (vic = 0; vic < 2; vic++) {
		base = 0x80000U + (vic << 12);
		st = ops->reg_read(base + 0x1e0);
		res.status[vic] = st;
		dma = ops->reg_read(base + 0x1e4);
		res.dma_status[vic] = dma;
		ops->reg_write(base + 0x1f0, st);
		ops->reg_write(base + 0x1f4, dma);
		cnt = g->cfg[vic];
		if (st & 0x1ff) {
			for (b = 0; b < 6; b++)
				if (st & (1U << b))
					cnt[2 + b]++;
			if (st & 0x10) {
				r = ops->reg_read(base + 0x20c);
				res.frame[vic] = (u8)(2 * vic + ((r & 7) != 0));
			}
		}
		if (st & ~0x1ffU) {
			for (b = 0; b < 15; b++)
				if (st & (1U << (9 + b)))
					cnt[14 + b]++;
			if (st & 0x0ffffe00)
				g->err_cnt[vic]++;
		}
		for (b = 0; b < 4; b++)
			if (dma & (1U << b))
				cnt[10 + b]++;
		if (dma & 1) {
			n = (u8)(*g->raw_num - 1);
			*g->raw_num = n;
			idx = (u8)(*g->raw_idx + 1);
			*g->raw_idx = (u8)idx;
			if (n == 0) {
				*g->raw_idx = 0;
				ops->complete(g->compl_);
			} else {
				ops->reg_write(base + 0x320, g->raw_paddr + idx * g->raw_size);
			}
		}
		if (dma & 2) {
			if (g->save_flag == 1)
				ops->complete(g->compl_);
		}
	}
	*out = res;
	return out;
}

/* lib_tisp_vic_deinit: free the two cached configs (stVicCacheAttr[0..1]) */
static inline void t33_lib_vic_deinit(const struct t33_irq_ops *ops, void **attr)
{
	unsigned int k;

	for (k = 0; k < 2; k++) {
		if (attr[k]) {
			ops->vfree(attr[k]);
			attr[k] = (void *)0;
		}
	}
}

#endif /* TX_ISP_T33_R12H_H */
