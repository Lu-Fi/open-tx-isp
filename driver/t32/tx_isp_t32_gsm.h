/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 gamma statistics module (GSM, registers 0x19040.. and RAM window
 * 0x321a0..): a histogram unit that is read back into two 256 word RAMs and
 * reduced to five zone weights.
 *
 * Vendor functions ported (Matt Davis' recovered C, PR #15):
 *   tisp_gsm_irq_frmae_done, tisp_gsm_process, tisp_gsm_read_ram,
 *   tisp_gsm_interrupt_hist, tisp_gsm_init, tisp_gsm_api_set,
 *   tisp_gsm_api_get, tisp_gsm_wake_up.
 *
 * Registers: 0x19040 ack, 0x19044 status (1 = histogram ready), 0x1904c
 * configuration (u16 t32_data_d7e28), 0x321a0 / 0x321b0 RAM control,
 * 0x321a8 / 0x321b8 RAM read ports (22 bit).
 * The u16 configuration value is filled outside these functions (tuning
 * data); its IQ section is not visible in the recovered GSM code.
 *
 * Not ported: spin locks, irq registration (system_irq_func_set 7 ->
 * interrupt_hist, 63 -> irq_frame_done), tisp_event_set_cb(ch, 16, process),
 * isp_printf diagnostics (checksum mismatch, bad status), and the
 * t32_data_d03a0 != 0 indirect register mode (direct writes only).
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_gsm_oracle).
 */
#ifndef TX_ISP_T32_GSM_H
#define TX_ISP_T32_GSM_H

#include "tx_isp_t32_tp.h"

#define T32_GSM_REG_ACK		0x19040
#define T32_GSM_REG_STATUS	0x19044
#define T32_GSM_REG_CFG		0x1904c
#define T32_GSM_RAM_CTL0	0x321a0
#define T32_GSM_RAM_RD0		0x321a8
#define T32_GSM_RAM_CTL1	0x321b0
#define T32_GSM_RAM_RD1		0x321b8
#define T32_GSM_RAM_ARM		0x201U
#define T32_GSM_RAM_DONE	0xff0202U
#define T32_GSM_RAM_MASK	0x3fffffU
#define T32_GSM_WORDS		256U
#define T32_GSM_EV_HIST		16U
#define T32_GSM_IRQ_HIST	7
#define T32_GSM_IRQ_FRAME	63
#define T32_GSM_TOTAL		0xffffU
#define T32_GSM_SCALE_ONE	0x80000000U

/* t32_data_d6e0c, 1052 bytes, returned by tisp_gsm_api_get */
struct t32_gsm_api {
	u32 table[256];		/* weights A+B per bin */
	u32 zone[5];		/* zone weights, sum 0xffff */
	u8 bound[4];		/* zone boundaries (api_set writes these) */
	u32 rsvd;
};

/* vendor data t32_data_d6e00 .. d7e28 */
struct t32_gsm_ctx {
	u32 scale;		/* d6e00: 0x80000000 / pixels */
	u32 scale_hi;		/* d6e04 */
	u32 pixels;		/* d6e08 */
	struct t32_gsm_api api;	/* d6e0c */
	u32 a[256];		/* d7228 copy of ram_a */
	u32 b[256];		/* d7628 copy of ram_b */
	u32 c[256];		/* d7a28 a + b */
	u16 cfg;		/* d7e28 value for 0x1904c */
};

/* vendor data t32_data_e2900 / e2910 / e2d10 */
struct t32_gsm_ram {
	u32 irq[4];		/* frame done counter per channel */
	u32 ram_b[256];		/* read port 0x321b8 */
	u32 ram_a[256];		/* read port 0x321a8 */
};

struct t32_gsm_event {
	u32 rsvd0;
	u32 rsvd1;
	u32 type;
};

/* tisp_gsm_irq_frmae_done */
static inline u32 t32_gsm_irq_frame_done(struct t32_gsm_ram *ram, u8 ch)
{
	ram->irq[ch]++;
	return 1;
}

/* tisp_gsm_read_ram */
static inline void t32_gsm_read_ram(struct t32_gsm_ram *ram, u32 (*rd)(u32 reg),
				    struct t32_front_write *w, unsigned int *n)
{
	u32 i;

	T32_FWR(w, *n, T32_GSM_RAM_CTL0, T32_GSM_RAM_ARM);
	T32_FWR(w, *n, T32_GSM_RAM_CTL1, T32_GSM_RAM_ARM);
	for (i = 0; i < T32_GSM_WORDS; i++) {
		ram->ram_a[i] = rd(T32_GSM_RAM_RD0) & T32_GSM_RAM_MASK;
		ram->ram_b[i] = rd(T32_GSM_RAM_RD1) & T32_GSM_RAM_MASK;
	}
	T32_FWR(w, *n, T32_GSM_RAM_CTL0, T32_GSM_RAM_DONE);
	T32_FWR(w, *n, T32_GSM_RAM_CTL1, T32_GSM_RAM_DONE);
}

