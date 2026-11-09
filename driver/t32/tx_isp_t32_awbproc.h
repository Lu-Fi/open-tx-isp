/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AWB per-frame entry, the vendor tisp_awb_process (0x3ed34, 3 calls and a counter around the leaves ported in
 * tx_isp_t32_awb.h / tx_isp_t32_awblong.h).  Vendor arguments: a0 = the channel.  The vendor keeps the pieces in the
 * data symbols e16a4 (per channel: the pointer table that long_par_update fills and long_alogrithm reads), e16b4 (the
 * channel context), e16c4 (per channel 16 bytes, the first word is the "frame-done completion wanted" flag) and e1690
 * (a 16 bit frame counter).  Ours takes the context areas and these values directly and returns what the vendor
 * writes outside them (register writes, the completion, the event).
 *
 * Phase 1 (L_3ed34 .. L_3ed94): tisp_awb_long_par_update, tisp_awb_spec_calculate, tisp_awb_long_alogrithm with the
 * parameter table choice of the first (t32_awb_proc_calc).
 */
#ifndef TX_ISP_T32_AWBPROC_H
#define TX_ISP_T32_AWBPROC_H

#include "tx_isp_t32_awblong.h"

/* words 2, 4 and 5 of the 6 word event the vendor builds on its stack (the other words are not written) */
struct t32_awb_ev {
	u32 type;	/* 11: AWB result */
	u32 ct;		/* colour temperature after the gains */
	u32 arg;	/* 0 */
};

/* what one run leaves outside the context */
struct t32_awb_proc {
	struct t32_awb_lp lp;
	struct t32_stats_write w[2 * T32_AWB_MAX_WRITES];	/* spec_calculate, then set_gain */
	unsigned int nw;
	int rc;			/* t32_awb_long_alogrithm: 0 ran, 1 left early, -1 refused */
	int path;		/* its apply path */
	int complete;		/* private_complete(ctx + 16) was called */
	struct t32_awb_ev ev;
};

/* phase 1; returns -1 when the long algorithm refuses the case (the vendor would run past one of its tables) */
static inline int t32_awb_proc_calc(struct t32_awb *a, struct t32_awb_ctd *d, u32 (*rd)(void *, u32), void *rdp,
				    struct t32_awb_proc *p)
{
	t32_awb_long_par_update(a, &p->lp);
	p->nw = t32_awb_spec_calculate(a, rd, rdp, p->w);
	p->path = 0;
	p->rc = t32_awb_long_alogrithm(a, d, p->lp.tbl, &p->path);
	return p->rc < 0 ? -1 : 0;
}

/* phase 2: flag = the first word of e16c4 + 16 * ch */
static inline void t32_awb_proc_gain(struct t32_awb *a, u32 flag, struct t32_awb_proc *p)
{
	p->complete = flag == 1;
	p->nw += t32_awb_set_gain(a, p->w + p->nw);
}

#endif /* TX_ISP_T32_AWBPROC_H */
