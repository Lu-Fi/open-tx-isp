/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AF and GSM blocks in the driver (openimp #234, #296): the vendor's
 * tisp_af_init / tisp_gsm_init as lib_tisp_init calls them, their interrupt
 * bits and events, composed from the oracle-proven block ports
 * (tx_isp_t32_af.h, oracle t32_af_oracle; tx_isp_t32_gsm.h, oracle
 * t32_gsm_oracle).  The driver glue is tx_isp_t32_afgsm.c.
 *
 *  - AF (tisp_af_init(ch, &{width, height})): the AF parameter block is the
 *    vendor's tuning base + 0xec30 = IQ bin section 24 (800 bytes, the
 *    t32_iq_oracle section table); its attribute record starts as the
 *    vendor's .data t32_data_d5c60 (+ ch * 24), the gain tables are
 *    t32_data_d6014 (rows of 12 words) and t32_data_d5c90 (rows of 5).
 *    Interrupt bit 6 (system_irq_func_set 6: tisp_af_interrupt_static, the
 *    statistics DMA slot -> blk arrays, event 15), event 15
 *    (tisp_event_set_cb: tisp_af_calc_process -> tisp_af_alogrithm, the
 *    metrics).  tisp_long_tgain_update hands the gain (tisp_af_tgain_update).
 *  - GSM (tisp_gsm_init(&{width, height})): the context starts as the
 *    vendor's .data t32_data_d6e00 .. d7e28 (scale 1, pixels 1, zone 0
 *    0xffff, zone bounds 0x0d / 0x40 / 0x90 / 0xc0, configuration 0x4080);
 *    the init writes 0x1904c.  Interrupt bit 7 (tisp_gsm_interrupt_hist: RAM
 *    read, event 16), bit 63 (tisp_gsm_irq_frmae_done: counter), event 16
 *    (tisp_gsm_process: zone weights).
 *  - The bits run in ascending order (lib_tisp_intp_process); the events
 *    are run later by the caller (the vendor's event thread).
 *
 * Vendor table rows: in mode 0 (every T32 IQ bin) tisp_af_par_init reads
 * rows 0..20 of d6014 and 0..44 of d5c90 (the whole vendor tables).  The
 * gain mode (par[768] != 0) indexes rows with a parameter byte (+ 1): the
 * vendor then reads the .data behind its table.  The tables are kept here as
 * the vendor lays them out (d5c90, then d6014), so a d5c90 row past 44 reads
 * d6014 as in the vendor; behind d6014 (vendor: t32_data_d6410 ..) this copy
 * has zero rows up to row 256 (named, beyond-vendor data not modelled).  The
 * gain index (tgain >> 16) is not bounded by the vendor either (vendor UB,
 * kept).
 *
 * Checked against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_afgsm_oracle): init equivalence on every T32 IQ bin
 * and random sections, then frames (bits 6 / 7 / 63, events 15 / 16, gain
 * updates, the AF / GSM API calls of the tuning ids).
 */
#ifndef TX_ISP_T32_AFGSM_H
#define TX_ISP_T32_AFGSM_H

#include "tx_isp_t32_af.h"
#include "tx_isp_t32_gsm.h"
#include "tx_isp_t32_iq.h"

#define T32_AFG_SEC_AF		24	/* IQ bin section of the AF parameter block (tuning base + 0xec30) */
#define T32_AFG_IRQ_AF		6
#define T32_AFG_IRQ_GSM		7
#define T32_AFG_IRQ_GSM_FD	63	/* bit 31 of the third status word (0x300a8) */
#define T32_AFG_EV_AF		T32_AF_EVENT_STAT
#define T32_AFG_EV_GSM		T32_GSM_EV_HIST
#define T32_AFG_ROWS		257U	/* u8 row index + 1 */
#define T32_AFG_D5C90_BYTES	900U
#define T32_AFG_D6014_BYTES	1020U
#define T32_AFG_TBL_BYTES	(T32_AFG_D5C90_BYTES + T32_AFG_ROWS * 48U)	/* d5c90, d6014, zero rows */
#define T32_AFG_GSM_CFG		0x4080U	/* t32_data_d7e28 (data.c:26935) */
#define T32_AFG_GSM_BOUND	0xc090400dU	/* t32_data_d6e00 + 0x420 (data.c:26165) */
#define T32_AFG_GSM_RSVD	0x00000f0fU	/* t32_data_d6e00 + 0x424 (data.c:26166) */

