/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LDC attribute API of the vendor tisp core: tisp_online_ldc_get_attr,
 * tisp_online_ldc_set_attr and tisp_offline_ldc_set_attr (the offline LDC
 * register program from a user parameter block).
 *
 * The online context is struct t32_ldc_ctx (tx_isp_t32_ldc.h): set_attr
 * copies the user block into blk[0] and, when both mode words are 1, blk[0]
 * into blk[2]; get_attr returns the two mode words and &blk[2].
 * User copies, the error print and the LDC register writes go through
 * struct t32_ldcattr_ops.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK):
 * online set_attr clears both mode words before it checks anything, so a
 * rejected or disabled call leaves the context with mode 0.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_ldcattr_oracle).
 */
#ifndef TX_ISP_T32_LDCATTR_H
#define TX_ISP_T32_LDCATTR_H

#include "tx_isp_t32_ldc.h"

#define T32_LDC_OFFLINE_BYTES		1096
#define T32_LDC_COPY_FAIL_FMT		"[ %s:%d ] copy from user error\n"
#define T32_EFAULT			14

struct t32_ldcattr_ops {
	unsigned long (*copy_from_user)(void *to, const void *from, unsigned long n);
	int (*isp_printf)(int level, const char *fmt, ...);
	void (*ldc_write)(u32 reg, u32 val);	/* system_reg_ldc_write */
};

/* tisp_online_ldc_get_attr: out { u16 mode, u16 mode2, param pointer } */
struct t32_ldc_online_attr {
	u16 mode, mode2;
	u32 param;		/* 32-bit pointer (user: the parameter block) */
};

static inline int t32_online_ldc_get_attr(struct t32_ldc_ctx *ctx, struct t32_ldc_online_attr *out)
{
	out->mode = ctx->mode;
	out->mode2 = ctx->mode2;
	out->param = (u32)(uintptr_t)&ctx->blk[2];
	return 0;
}

/* the set_attr API struct: the user pointer sits at +8 (get_attr returns it at +4) */
struct t32_ldc_online_set {
	u16 mode, mode2;
	u32 rsvd;
	u32 param;		/* 32-bit user pointer of the 1076-byte block */
};

/* tisp_online_ldc_set_attr; enable = the channel's LDC enable byte (dc284[ch] + 40) */
static inline int t32_online_ldc_set_attr(const struct t32_ldcattr_ops *k, struct t32_ldc_ctx *ctx, u8 enable,
					  const struct t32_ldc_online_set *in)
{
	ctx->mode = 0;
	ctx->mode2 = 0;
	if (enable != 1)
		return 0;
	if (k->copy_from_user(&ctx->blk[0], (const void *)(uintptr_t)in->param, T32_LDC_PARAM_BYTES)) {
		k->isp_printf(2, T32_LDC_COPY_FAIL_FMT, "tisp_online_ldc_set_attr", 346);
		return -T32_EFAULT;
	}
	if (in->mode != enable)
		return 0;
	ctx->mode = in->mode;
	if (in->mode2 != in->mode)
		return 0;
	ctx->mode2 = in->mode2;
	memcpy(&ctx->blk[2], &ctx->blk[0], T32_LDC_PARAM_BYTES);
	return 0;
}

/*
 * tisp_offline_ldc_set_attr.  in: the API struct (+4 u16, +6 u16, +8..+20 four
 * words, +24 user pointer of the 1096-byte parameter block).  blk: the
 * vendor's offline state (20 header bytes, then the copied block).  Block
 * offsets: +12..+48 words, +52 / +564 two tables of 255 s16.
 */
#define T32_LDC_OFF_HDR		20
static inline u32 t32_ldc_off_w(const u8 *blk, u32 off)
{
	return *(const u32 *)(blk + T32_LDC_OFF_HDR + off);
}

static inline int t32_offline_ldc_set_attr(const struct t32_ldcattr_ops *k, u8 *blk, const u8 *in)
{
	const u8 *p = blk + T32_LDC_OFF_HDR;
	u32 i;

	if (k->copy_from_user(blk + T32_LDC_OFF_HDR, (const void *)(uintptr_t)*(const u32 *)(in + 24),
			      T32_LDC_OFFLINE_BYTES)) {
		k->isp_printf(2, T32_LDC_COPY_FAIL_FMT, "tisp_offline_ldc_set_attr", 410);
		return -T32_EFAULT;
	}
	*(u16 *)(blk + 0) = *(const u16 *)(in + 4);
	*(u16 *)(blk + 2) = *(const u16 *)(in + 6);
	memcpy(blk + 4, in + 8, 16);
	k->ldc_write(16, t32_ldc_off_w(blk, 16) << 16 | t32_ldc_off_w(blk, 12));
	k->ldc_write(20, t32_ldc_off_w(blk, 32) << 16 | t32_ldc_off_w(blk, 28));
	k->ldc_write(28, t32_ldc_off_w(blk, 36) << 16 | t32_ldc_off_w(blk, 40));
	k->ldc_write(32, t32_ldc_off_w(blk, 44));
	k->ldc_write(36, 0);
	k->ldc_write(52, t32_ldc_off_w(blk, 20));
	k->ldc_write(60, t32_ldc_off_w(blk, 20));
	k->ldc_write(68, t32_ldc_off_w(blk, 24));
	k->ldc_write(76, t32_ldc_off_w(blk, 24));
	k->ldc_write(80, t32_ldc_off_w(blk, 48));
	for (i = 0; i < 255; i++) {
		k->ldc_write(0x1000 + 4 * i, (u32)(int)*(const s16 *)(p + 52 + 2 * i));
		k->ldc_write(0x3000 + 4 * i, (u32)(int)*(const s16 *)(p + 564 + 2 * i));
	}
	k->ldc_write(48, *(const u32 *)(blk + 4));
	k->ldc_write(64, *(const u32 *)(blk + 8));
	k->ldc_write(56, *(const u32 *)(blk + 12));
	k->ldc_write(72, *(const u32 *)(blk + 16));
	k->ldc_write(8, 0);
	k->ldc_write(112, 0);
	k->ldc_write(4, 1);
	return 0;
}

#endif /* TX_ISP_T32_LDCATTR_H */
