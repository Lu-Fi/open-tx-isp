/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC raw capture of the ISP library: raw parameter set-up and the raw snapshot to a file.
 *
 *   t32_lib_vic_set_par   lib_tisp_vic_set_par 0xae338: VIC parameter word 0 = fmt, bytes 5 / 6 / 7 / 8 = raw
 *                         mode, VIC (idx & 1), b7, b8.  Line stride (e3f28) from the VIC input width w:
 *                         fmt 0: w, fmt 2: 3w / 2, other: 2w, each rounded up to 8; fmt 1 only logs (stride kept).
 *                         Frame size (e3f44) = input height * stride.  Buffer size = frame size, doubled when
 *                         the DMA flag (byte 4) is 1 and b8 is 2, times b7 when the DMA flag is not 1.
 *                         Raw mode 0: kmalloc + clear (DMA address = virtual + 0x80000000); 1: isp_malloc_buffer;
 *                         2: the MDNS buffer (refused when smaller than the buffer size), byte 15 of channel 0's
 *                         tuning block = 1, tisp_top_reg_cfg(0), sleep one frame (1000 * fps_num / fps_den + 1 ms);
 *                         3: DMA address = the caller's; other: error.  Errors return 0x80032003.
 *   t32_lib_vic_save_raw  lib_tisp_vic_save_raw 0xae858: set_par(vic, mode, fmt 3, b7, b8, 0), raw DMA start,
 *                         then lib_tisp_save_file(raw buffer, frame size, name): "/tmp/snap0.raw" (DMA flag not 1),
 *                         "/tmp/snap1_short.raw" (b8 1), "/tmp/snap0_long.raw" (b8 other); b8 2 saves the same
 *                         buffer twice, as long then short (vendor).  Returns the set_par error or the last
 *                         save_file result.
 *
 * Vendor behaviour kept: fps denominator 0 divides by zero (MIPS result ~0, so the sleep is 0 ms); the stride,
 * frame and buffer sizes wrap at 32 bits; mode 0 stores the kmalloc result before the NULL check.  The printk
 * traces are a hook with the vendor source line and the traced values.  OS calls are hooks.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libvicp_oracle).
 */
#ifndef TX_ISP_T32_LIBVICP_H
#define TX_ISP_T32_LIBVICP_H

#include "tx_isp_t32_libvicg.h"
#ifndef __KERNEL__
#include <stdio.h>
#endif

#define T32_VIC_PAR_FMTW	0U	/* u32: raw format */
#define T32_VIC_PAR_FRAME	16U	/* u32 (e3f44): frame size */
#define T32_VIC_ERR		0x80032003U
#define T32_VIC_KMALLOC_FLAGS	0x24002c0U
#define T32_VIC_KSEG0		0x80000000U
#define T32_VIC_NAME_LEN	128U

struct t32_vicp_hooks {
	struct t32_vic_hooks vic;
	void (*trace)(void *ctx, u32 line, u32 a, u32 b, u32 c);	/* printk (source line, values) */
	void *(*kmalloc)(void *ctx, u32 size, u32 flags);		/* private_kmalloc */
	u32 (*malloc_buffer)(void *ctx, u32 size);			/* isp_malloc_buffer */
	void (*msleep)(void *ctx, u32 ms);				/* private_msleep */
	void *(*to_virt)(void *ctx, u32 dma);				/* vendor: dma + 0x80000000 */
	u32 (*to_dma)(void *ctx, void *p);				/* vendor: virtual + 0x80000000 */
	u32 (*save_file)(void *ctx, void *buf, u32 size, const char *name);	/* lib_tisp_save_file */
};

/* inputs the vendor reads from other blocks */
struct t32_vicp_env {
	u8 *tuning0;		/* t32_data_e1484[0]: channel 0 tuning block */
	u32 mdns_dma;		/* t32_data_dc074 + 16 */
	u32 mdns_size;		/* t32_data_dc074 + 32 */
	u32 fps;		/* t32_data_dd30c: numerator << 16 | denominator */
};

static inline u32 *t32_vic_par_word(struct t32_vic_glob *g, u32 off)
{
	return (u32 *)(g->par + off);
}

