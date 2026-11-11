/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 block glue, remaining functions (one block per vendor group).
 *
 * - tisp_awb_deinit: free the per channel AWB buffers.
 * - tisp_vic_mdma_enable: VIC memory DMA size, strides, buffer addresses, enable.
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

#endif /* TX_ISP_T32_BLKREST_H */