/* t32_data_d5c60, 48 bytes (data.c:24773..24784): the AF attribute records of channels 0 and 1 */
static const u32 t32_afg_d5c60[12] = {
	0x0, 0x0, 0x2a0001, 0xc80016, 0x3010013, 0x3a0f0f,
	0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
};

/* t32_data_d5c90, 900 bytes (data.c:24785..25009): 45 rows of 5 words */
static const u32 t32_afg_d5c90[225] = {
	0x38, 0x7e, 0x9e, 0x7e, 0x38,
	0x2c, 0x91, 0xc9, 0x91, 0x2c,
	0x0, 0xa2, 0x11b, 0xa2, 0x0,
	0xffb9, 0x76, 0x142, 0x76, 0xffb9,
	0xff96, 0x26, 0x12a, 0x26, 0xff96,
	0xff96, 0xffda, 0x12a, 0xffda, 0xff96,
	0xffb9, 0xff8a, 0x142, 0xff8a, 0xffb9,
	0x0, 0xff5e, 0x11b, 0xff5e, 0x0,
	0x2c, 0xff6f, 0xc9, 0xff6f, 0x2c,
	0x33, 0x86, 0xb0, 0x86, 0x33,
	0x1b, 0x9d, 0xf2, 0x9d, 0x1b,
	0xffdd, 0x94, 0x13c, 0x94, 0xffdd,
	0xffa5, 0x4e, 0x13c, 0x4e, 0xffa5,
	0xff96, 0x0, 0x12b, 0x0, 0xff96,
	0xffa5, 0xffb2, 0x13c, 0xffb2, 0xffa5,
	0xffdd, 0xff6c, 0x13c, 0xff6c, 0xffdd,
	0x1b, 0xff63, 0xf2, 0xff63, 0x1b,
	0x28, 0x91, 0xd0, 0x91, 0x28,
	0x0, 0x9f, 0x11f, 0x9f, 0x0,
	0xffc1, 0x75, 0x14b, 0x75, 0xffc1,
	0xff9e, 0x26, 0x13a, 0x26, 0xff9e,
	0xff9e, 0xffda, 0x13a, 0xffda, 0xff9e,
	0xffc1, 0xff8b, 0x14b, 0xff8b, 0xffc1,
	0x0, 0xff61, 0x11f, 0xff61, 0x0,
	0x16, 0x9a, 0xfa, 0x9a, 0x16,
	0xffe3, 0x91, 0x144, 0x91, 0xffe3,
	0xffb2, 0x4f, 0x150, 0x4f, 0xffb2,
	0xffa2, 0x0, 0x145, 0x0, 0xffa2,
	0xffb2, 0xffb1, 0x150, 0xffb1, 0xffb2,
	0xffe3, 0xff6f, 0x144, 0xff6f, 0xffe3,
	0x0, 0x99, 0x128, 0x99, 0x0,
	0xffce, 0x74, 0x15d, 0x74, 0xffce,
	0xffb0, 0x28, 0x15b, 0x28, 0xffb0,
	0xffb0, 0xffd8, 0x15b, 0xffd8, 0xffb0,
	0xffce, 0xff8c, 0x15d, 0xff8c, 0xffce,
	0xffec, 0x8a, 0x151, 0x8a, 0xffec,
	0xffc7, 0x50, 0x172, 0x50, 0xffc7,
	0xffb9, 0x0, 0x172, 0x0, 0xffb9,
	0xffc7, 0xffb0, 0x172, 0xffb0, 0xffc7,
	0xffe1, 0x70, 0x176, 0x70, 0xffe1,
	0xffcb, 0x29, 0x18e, 0x29, 0xffcb,
	0xffcb, 0xffd7, 0x18e, 0xffd7, 0xffcb,
	0xffe2, 0x4e, 0x19f, 0x4e, 0xffe2,
	0xffd9, 0x0, 0x1b3, 0x0, 0xffd9,
	0xffed, 0x29, 0x1d0, 0x29, 0xffed,
};

