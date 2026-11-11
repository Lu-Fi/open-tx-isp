/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AWB context init: the state part of the vendor tisp_awb_init (0x3c8f4)
 * for channel ch, on the blocks of tx_isp_t32_awb.h.
 *
 * Vendor flow (after tisp_awb_malloc_cfg, the statistics buffers and their
 * registers, done by t32_awb_stats_init at stream-on):
 *   e16a4[ch] = kmalloc(192), zeroed (the table long_par_update fills)
 *   e16b4[ch] = ctx = kmalloc(32), zeroed; printk when it fails (the vendor
 *               goes on and writes through the NULL pointer: refused here)
 *   ctx +28 = 0, e16c4[ch] word 0 (the completion flag of tisp_awb_process) = 0
 *   ctx +4 = st = kmalloc(0x1d48c), zeroed, then the defaults below
 *   ctx +8 = mode = kmalloc(28), zeroed; ctx +12 = hw = kmalloc(4292), zeroed
 *   ctx +0 = par = dd0a4[ch] + 3392 (the AWB section of the tuning copy)
 *   tisp_awb_params_refresh (t32_awbi_params_refresh), tisp_awb_show_para_update
 *   (par 5006 .. 5125 -> hw 0 .. 119), tisp_awb_set_riscv_param when
 *   f0900[ch] == 1 (hook; the RISC-V AWB is not used by this driver),
 *   st 119944 = -1, and unless frozen (st 107597, 0 after the init)
 *   tisp_awb_set_hardware_param (hook: the statistics init writes it) and
 *   tisp_awb_set_gain (t32_awb_set_gain).  Then system_irq_func_set(ch, 3,
 *   tisp_awb_interrupt_static) and tisp_event_set_cb(ch, 12,
 *   tisp_awb_process) (hooks) and a printk.
 *
 * The allocations go through ops->alloc in the vendor order and size; the
 * blocks are zeroed here.  Vendor stores that set_gain overwrites (st off_r /
 * off_b) are kept.  Checked against Matt Davis' recovered C (PR #15 @
 * 2e2e06bc) by tests/t32_oracle (t32_awbinit_oracle: every byte of st, hw,
 * mode, the table, the par section, the context words, the allocation /
 * hook calls and the register writes of set_gain).
 */
#ifndef TX_ISP_T32_AWBINIT_H
#define TX_ISP_T32_AWBINIT_H

#include "tx_isp_t32_awbproc.h"

#define T32_AWBI_TBL_BYTES	192U
#define T32_AWBI_CTX_BYTES	32U
#define T32_AWBI_ST_BYTES	0x1d48cU
#define T32_AWBI_MODE_BYTES	28U
#define T32_AWBI_HW_BYTES	4292U
#define T32_AWBI_PAR_OFF	3392U	/* vendor par = dd0a4[ch] + 3392 */
#define T32_AWBI_IRQ		3U	/* system irq of the AWB statistics */
#define T32_AWBI_EVENT		12U	/* event of tisp_awb_process */