static inline u32 t32_lib_vic_set_par(const struct t32_vicp_hooks *h, struct t32_vic_glob *g,
				      const struct t32_vicp_env *e, u32 idx, u32 mode, u32 fmt, u32 b7,
				      u32 b8, u32 dma)
{
	u8 *par = g->par;
	u32 w, frame, size, den, ms;

	fmt &= 0xff;
	*t32_vic_par_word(g, T32_VIC_PAR_FMTW) = fmt;
	par[T32_VIC_PAR_RAW_MODE] = (u8)mode;
	par[T32_VIC_PAR_IDX] = (u8)(idx & 1);
	par[T32_VIC_PAR_B7] = (u8)b7;
	par[T32_VIC_PAR_FMT] = (u8)b8;
	w = *(const u32 *)g->size[idx & 1];
	if (fmt == 1) {
		h->vic.log(h->vic.ctx, 1, 666);
	} else if (fmt == 0) {
		g->mdma_a1 = (w + 7) & ~7U;
	} else if (fmt == 2) {
		g->mdma_a1 = ((((w << 1) + w) >> 1) + 7) & ~7U;
		h->trace(h->vic.ctx, 670, g->mdma_a1, 0, 0);
	} else {
		g->mdma_a1 = ((w << 1) + 7) & ~7U;
	}
	frame = *(const u32 *)(g->size[par[T32_VIC_PAR_IDX]] + 4) * g->mdma_a1;
	h->trace(h->vic.ctx, 678, frame, 0, 0);
	*t32_vic_par_word(g, T32_VIC_PAR_FRAME) = frame;
	size = frame;
	if (par[T32_VIC_PAR_DMA] == 1) {
		if (par[T32_VIC_PAR_FMT] == 2)
			size = frame << 1;
	} else {
		size = frame * par[T32_VIC_PAR_B7];
	}
	h->trace(h->vic.ctx, 690, par[T32_VIC_PAR_RAW_MODE], 0, 0);
	switch (par[T32_VIC_PAR_RAW_MODE]) {
	case 0:
		g->raw_buf = h->kmalloc(h->vic.ctx, size, T32_VIC_KMALLOC_FLAGS);
		if (!g->raw_buf) {
			h->trace(h->vic.ctx, 694, size, T32_VIC_ERR, 0);
			return T32_VIC_ERR;
		}
		memset(g->raw_buf, 0, size);
		g->raw_dma = h->to_dma(h->vic.ctx, g->raw_buf);
		return 0;
	case 1:
		g->raw_dma = h->malloc_buffer(h->vic.ctx, size);
		g->raw_buf = h->to_virt(h->vic.ctx, g->raw_dma);
		h->trace(h->vic.ctx, 703, g->raw_dma, 0, 0);
		return 0;
	case 2:
		if (e->mdns_size < size) {
			h->trace(h->vic.ctx, 706, e->mdns_size, size, T32_VIC_ERR);
			return T32_VIC_ERR;
		}
		e->tuning0[T32_TUNING_B15] = 1;
		h->vic.top_reg_cfg(h->vic.ctx, 0);
		den = e->fps >> 16;
		ms = den ? (e->fps & 0xffff) * 1000U / den : ~0U;	/* MIPS divu by 0 (vendor) */
		h->msleep(h->vic.ctx, ms + 1);
		g->raw_dma = e->mdns_dma;
		g->raw_buf = h->to_virt(h->vic.ctx, g->raw_dma);
		return 0;
	case 3:
		g->raw_dma = dma;
		return 0;
	default:
		h->trace(h->vic.ctx, 721, T32_VIC_ERR, 0, 0);
		return T32_VIC_ERR;
	}
}

static inline u32 t32_vic_save_one(const struct t32_vicp_hooks *h, struct t32_vic_glob *g, const char *fmt,
				   u32 n)
{
	char name[T32_VIC_NAME_LEN];

	snprintf(name, sizeof(name), fmt, n, "raw");
	return h->save_file(h->vic.ctx, g->raw_buf, *t32_vic_par_word(g, T32_VIC_PAR_FRAME), name);
}

static inline u32 t32_lib_vic_save_raw(const struct t32_vicp_hooks *h, struct t32_vic_glob *g,
				       const struct t32_vicp_env *e, u32 vic, u32 mode, u32 b7, u32 b8)
{
	u32 r;

	h->trace(h->vic.ctx, 794, mode & 0xff, 0, 0);
	r = t32_lib_vic_set_par(h, g, e, vic & 0xff, mode & 0xff, 3, b7 & 0xff, b8 & 0xff, 0);
	if (r)
		return r;
	t32_lib_vic_dma_start(&h->vic, g);
	if (g->par[T32_VIC_PAR_DMA] != 1)
		return t32_vic_save_one(h, g, "/tmp/snap%d.%s", 0);
	if (g->par[T32_VIC_PAR_FMT] == 2)
		t32_vic_save_one(h, g, "/tmp/snap%d_long.%s", 0);
	else if (g->par[T32_VIC_PAR_FMT] != 1)
		return t32_vic_save_one(h, g, "/tmp/snap%d_long.%s", 0);
	return t32_vic_save_one(h, g, "/tmp/snap%d_short.%s", 1);
}

#endif