/* t32_data_d6014, 1020 bytes (data.c:25010..25264): 21 rows of 12 words (+ 3 words) */
static const u32 t32_afg_d6014[255] = {
	0x21, 0x100, 0x0, 0xff00, 0x1f5, 0xff0b, 0x100, 0x0, 0xff00, 0x1a1, 0xff4d, 0x21,
	0x24, 0x100, 0x0, 0xff00, 0x1bc, 0xfefb, 0x100, 0x0, 0xff00, 0x17a, 0xff3d, 0x24,
	0x24, 0x100, 0x0, 0xff00, 0x16f, 0xff2e, 0x100, 0x0, 0xff00, 0x11e, 0xff38, 0x24,
	0x24, 0x100, 0x0, 0xff00, 0x101, 0xff30, 0x100, 0x0, 0xff00, 0xa2, 0xff36, 0x24,
	0x24, 0x100, 0x0, 0xff00, 0x15, 0xff34, 0x100, 0x0, 0xff00, 0x7b, 0xff32, 0x24,
	0x24, 0x100, 0x0, 0xff00, 0xffeb, 0xff34, 0x100, 0x0, 0xff00, 0xff85, 0xff96, 0x24,
	0x42, 0x100, 0x0, 0xff00, 0x1ff, 0xff01, 0x100, 0x0, 0xff00, 0x126, 0xff95, 0x42,
	0x42, 0x100, 0x0, 0xff00, 0x1a6, 0xff3e, 0x100, 0x0, 0xff00, 0x106, 0xff75, 0x42,
	0x42, 0xec, 0x0, 0xff00, 0x14f, 0xff4d, 0x100, 0x0, 0xff00, 0xa2, 0xff68, 0x42,
	0x42, 0x100, 0x0, 0xff00, 0xf4, 0xff55, 0x100, 0x0, 0xff00, 0x25, 0xff61, 0x42,
	0x42, 0x100, 0x0, 0xff00, 0xffa0, 0xff5f, 0x100, 0x0, 0xff00, 0x60, 0xff5c, 0x42,
	0x5c, 0x100, 0x0, 0xff00, 0x1ff, 0xff01, 0x100, 0x0, 0xff00, 0xc0, 0xffba, 0x5c,
	0x5d, 0x100, 0x0, 0xff00, 0x19a, 0xff4b, 0x100, 0x0, 0xff00, 0x94, 0xff9e, 0x5d,
	0x5d, 0x100, 0x0, 0xff00, 0x139, 0xff65, 0x100, 0x0, 0xff00, 0x2e, 0xff8d, 0x5d,
	0x5d, 0x100, 0x0, 0xff00, 0xc8, 0xff74, 0x100, 0x0, 0xff00, 0xffb3, 0xff80, 0x5d,
	0x74, 0x100, 0x0, 0xff00, 0x1ff, 0xff01, 0x100, 0x0, 0xff00, 0x5f, 0xffce, 0x74,
	0x74, 0x100, 0x0, 0xff00, 0x194, 0xff52, 0x100, 0x0, 0xff00, 0x25, 0xffb6, 0x74,
	0x74, 0x100, 0x0, 0xff00, 0x12a, 0xff78, 0x100, 0x0, 0xff00, 0xffbe, 0xff8e, 0x74,
	0x8a, 0x100, 0x0, 0xff00, 0x1ff, 0xff01, 0x100, 0x0, 0xff00, 0x0, 0xffd4, 0x8a,
	0x8b, 0x100, 0x0, 0xff00, 0x190, 0xff57, 0x100, 0x0, 0xff00, 0xffb8, 0xffbe, 0x8b,
	0xa0, 0x100, 0x0, 0xff00, 0x1ff, 0xff01, 0x100, 0x0, 0xff00, 0xff8e, 0xffce, 0xa0,
	0x0, 0x0, 0x0,
};

struct t32_afg {
	struct t32_af_state af;
	struct t32_gsm_ctx gsm;
	struct t32_gsm_ram ram;
	u8 tbl[T32_AFG_TBL_BYTES];	/* d5c90, d6014 (vendor .data order), zero rows */
	u8 af_on, gsm_on;
};

