/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC raw capture of one frame (by frame index) to a file.
 *
 *   t32_lib_vic_save_frame_index_raw  lib_tisp_vic_save_frame_index_raw 0xae9c4: set_par(vic, request raw mode,
 *     fmt, b7, b8, 0) (error: returned).  Request "by index" word 1: frame index (1..10, else log and return
 *     the printk result) into par byte 10, frames left (e3f48) = b7, DMA control of VIC 0 (0x801ac) = 0x4410 /
 *     0x4414 / 0x4440 when the DMA flag is 1 (as lib_tisp_vic_dma_start), then start the MDMA at once (index 1,
 *     no frame counted yet on this VIC) or arm the re-arm flag (e3f40) for lib_tisp_vic_intp_handle; wait for the
 *     raw completion (2000 jiffies); interrupted (-ERESTARTSYS): log, lib_tisp_raw_free.  Otherwise: raw DMA start.
 *     Save mode 1: lib_tisp_save_file(raw buffer, frame size, request name), with DMA flag 1 and b8 2 also
 *     "<name>_short"; save mode 2: log only (returns the printk result); other: the snapshot names of
 *     lib_tisp_vic_save_raw.  Returns the last lib_tisp_save_file result.
 *
 * Vendor behaviour kept: the DMA control write goes to VIC 0 whatever the VIC; with save mode 1, DMA flag 1 and
 * b8 2 the same buffer is saved twice; the wait result other than -ERESTARTSYS (timeout 0) is not checked.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libvicf_oracle).
 */
#ifndef TX_ISP_T32_LIBVICF_H
#define TX_ISP_T32_LIBVICF_H

#include "tx_isp_t32_libvicp.h"

#define T32_VICF_NAME_LEN	256U
#define T32_VICF_MAX_INDEX	10U
#define T32_VICF_WAIT		2000U
#define T32_VICF_ERESTARTSYS	((u32)-512)
#define T32_VIC_PAR_INDEX	10U	/* frame index of the capture */

/* request (vendor layout) */
struct t32_vic_raw_req {
	char name[128];		/* +0 */
	u8 fmt;			/* +128 */
	u8 pad129[3];
	u32 save_mode;		/* +132 */
	u32 by_index;		/* +136 */
	u8 pad140;
	u8 raw_mode;		/* +141 */
	u8 b7;			/* +142 */
	u8 b8;			/* +143 */
	u8 index;		/* +144 */
};

struct t32_vicf_hooks {
	struct t32_vicp_hooks p;
	u32 (*printk)(void *ctx, u32 line, u32 a);		/* printk whose result the vendor returns */
	u32 (*wait)(void *ctx, void *completion, u32 timeout);	/* private_wait_for_completion_timeout */
	u8 *tuning0;						/* lib_tisp_raw_free: t32_data_e1484[0] */
};

static inline u32 t32_vicf_save(const struct t32_vicp_hooks *h, struct t32_vic_glob *g, const char *fmt,
				const char *arg, u32 n, int with_n)
{
	char name[T32_VICF_NAME_LEN];

	if (with_n)
		snprintf(name, sizeof(name), fmt, n, "raw");
	else
		snprintf(name, sizeof(name), fmt, arg);
	return h->save_file(h->vic.ctx, g->raw_buf, *t32_vic_par_word(g, T32_VIC_PAR_FRAME), name);
}

static inline u32 t32_lib_vic_save_frame_index_raw(const struct t32_vicf_hooks *h, struct t32_vic_glob *g,
						   const struct t32_vicp_env *e, u32 vic,
						   const struct t32_vic_raw_req *req)
{
	const struct t32_vic_hooks *v = &h->p.vic;
	u8 *par = g->par;
	u32 r, val;

	vic &= 0xff;
	r = t32_lib_vic_set_par(&h->p, g, e, vic, req->raw_mode, req->fmt, req->b7, req->b8, 0);
	if (r)
		return r;
	if (req->by_index == 1) {
		par[T32_VIC_PAR_INDEX] = req->index;
		if ((u32)req->index - 1 >= T32_VICF_MAX_INDEX)
			return h->printk(v->ctx, 836, req->index);
		g->b48 = par[T32_VIC_PAR_B7];
		if (par[T32_VIC_PAR_DMA] == 1) {
			val = par[T32_VIC_PAR_FMT] == 2 ? T32_VIC_DMA_FMT2 :
			      par[T32_VIC_PAR_FMT] == 1 ? T32_VIC_DMA_FMT1 : T32_VIC_DMA_DEFAULT;
			v->sys_write(v->ctx, T32_VICG_BASE(0) + T32_VICG_DMA_CTRL, val);
		}
		if (par[T32_VIC_PAR_INDEX] == 1 && ((const u32 *)g->cfg[vic])[2] == 0)
			v->mdma_enable(v->ctx, g->par, g->mdma_a1, 1, g->raw_dma);
		else
			par[12] = 1;	/* e3f40: re-arm in lib_tisp_vic_intp_handle */
		if (h->wait(v->ctx, &g->frame_cnt, T32_VICF_WAIT) == T32_VICF_ERESTARTSYS) {
			v->log(v->ctx, 2, 861);
			t32_lib_raw_free(v, g, h->tuning0);
		}
	} else {
		t32_lib_vic_dma_start(v, g);
	}
	if (req->save_mode == 1) {
		if (par[T32_VIC_PAR_DMA] == 1 && par[T32_VIC_PAR_FMT] == 2) {
			h->p.save_file(v->ctx, g->raw_buf, *t32_vic_par_word(g, T32_VIC_PAR_FRAME), req->name);
			return t32_vicf_save(&h->p, g, "%s_short", req->name, 0, 0);
		}
		return h->p.save_file(v->ctx, g->raw_buf, *t32_vic_par_word(g, T32_VIC_PAR_FRAME), req->name);
	}
	if (req->save_mode == 2)
		return h->printk(v->ctx, 884, 0);
	if (par[T32_VIC_PAR_DMA] != 1)
		return t32_vicf_save(&h->p, g, "/tmp/snap%d.%s", NULL, 0, 1);
	if (par[T32_VIC_PAR_FMT] == 2)
		t32_vicf_save(&h->p, g, "/tmp/snap%d_long.%s", NULL, 0, 1);
	else if (par[T32_VIC_PAR_FMT] != 1)
		return t32_vicf_save(&h->p, g, "/tmp/snap%d_long.%s", NULL, 0, 1);
	return t32_vicf_save(&h->p, g, "/tmp/snap%d_short.%s", NULL, 1, 1);
}

#endif