/* the vendor default gray world class table (t32_data_d6410, data.c:25265, 514 u16) */
static const u16 t32_awbi_gw_default[514] = {
	256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256,
	256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 256, 254, 252, 250,
	248, 246, 244, 242, 240, 238, 236, 234, 233, 231, 229, 227, 225, 224, 222, 220,
	218, 217, 215, 213, 212, 210, 208, 207, 205, 203, 202, 200, 199, 197, 195, 194,
	192, 191, 189, 188, 186, 185, 183, 182, 181, 179, 178, 176, 175, 174, 172, 171,
	170, 168, 167, 166, 164, 163, 162, 160, 159, 158, 157, 155, 154, 153, 152, 151,
	149, 148, 147, 146, 145, 144, 142, 141, 140, 139, 138, 137, 136, 135, 134, 133,
	132, 131, 130, 129, 128, 127, 126, 125, 124, 123, 122, 121, 120, 119, 118, 117,
	116, 115, 114, 113, 112, 112, 111, 110, 109, 108, 107, 106, 106, 105, 104, 103,
	102, 101, 101, 100, 99, 98, 98, 97, 96, 95, 94, 94, 93, 92, 92, 91,
	90, 89, 89, 88, 87, 87, 86, 85, 85, 84, 83, 83, 82, 81, 81, 80,
	79, 79, 78, 78, 77, 76, 76, 75, 75, 74, 73, 73, 72, 72, 71, 71,
	70, 69, 69, 68, 68, 67, 67, 66, 66, 65, 65, 64, 64, 63, 63, 62,
	62, 61, 61, 60, 60, 59, 59, 58, 58, 57, 57, 57, 56, 56, 55, 55,
	54, 54, 53, 53, 53, 52, 52, 51, 51, 51, 50, 50, 49, 49, 49, 48,
	48, 48, 47, 47, 46, 46, 46, 45, 45, 45, 44, 44, 44, 43, 43, 43,
	42, 42, 42, 41, 41, 41, 40, 40, 40, 39, 39, 39, 38, 38, 38, 37,
	37, 37, 37, 36, 36, 36, 35, 35, 35, 35, 34, 34, 34, 34, 33, 33,
	33, 33, 32, 32, 32, 32, 31, 31, 31, 31, 30, 30, 30, 30, 29, 29,
	29, 29, 28, 28, 28, 28, 28, 27, 27, 27, 27, 26, 26, 26, 26, 26,
	25, 25, 25, 25, 25, 24, 24, 24, 24, 24, 24, 23, 23, 23, 23, 23,
	22, 22, 22, 22, 22, 22, 21, 21, 21, 21, 21, 21, 20, 20, 20, 20,
	20, 20, 19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18, 18,
	17, 17, 17, 17, 17, 17, 17, 16, 16, 16, 16, 16, 16, 16, 16, 15,
	15, 15, 15, 15, 15, 15, 15, 15, 14, 14, 14, 14, 14, 14, 14, 14,
	14, 13, 13, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12,
	12, 12, 12, 12, 12, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
	11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6,
	6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 6,
};

struct t32_awb_init_ops {
	void *priv;
	void *(*alloc)(void *priv, u32 size);		/* private_kmalloc */
	void (*riscv)(void *priv, u32 ch);		/* tisp_awb_set_riscv_param */
	void (*hw_param)(void *priv, u32 ch);		/* tisp_awb_set_hardware_param */
	void (*irq_set)(void *priv, u32 ch, u32 irq);	/* system_irq_func_set(.., tisp_awb_interrupt_static) */
	void (*event_set)(void *priv, u32 ch, u32 ev);	/* tisp_event_set_cb(.., tisp_awb_process) */
	void (*print)(void *priv, int ok);		/* printk: 0 context allocation failed, 1 init done */
};

/* what tisp_awb_init leaves (vendor e16a4[ch], e16b4[ch] and its words, e16c4[ch] word 0) */
struct t32_awb_init {
	struct t32_awb a;	/* par, st, hw */
	u8 *tbl;		/* e16a4[ch] */
	u8 *ctx;		/* e16b4[ch]: 32 bytes; words 0 / 1 / 2 / 3 = par / st / mode / hw as 32 bit addresses */
	u8 *mode;		/* ctx +8 */
	u32 proc_flag;		/* e16c4[ch] word 0 */
};

static inline void *t32_awbi_zalloc(const struct t32_awb_init_ops *o, u32 size)
{
	u8 *p = o->alloc(o->priv, size);

	if (p)
		memset(p, 0, size);
	return p;
}

/* the run state defaults of tisp_awb_init */
static inline void t32_awbi_state(struct t32_awb_state *st, u32 width, u32 height)
{
	u8 *r = st->raw;
	unsigned int i;

	for (i = 0; i < 1024; i++)
		st->zt2[i] = 1;
	st->out_w = 512;
	st->in_w = 512;
	t32_awb_put16at(r, 28690, width);	/* sensor size, read by params_refresh */
	t32_awb_put16at(r, 28692, height);
	r[28758] = 0;
	r[28759] = 1;
	memcpy(st->gw_tab, t32_awbi_gw_default, sizeof(t32_awbi_gw_default));
	st->off_r = 1024;
	st->off_b = 1024;
	for (i = 0; i < 15; i++) {
		st->ring_r[i] = 1024;
		st->ring_b[i] = 1024;
		st->ring_ct[i] = 5000;
	}
	st->ev = 400;
	st->tgain = 1024;
	st->man_ct = 5000;
	st->man_r = 256;
	st->man_b = 256;
	for (i = 0; i < 6; i++)
		st->avg[i] = 256;
	for (i = 0; i < 4; i++)
		st->hist[i] = 256;
}