static inline void t32_afg_put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* the vendor's module .data of both blocks (load state); ops stays the caller's */
static inline void t32_afg_data(struct t32_afg *g)
{
	const struct t32_af_ops *ops = g->af.ops;
	unsigned int i;

	memset(&g->af, 0, sizeof(g->af));
	g->af.ops = ops;
	memset(g->tbl, 0, sizeof(g->tbl));
	for (i = 0; i < 225U; i++)
		t32_afg_put32(g->tbl + 4U * i, t32_afg_d5c90[i]);
	for (i = 0; i < 255U; i++)
		t32_afg_put32(g->tbl + T32_AFG_D5C90_BYTES + 4U * i, t32_afg_d6014[i]);
	for (i = 0; i < 12U; i++)	/* channels 2, 3 overlap d5c90 as in the vendor */
		t32_afg_put32(g->af.attr[i / 6U] + 4U * (i % 6U), t32_afg_d5c60[i]);
	for (i = 0; i < 12U; i++)
		t32_afg_put32(g->af.attr[2U + i / 6U] + 4U * (i % 6U), t32_afg_d5c90[i]);
	g->af.tbl_row12 = g->tbl + T32_AFG_D5C90_BYTES;
	g->af.tbl_row5 = g->tbl;

	memset(&g->gsm, 0, sizeof(g->gsm));
	memset(&g->ram, 0, sizeof(g->ram));
	g->gsm.scale = 1;
	g->gsm.pixels = 1;
	g->gsm.api.zone[0] = T32_GSM_TOTAL;
	t32_afg_put32(g->gsm.api.bound, T32_AFG_GSM_BOUND);
	g->gsm.api.rsvd = T32_AFG_GSM_RSVD;
	g->gsm.cfg = T32_AFG_GSM_CFG;
	g->af_on = 0;
	g->gsm_on = 0;
}

/* the AF parameter section of an IQ bin (the vendor's tuning base + 0xec30), NULL when absent or short */
static inline const u8 *t32_afg_af_section(const u8 *bin, u32 bytes)
{
	u32 len = 0, off = t32_iq_section(bin, bytes, T32_AFG_SEC_AF, &len);

	return off && len >= T32_AF_PAR_BYTES ? bin + off : NULL;
}

/*
 * tisp_af_init(ch, {width, height}) on the AF parameter section sec (800
 * bytes, IQ bin section 24): the vendor copies it from tuning base + 0xec30.
 * Returns the register writes in w (malloc_cfg, all_reg_write).
 */
static inline int t32_afg_af_init(struct t32_afg *g, u32 ch, const u8 *sec, u32 width, u32 height,
				  struct t32_front_write *w)
{
	int n = t32_af_init(&g->af, ch, width, height, sec - T32_AF_PAR_IQ_OFF, w);

	g->af_on = 1;
	return n;
}

/* tisp_gsm_init({width, height}): 0x1904c */
static inline int t32_afg_gsm_init(struct t32_afg *g, u32 width, u32 height, struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_gsm_init(&g->gsm, width, height, w, &n);
	g->gsm_on = 1;
	return (int)n;
}

/*
 * lib_tisp_intp_process for the AF / GSM bits of the first status word
 * (bit 6 before bit 7) and bit 63 (third word, bit 31): the handlers the two
 * inits registered.  The GSM RAM read / acknowledge writes go to w.
 */
static inline int t32_afg_irq(struct t32_afg *g, u32 st0, u32 st3, u32 ch, u32 (*rd)(u32 reg),
			      void (*push)(u8 ch, const struct t32_gsm_event *ev), struct t32_front_write *w)
{
	unsigned int n = 0;

	if (g->af_on && (st0 & (1U << T32_AFG_IRQ_AF)))
		t32_af_interrupt_static(&g->af, ch & 0xffU);
	if (g->gsm_on && (st0 & (1U << T32_AFG_IRQ_GSM)))
		t32_gsm_interrupt_hist(&g->ram, (u8)ch, rd, push, w, &n);
	if (g->gsm_on && (st3 & (1U << (T32_AFG_IRQ_GSM_FD - 32))))
		t32_gsm_irq_frame_done(&g->ram, (u8)ch);
	return (int)n;
}

/* the event callbacks of the two inits: 15 tisp_af_calc_process, 16 tisp_gsm_process */
static inline void t32_afg_event(struct t32_afg *g, u32 ch, u32 type)
{
	if (type == T32_AFG_EV_AF && g->af_on)
		t32_af_calc_process(&g->af, ch);
	else if (type == T32_AFG_EV_GSM && g->gsm_on)
		t32_gsm_process(&g->gsm, &g->ram);
}

#endif /* TX_ISP_T32_AFGSM_H */
