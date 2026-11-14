/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 normal AE interrupt / calc / init glue (vendor tisp_normal_ae_interrupt_hist,
 * tisp_normal_ae_interrupt_static, tisp_normal_ae_calc_process, tisp_normal_ae_init).
 * Callees (hardware, events, the AE algorithm, memcpy ...) are reached through
 * struct t33_aeint_ops so the same code serves the driver and the binary oracle.
 * The debug printk of calc_process is not ported (only the calls that feed it).
 */
#ifndef TX_ISP_T33_AEINT_H
#define TX_ISP_T33_AEINT_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_aealg.h"

#define T33_PTR(p)	((u32)(unsigned long)(p))

struct t33_aeint_ops {
	int (*long_ev_alloc)(u32 ch);
	u32 (*reg_read)(u32 reg);
	int (*event_push)(u32 ch, void *ev);
	int (*par_update_trig)(u32 ch, u32 par);
	int (*algorithm)(u32 ch);
	int (*long_process)(u32 ch);
	void (*complete)(void *c);
	u32 (*mult2_32)(u32 sh, u32 a, u32 b);
	u32 (*mult3_32)(u32 sh, u32 a, u32 b, u32 c);
	u32 (*log2fx)(u32 v, u32 in, u32 out);
	int (*get_luma)(u32 ch, u8 *luma);
	int (*get_attr)(u32 ch, void *attr);
	int (*flicker_init)(u32 ch);
	int (*init_par_calc)(u32 ch);
	int (*set_start_param)(u32 ch, u32 par);
	void *(*copy)(void *d, const void *s, size_t n);
	void *(*fill)(void *d, int c, size_t n);
	void (*spin_init)(void *lock);
};

static inline u8 *t33_p(u32 a)
{
	return (u8 *)(unsigned long)a;
}

/* tisp_normal_ae_interrupt_hist */
static inline int t33_ae_int_hist(u32 ch, u8 *cache, u32 *stat_num, const struct t33_aeint_ops *o)
{
	u32 ev[3];

	if (t33_le32(cache + 1732) == 1)
		o->long_ev_alloc(ch & 0xff);
	stat_num[2 * ch + 1] = t33_ae_hbuf_idx(o->reg_read(T33_AE_IDX_REG));
	ev[0] = 0;
	ev[1] = 0;
	ev[2] = 1;
	o->event_push(ch, ev);
	return 0;
}

/* tisp_normal_ae_interrupt_static */
static inline int t33_ae_int_static(u32 ch, u32 *stat_num, const struct t33_aeint_ops *o)
{
	stat_num[2 * ch] = t33_ae_zbuf_idx(o->reg_read(T33_AE_IDX_REG));
	return 1;
}

/* tisp_normal_ae_calc_process; R: ae_cache[ch], par: the parameter pointer at ae_contexts[ch] + 19952 */
static inline int t33_ae_calc_process(u32 ch, u8 *R, u32 custom_en, u32 par, u32 *frame_cnt, u32 zeratul,
				      u32 per_frame, u32 dbg_frames, u8 *comp, const struct t33_aeint_ops *o)
{
	u8 luma = 0;
	u32 attr[20];
	u32 ev, cnt;
	u8 pend = R[1285];

	R[1706] = 0;
	if (pend)
		R[1286] = 1;
	if (!custom_en && pend)
		o->par_update_trig(ch, par);
	o->algorithm(ch & 0xff);
	if (R[1286] == 1) {
		R[1285] = 0;
		R[1286] = 0;
	}
	o->long_process(ch);
	if (custom_en == 1)
		o->complete(comp);
	if (!zeratul)
		return 0;
	cnt = *frame_cnt + 1;
	*frame_cnt = cnt;
	if (!per_frame)
		return 0;
	if ((int)dbg_frames < (int)cnt)
		return 0;
	ev = o->mult2_32(10, t32_ld32(R, 580), t32_ld32(R, 588));
	o->mult3_32(10, t32_ld32(R, 576), t32_ld32(R, 580), t32_ld32(R, 588));
	o->log2fx(ev << 6, 16, 16);
	o->get_luma(ch, &luma);
	o->get_attr(ch, attr);
	return 0;
}

/* the module slots of one channel that tisp_normal_ae_init fills */
struct t33_aeinit_io {
	u32 *fliker_info, *fliker_para, *stat_buff, *hist_buff, *info, *cache, *ctx;
	u8 *init_flag;
	const u8 *cache_default;	/* ae_cache_default, 1744 bytes */
};

/* tisp_normal_ae_init; p: the AE parameter block, out_a / out_b: the callers' outputs (a2 / a3) */
static inline int t33_normal_ae_init(u32 ch, u8 *p, u8 *out_a, u8 *out_b, const struct t33_aeinit_io *io,
				     const struct t33_aeint_ops *o)
{
	u8 *info = p + 8, *cfg, *tun;
	u32 c = ch & 0xff, zx, zy, v, k;
	u32 lock[2];

	*io->fliker_info = T33_PTR(p + 8160);
	*io->fliker_para = T33_PTR(p + 19928);
	*io->stat_buff = t33_le32(p + 19960);
	*io->hist_buff = t33_le32(p + 19964);
	*io->info = T33_PTR(info);
	*io->cache = T33_PTR(info);
	o->flicker_init(c);
	o->copy(info, io->cache_default, 1744);
	o->fill(info + 1752, 0, 6400);
	*io->init_flag = 1;
	cfg = t33_p(t33_le32(p + 19956));
	t32_wr16(info, 2828, t32_st16(cfg, 12));
	t32_wr16(info, 2830, t32_st16(cfg, 14));
	t32_wr16(info, 2832, t32_ld32(cfg, 8));
	o->par_update_trig(c, t33_le32(p + 19952));
	o->init_par_calc(c);
	o->copy(info + 1544, t33_p(t33_le32(info + 1744)) + 1496, 120);
	o->copy(info + 1664, t33_p(t33_le32(info + 1744)) + 2870, 30);
	o->set_start_param(c, t33_le32(p + 19956));
	tun = t33_p(t33_le32(info + 1744));
	zx = (u32)((int)(t32_st16(info, 2828) - t32_st16(tun, 2772) + 1) / 30);
	zy = (u32)((int)(t32_st16(info, 2830) - t32_st16(tun, 2768) + 1) / 30);
	for (k = 0; k < 15; k++) {
		t32_wr16(info, 7100 + 2 * k, zx);
		t32_wr16(info, 7130 + 2 * k, zy);
	}
	v = t32_st16(tun, 2778);
	if (v < 255)
		v = 255;
	out_b[5] = (u8)t32_st16(tun, 2776);
	out_b[14] = (u8)((v << 1) / 3);
	out_b[4] = 1;
	out_b[10] = 1;
	t32_wr16(out_b, 2, t32_st16(tun, 2768));
	out_b[6] = (u8)t32_st16(tun, 2770);
	t32_wr16(out_b, 0, t32_st16(tun, 2772));
	out_b[7] = (u8)zy;
	out_b[15] = (u8)t32_st16(tun, 2774);
	out_b[12] = (u8)zx;
	t32_wr32(out_a, 36, t32_ld32(info, 288));
	o->spin_init(&lock[0]);
	o->spin_init(&lock[1]);
	*io->ctx = T33_PTR(p);
	return 0;
}

#endif /* TX_ISP_T33_AEINT_H */
