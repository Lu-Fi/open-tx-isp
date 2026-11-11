/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 block glue, remaining functions (one block per vendor group).
 *
 * - tisp_awb_deinit: free the per channel AWB buffers.
 * - tisp_vic_mdma_enable: VIC memory DMA size, strides, buffer addresses, enable.
 * - tisp_ae_algo_init: fill the AE algorithm init block from tuning / parameters.
 *
 * OS calls are hooks.  Pointers the vendor keeps in its tables are 32-bit
 * words (MIPS32 kernel), read and cleared in place.
 * Checked against Matt Davis' recovered C (PR #15 @ 2e2e06bc) by
 * tests/t32_oracle (one oracle per block, named at each block).
 */
#ifndef TX_ISP_T32_BLKREST_H
#define TX_ISP_T32_BLKREST_H

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
#endif
#ifndef T32_HOST_U16
#define T32_HOST_U16
typedef uint16_t u16;
#endif
#endif

static inline u8 *t32_br_ptr(u32 addr) { return (u8 *)(uintptr_t)addr; }
static inline u32 t32_br_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline void t32_br_wr32(u8 *p, u32 v) { memcpy(p, &v, 4); }

/* ---- tisp_awb_deinit (vendor 0x3b89c), oracle t32_awbdein_oracle ---- */

/* t32_data_e16b4[ch]: AWB work block (kmalloc) */
#define T32_AWBD_WK_BUF1	4	/* kmalloc */
#define T32_AWBD_WK_BUF2	8	/* kmalloc */
#define T32_AWBD_WK_BUF3	12	/* kmalloc */
#define T32_AWBD_WK_VBUF	28	/* vmalloc */
/* t32_data_e1694[ch]: AWB state (vmalloc) */
#define T32_AWBD_ST_BUF		4	/* kmalloc */

struct t32_awbd_ops {
	void (*kfree)(void *p);
	void (*vfree)(void *p);
};

struct t32_awbd {
	const struct t32_awbd_ops *ops;
	u32 *zone;	/* t32_data_e16a4[4]: zone buffer (kmalloc) */
	u32 *work;	/* t32_data_e16b4[4] */
	u32 *state;	/* t32_data_e1694[4] */
};

/* free *slot through fn when it is set, then clear it */
static inline void t32_awbd_free(u8 *slot, void (*fn)(void *p))
{
	u32 p = t32_br_rd32(slot);

	if (p) {
		fn(t32_br_ptr(p));
		t32_br_wr32(slot, 0);
	}
}

/* tisp_awb_deinit: always 0 */
static inline int t32_awb_deinit(struct t32_awbd *a, u32 ch)
{
	if (a->zone[ch]) {
		a->ops->kfree(t32_br_ptr(a->zone[ch]));
		a->zone[ch] = 0;
	}
	if (a->work[ch]) {
		u8 *w = t32_br_ptr(a->work[ch]);

		t32_awbd_free(w + T32_AWBD_WK_VBUF, a->ops->vfree);
		t32_awbd_free(w + T32_AWBD_WK_BUF1, a->ops->kfree);
		t32_awbd_free(w + T32_AWBD_WK_BUF2, a->ops->kfree);
		t32_awbd_free(w + T32_AWBD_WK_BUF3, a->ops->kfree);
		a->ops->kfree(w);
		a->work[ch] = 0;
	}
	if (a->state[ch]) {
		u8 *s = t32_br_ptr(a->state[ch]);

		t32_awbd_free(s + T32_AWBD_ST_BUF, a->ops->kfree);
		a->ops->vfree(s);
		a->state[ch] = 0;
	}
	return 0;
}

/* ---- tisp_vic_mdma_enable (vendor 0xacde0), oracle t32_vicmdma_oracle ---- */

#define T32_VMD_BASE(vi)	(((vi) + 128) << 12)	/* VIC n register page */
#define T32_VMD_CTRL		768
#define T32_VMD_SIZE		772	/* width << 16 | height */
#define T32_VMD_STRIDE0		784
#define T32_VMD_STRIDE1		788
#define T32_VMD_ADDR_Y		800	/* + 4 per buffer (vendor writes only +0) */
#define T32_VMD_ADDR_UV		816
#define T32_VMD_ADDR_RAW	832
#define T32_VMD_MAX_BUFS	4
#define T32_VMD_CTRL_EN		0x80010000u
#define T32_VMD_CTRL_MODE0	0x80010001u
#define T32_VMD_CTRL_MODE2	0x80010008u

struct t32_vmd_sensor {		/* argument a0 */
	u32 mode;		/* +0: 0, 1 (no enable write), 2, other */
	u8 rsvd[2];
	u8 vic;			/* +6 */
};

struct t32_vmd_dim {		/* t32_data_e3f00[vic]: 16 B per VIC */
	u32 width, height;
	u32 rsvd[2];
};

struct t32_vmd_fmt {		/* t32_data_e3f34 */
	u32 rsvd0;
	u8 out;			/* +4: 1 = YUV output */
	u8 rsvd1[3];
	u8 planes;		/* +8: 1 = UV plane only, 2 = Y + UV (NV12) */
};

/*
 * tisp_vic_mdma_enable: program the VIC memory DMA for n (<= 4) buffers at
 * base, each width x height x stride bytes (NV12: twice that, Y then UV).
 * Mode 1 returns 1 without the enable write; the other modes end with the
 * enable write and return its result (0).
 */