/* weights of one zone: c[i] = a[i] + b[i] for i in [from, to) */
static inline u32 t32_gsm_seg(struct t32_gsm_ctx *ctx, u32 from, u32 to)
{
	u32 i, sum = 0;

	for (i = from; i < to; i++) {
		ctx->c[i] = ctx->a[i] + ctx->b[i];
		sum += ctx->c[i];
	}
	return sum;
}

/* 17.15 fixed point scale of a zone sum (64 bit product, high word + scale_hi * x) */
static inline u32 t32_gsm_norm(const struct t32_gsm_ctx *ctx, u32 x)
{
	u64 p = (u64)x * ctx->scale;
	u32 lo = (u32)p;
	u32 hi = (u32)(p >> 32) + ctx->scale_hi * x;

	return (lo >> 15) | (hi << 17);
}

/* tisp_gsm_process */
static inline int t32_gsm_process(struct t32_gsm_ctx *ctx, const struct t32_gsm_ram *ram)
{
	u32 b0 = ctx->api.bound[0], b1 = ctx->api.bound[1];
	u32 b2 = ctx->api.bound[2], b3 = ctx->api.bound[3];
	u32 z1, z2, z3, z4, n1, n2, n3, n4;

	memcpy(ctx->a, ram->ram_a, sizeof(ctx->a));
	memcpy(ctx->b, ram->ram_b, sizeof(ctx->b));
	memset(ctx->c, 0, sizeof(ctx->c));
	t32_gsm_seg(ctx, 0, b0);
	z1 = t32_gsm_seg(ctx, b0, b1);
	z2 = t32_gsm_seg(ctx, b1, b2);
	z3 = t32_gsm_seg(ctx, b2, b3);
	z4 = t32_gsm_seg(ctx, b3, T32_GSM_WORDS);
	n1 = t32_gsm_norm(ctx, z1);
	n2 = t32_gsm_norm(ctx, z2);
	n3 = t32_gsm_norm(ctx, z3);
	n4 = t32_gsm_norm(ctx, z4);
	memcpy(ctx->api.table, ctx->c, sizeof(ctx->c));
	ctx->api.zone[0] = T32_GSM_TOTAL - n1 - n2 - n3 - n4;
	ctx->api.zone[1] = n1;
	ctx->api.zone[2] = n2;
	ctx->api.zone[3] = n3;
	ctx->api.zone[4] = n4;
	return 0;
}

/* tisp_gsm_interrupt_hist */
static inline u32 t32_gsm_interrupt_hist(struct t32_gsm_ram *ram, u8 ch, u32 (*rd)(u32 reg),
					 void (*push)(u8 ch, const struct t32_gsm_event *ev),
					 struct t32_front_write *w, unsigned int *n)
{
	struct t32_gsm_event ev = { 0, 0, T32_GSM_EV_HIST };

	if (rd(T32_GSM_REG_STATUS) == 1) {
		t32_gsm_read_ram(ram, rd, w, n);
		push(ch, &ev);
	}
	T32_FWR(w, *n, T32_GSM_REG_ACK, 1);
	return 1;
}

/* tisp_gsm_init (width, height come from the first two words of its argument) */
static inline int t32_gsm_init(struct t32_gsm_ctx *ctx, u32 width, u32 height,
			       struct t32_front_write *w, unsigned int *n)
{
	ctx->scale_hi = 0;
	ctx->pixels = width * height;
	ctx->scale = ctx->pixels ? T32_GSM_SCALE_ONE / ctx->pixels : ~0U;
	T32_FWR(w, *n, T32_GSM_REG_CFG, ctx->cfg);
	return 0;
}

/* tisp_gsm_api_set: only the four zone boundaries are taken */
static inline int t32_gsm_api_set(struct t32_gsm_ctx *ctx, const struct t32_gsm_api *p)
{
	memcpy(ctx->api.bound, p->bound, sizeof(ctx->api.bound));
	return 0;
}

/* tisp_gsm_api_get */
static inline int t32_gsm_api_get(const struct t32_gsm_ctx *ctx, struct t32_gsm_api *out)
{
	memcpy(out, &ctx->api, sizeof(ctx->api));
	return 0;
}

/* tisp_gsm_wake_up */
static inline void t32_gsm_wake_up(const struct t32_gsm_ctx *ctx,
				   struct t32_front_write *w, unsigned int *n)
{
	T32_FWR(w, *n, T32_GSM_REG_CFG, ctx->cfg);
}

#endif /* TX_ISP_T32_GSM_H */