/*
 * tisp_awb_params_refresh on the context: the zone divisors / tables of the
 * section (t32_awb_params_refresh, with the sensor size as the u16 st
 * 28690 / 28692 the vendor reads), and unless the day / night reset
 * (st 79037) or an update (st 107596) is pending the long algorithm's change
 * state is restarted (par +8 = 1, +12 = 0); the reset flag is cleared.
 */
static inline void t32_awbi_params_refresh(struct t32_awb *a)
{
	struct t32_awb_state *st = a->st;

	t32_awb_params_refresh(a->par->raw + 3184, t32_st16(st->raw, 28690), t32_st16(st->raw, 28692));
	if (st->lg_reset == 0 && st->update == 0) {
		a->par->lg_cnt_a = 1;
		a->par->lg_cnt_b = 0;
	}
	st->lg_reset = 0;
}

/*
 * tisp_awb_init, state part.  par = the AWB section of the channel's tuning
 * copy (vendor dd0a4[ch] + T32_AWBI_PAR_OFF, IQ bin section 2, 6740 B),
 * width / height from the sensor info (words 0 / 1), riscv = f0900[ch].
 * w gets set_gain's register writes (<= T32_AWB_MAX_WRITES); returns their
 * number, or -12 (-ENOMEM) when an allocation fails (the blocks allocated
 * so far stay in ai for the caller to free).
 */
static inline int t32_awb_init(struct t32_awb_init *ai, u32 ch, u8 *par, u32 width, u32 height, u8 riscv,
			       const struct t32_awb_init_ops *o, struct t32_stats_write *w)
{
	struct t32_awb *a = &ai->a;
	unsigned int n = 0, i;

	memset(ai, 0, sizeof(*ai));
	ai->tbl = t32_awbi_zalloc(o, T32_AWBI_TBL_BYTES);
	ai->ctx = t32_awbi_zalloc(o, T32_AWBI_CTX_BYTES);
	if (!ai->ctx) {
		o->print(o->priv, 0);
		return -12;
	}
	ai->proc_flag = 0;
	a->st = t32_awbi_zalloc(o, T32_AWBI_ST_BYTES);
	if (!a->st)
		return -12;
	t32_awbi_state(a->st, width, height);
	ai->mode = t32_awbi_zalloc(o, T32_AWBI_MODE_BYTES);
	a->hw = t32_awbi_zalloc(o, T32_AWBI_HW_BYTES);
	if (!ai->tbl || !ai->mode || !a->hw)
		return -12;
	a->par = (struct t32_awb_par *)par;
	t32_wr32(ai->ctx, 0, (u32)(uintptr_t)par);
	t32_wr32(ai->ctx, 4, (u32)(uintptr_t)a->st);
	t32_wr32(ai->ctx, 8, (u32)(uintptr_t)ai->mode);
	t32_wr32(ai->ctx, 12, (u32)(uintptr_t)a->hw);
	t32_awbi_params_refresh(a);
	/* tisp_awb_show_para_update: six 20 byte rows */
	for (i = 0; i < 6; i++)
		memcpy(a->hw->raw + 20 * i, par + 5006 + 20 * i, 20);
	if (riscv == 1)
		o->riscv(o->priv, ch & 0xff);
	a->st->spec_ring = -1;
	if (a->st->frz == 0) {
		o->hw_param(o->priv, ch);
		n = t32_awb_set_gain(a, w);
	}
	o->irq_set(o->priv, ch & 0xff, T32_AWBI_IRQ);
	o->event_set(o->priv, ch, T32_AWBI_EVENT);
	o->print(o->priv, 1);
	return (int)n;
}

/*
 * One AWB frame of the driver: tisp_awb_interrupt_static (system irq 3: the
 * filled statistics buffer into the run state; the vendor's dma sync and
 * event 12 are the caller's) and tisp_awb_process (event 12).  Returns -1
 * when the long algorithm refuses (t32_awb_process); p gets the writes and
 * the event 11 (ct).  Checked with the init by t32_awbchain_oracle.
 */
static inline int t32_awb_frame(struct t32_awb_init *ai, struct t32_awb_ctd *d, u32 (*rd)(void *, u32), void *rdp,
				const u8 *bufs, u16 *counter, struct t32_awb_proc *p)
{
	u32 ev;

	t32_awb_interrupt_static(&ai->a, rd, rdp, bufs, &ev);
	return t32_awb_process(&ai->a, d, rd, rdp, ai->proc_flag, counter, p);
}

#endif /* TX_ISP_T32_AWBINIT_H */