static inline u32 t32_vic_mdma_enable(const struct t32_vmd_sensor *sen, u32 stride, u32 n, u32 base,
				      const struct t32_vmd_dim *dim, const struct t32_vmd_fmt *fmt,
				      void (*reg_write)(u32 reg, u32 val))
{
	const struct t32_vmd_dim *d = &dim[sen->vic];
	u32 page = T32_VMD_BASE(sen->vic), frame = d->height * stride;
	u8 i;

	reg_write(page + T32_VMD_SIZE, d->width << 16 | d->height);
	reg_write(page + T32_VMD_STRIDE0, stride);
	reg_write(page + T32_VMD_STRIDE1, stride);
	if (n > T32_VMD_MAX_BUFS)
		n = T32_VMD_MAX_BUFS;
	for (i = 0; i < n; i++) {
		if (fmt->out != 1) {
			reg_write(page + T32_VMD_ADDR_Y, frame * i + base);
			reg_write(page + T32_VMD_ADDR_RAW, frame * i + base);
		} else if (fmt->planes == 2) {
			reg_write(page + T32_VMD_ADDR_Y, i * (frame * 2) + base);
			reg_write(page + T32_VMD_ADDR_UV, (2 * i + 1) * frame + base);
		} else if (fmt->planes == 1) {
			reg_write(page + T32_VMD_ADDR_UV, frame * i + base);
		} else {
			reg_write(page + T32_VMD_ADDR_Y, frame * i + base);
		}
	}
	switch (sen->mode) {
	case 1:
		return 1;
	case 0:
		reg_write(page + T32_VMD_CTRL, T32_VMD_CTRL_MODE0);
		break;
	case 2:
		reg_write(page + T32_VMD_CTRL, T32_VMD_CTRL_MODE2);
		break;
	default:
		reg_write(page + T32_VMD_CTRL, T32_VMD_CTRL_EN);
		break;
	}
	return 0;
}

/* ---- tisp_ae_algo_init (vendor 0x3597c), oracle t32_aealgo_oracle ---- */

#define T32_AEA_CTX_DONE	2008	/* completion in the AE context */
#define T32_AEA_TUN_A		8608	/* 4 tuning words -> info +12 */
#define T32_AEA_TUN_B		8624	/* 4 tuning words -> info +60 */
#define T32_AEA_PAR_A		448	/* 8 parameter words -> info +28 */
#define T32_AEA_PAR_B		480	/* 8 parameter words -> info +76 */
#define T32_AEA_REG_EXP		12376	/* read when tuning +8620 is 0 (shadow index 491) */
#define T32_AEA_REG_EXP_SH	491
#define T32_AEA_REG_GAIN	12808	/* read when tuning +8636 is 0 (shadow index 572) */
#define T32_AEA_REG_GAIN_SH	572
#define T32_AEA_ZONE_STRIDE	1052	/* t32_data_d0950 per channel */
#define T32_AEA_ZONE_WORD	1044

struct t32_aea_info {		/* argument a2, the algorithm's init block */
	u32 rsvd0[2];
	u32 zero;		/* +8 */
	u32 tun_a[4];		/* +12: tuning +8608 .. +8620 */
	u32 par_a[8];		/* +28: parameters +448 .. +476 */
	u32 tun_b[4];		/* +60: tuning +8624 .. +8636 */
	u32 par_b[8];		/* +76: parameters +480 .. +508 */
	u32 rsvd1;		/* +108 */
	u32 one[3];		/* +112 */
	u8 w0, w1;		/* +124, +125: 15 */
	u8 undef[6];		/* +126: vendor stack left-over, 0 here */
	u8 zone[4];		/* +132: t32_data_d0950[ch] +1044 */
};

struct t32_aea_ops {
	void (*init_completion)(void *c);
	u32 (*reg_read)(u32 ch, u32 reg);
};

struct t32_aea {
	const struct t32_aea_ops *ops;
	u32 *enable;		/* t32_data_e15b8[4] */
	const u32 *ctx;		/* t32_data_e15a8[4]: AE context, +0 parameter block, +4 tuning block */
	const u8 *shadow;	/* t32_data_d03a0 */
	const u8 *zone;		/* t32_data_d0950 */
};

/* tisp_ae_algo_init: the vendor's result is a left-over register (none here) */
static inline void t32_ae_algo_init(struct t32_aea *a, u32 ch, u32 enable, struct t32_aea_info *info)
{
	u8 *ctx = t32_br_ptr(a->ctx[ch]);
	const u8 *par, *tun;
	u32 i;

	a->enable[ch] = enable;
	if (enable != 1)
		return;
	a->ops->init_completion(ctx + T32_AEA_CTX_DONE);
	par = t32_br_ptr(t32_br_rd32(ctx));
	tun = t32_br_ptr(t32_br_rd32(ctx + 4));
	info->zero = 0;
	for (i = 0; i < 4; i++) {
		info->tun_a[i] = t32_br_rd32(tun + T32_AEA_TUN_A + 4 * i);
		info->tun_b[i] = t32_br_rd32(tun + T32_AEA_TUN_B + 4 * i);
	}
	for (i = 0; i < 8; i++) {
		info->par_a[i] = t32_br_rd32(par + T32_AEA_PAR_A + 4 * i);
		info->par_b[i] = t32_br_rd32(par + T32_AEA_PAR_B + 4 * i);
	}
	if (!info->tun_a[3])
		info->tun_a[3] = a->ops->reg_read(ch, *a->shadow ? T32_AEA_REG_EXP_SH : T32_AEA_REG_EXP);
	if (!info->tun_b[3])
		info->tun_b[3] = a->ops->reg_read(ch, *a->shadow ? T32_AEA_REG_GAIN_SH : T32_AEA_REG_GAIN);
	info->one[0] = info->one[1] = info->one[2] = 1;
	info->w0 = info->w1 = 15;
	memset(info->undef, 0, sizeof(info->undef));
	memcpy(info->zone, a->zone + T32_AEA_ZONE_STRIDE * ch + T32_AEA_ZONE_WORD, 4);
}

#endif /* TX_ISP_T32_BLKREST_H */
