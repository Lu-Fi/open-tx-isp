// SPDX-License-Identifier: GPL-2.0
/*
 * T33 (PRJ008) per-frame AWB driver glue: tisp_awb_init's state part at
 * stream-on, and per AWB statistics interrupt tisp_awb_interrupt_static
 * (buffer index from 0x1608c into num_stat) + tisp_awb_process (event 12)
 * in a work.  Results go to the AWBG list 7 (gains) and the AWB list 29
 * (spec_calculate) through tx_isp_t32_hw.c, which runs the lists at the
 * next frame end like the vendor's tispAwbgRefresh / tispAwbRefresh.
 * T33 only: the T32 build does not compile this file.
 *
 * Vendor-layout AWB state (openimp #99 wiring group 10b): the module statics
 * of the vendor AWB are kept as the vendor has them: awbInfo[0] = the 24 byte
 * block { par, st, aux, mode, hw, drops } of tisp_awb_init (par = the AWB
 * section copy, the vendor's tparamsP + 0xd48; st = the run state in the T33
 * layout, 0x1345c B, T32_AWB_SO; aux = the 0x9a B hardware parameter block;
 * mode 28 B; hw = the 0x10c4 B history block; drops = the tag mismatch count),
 * pst_awb_ct_detect[0] = the 192 B detection table, stAwbStatBuff[0] = the
 * 12 B statistics block of the proven tisp_awb_malloc_info (block awbm:
 * physical base, buffers, count 4) and num_stat[0].  tisp_awb_process runs
 * the oracle-proven tisp_t33_awb_long_alogrithm (block awbL) on them, with
 * the T32 port (Matt Davis' recovered C, PR #15) for long_par_update,
 * spec_calculate, ct_detect and set_gain on the same blocks.  Deviation:
 * stAwbStatBuff word 1 is the uncached alias of the DMA buffers (the vendor
 * reads the cached kmalloc address).  The bin switch (lib_tisp_refresh_para)
 * runs tisp_awb_dn_params_refresh on the new section (t33_awbc_dn_refresh).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/seq_file.h>
#include "tx_isp_t32.h"
#include "tx_isp_t33_awbalg.h"
#include "tx_isp_t33_aemisc.h"
#include "tx_isp_t33_awbQ.h"
#include "tx_isp_t33_r11B.h"
#include "tx_isp_t33_i9F.h"

int isp_printf(unsigned int level, const char *fmt, ...);
u32 t33_hw_rd(u32 reg);
void t33_awb_list_gain(u32 r, u32 b);			/* tx_isp_t32_hw.c: AWBG list 7 slots 3 / 4 */
void t33_awb_list_slot(int slot, u32 v);		/* tx_isp_t32_hw.c: AWB list 29 slot */
void t33_awb_list_refresh(void);			/* tx_isp_t32_hw.c: run list 29 at the next AWB irq */

static struct t32_awb_init ai;
static struct t32_awb_ctd *ctd;
static struct t33_awbv awbv;
static u8 *par_copy;
static u16 counter;		/* the vendor's awb_count */
static bool ready;
static DEFINE_MUTEX(awbc_lock);
static struct {
	u32 frames, refused, gain_r, gain_b, ct, tg_updates, ev_updates, dn_refresh;
} awbc_st;

/* ---- the vendor module statics of the AWB (channel 0 in use) ---- */
#define T33_AWBV_PAR	0	/* awbInfo[ch] words */
#define T33_AWBV_ST	1
#define T33_AWBV_AUX	2
#define T33_AWBV_MODE	3
#define T33_AWBV_HW	4
#define T33_AWBV_DROPS	5
#define T33_AWBV_AUX_BYTES	0x9aU
static u32 awbInfo[4], stAwbStatBuff[4], num_stat[4], pst_awb_ct_detect[4];
static u32 awbv_blk[6];					/* *awbInfo[0] */
static u8 awbv_aux[T33_AWBV_AUX_BYTES + 2] __aligned(4);	/* awbInfo[0] word 2 */
static u32 awbv_stat[3];				/* *stAwbStatBuff[0] */
static unsigned long awbv_pages;			/* the DMA pages behind word 1 */
static u32 *awbv_list;					/* list 29 while tisp_awb_malloc_info runs */
/* the vendor's tgain_old / ev_last (tisp_long_tgain_update / tisp_long_ev_update), not reset per stream */
static u32 tgain_old, ev_last[2], ct_last, ct_new;

/* AWB event 11 (tisp_ct_update) after this frame's AE events, as the vendor's FIFO has it */
static void awbc_ct_fn(struct work_struct *w)
{
	(void)w;
	t33_ct_refresh(ACCESS_ONCE(ct_new));
}
static DECLARE_WORK(awbc_ct_work, awbc_ct_fn);

/* tisp_awb_set_hardware_param: its list 29 words are in the init list; the state part here */
static void drv_hw_param(void)
{
	t33_awb_regional_threshold(&ai.a);
	t33_awb_lum_th_freq(&ai.a);
}

/*
 * tisp_awb_init (group 15, port-loop block i9F, tx_isp_t33_i9F.h) on the vendor blocks: private_kmalloc by
 * size = awbInfo[0] (awbv_blk), the ct_detect table, the hardware parameter block (awbv_aux, kept: the
 * bytes of tisp_awb_malloc_info, which ran when list 29 was built), the run state, mode, the history block.
 * Callees: tisp_awb_params_refresh = the proven awbQ port, tisp_awb_show_para_update = the proven r11B port,
 * set_hardware_param = its state part (list 29 words are in the init list), set_gain = the T32 port (the
 * start gains are in list 7); rcfg register, malloc_info, irq 3 / event 12, RISC-V: the driver's fixed set-up.
 */
static u8 *awbi_info[4], *awbi_ctd[4];
static u32 awbi_runs[2];	/* init runs, irq / event number mismatches */
static u8 *awbi_tp[4];
static u8 awbi_riscv[4], awbi_g_refresh, awbi_refresh;

static void *awbi_kmalloc(u32 size, u32 flags)
{
	(void)flags;
	switch (size) {
	case 24:
		return awbv_blk;
	case T33_AWBV_AUX_BYTES:
		return awbv_aux;
	case T32_AWBI_TBL_BYTES:
		return ai.tbl = kzalloc(T32_AWBI_TBL_BYTES, GFP_KERNEL);
	case 0x1345c:	/* the T33 run state; the T32-form struct over it is larger (T32-port words behind it) */
		return ai.a.st = vzalloc(max_t(u32, sizeof(struct t32_awb_state), 0x1345cU));
	case T32_AWBI_MODE_BYTES:
		return ai.mode = kzalloc(T32_AWBI_MODE_BYTES, GFP_KERNEL);
	case T32_AWBI_HW_BYTES:
		return ai.a.hw = kzalloc(T32_AWBI_HW_BYTES, GFP_KERNEL);
	}
	return NULL;
}
static void *awbi_vmalloc(u32 size) { (void)size; return NULL; }
static void awbi_fill(void *p, u32 c, u32 n) { if (p) memset(p, (int)c, n); }
static void awbi_copy(void *d, const void *s, u32 n) { if (d) memcpy(d, s, n); }
static void awbi_hook(u32 id, u32 a, u32 b, u32 c, u32 d)
{
	struct t32_stats_write w[T32_AWB_MAX_WRITES];

	(void)c; (void)d;
	switch (id) {
	case T33_I9_ERR:
		isp_printf(2, "tx-isp-t33: no memory for the AWB context\n");
		break;
	case T33_I9_AWB_PARAMS:
		if (ai.a.st)
			t33_awbQ_params_refresh((u8 *)(unsigned long)b, ai.a.st->raw);
		break;
	case T33_I9_AWB_SHOW:
		if (ai.a.hw)
			t33_awb_show_para_update((struct t33_awb_ctx *const *)awbi_info, a);
		break;
	case T33_I9_HW:
		if (ai.a.st && ai.a.hw)
			drv_hw_param();
		break;
	case T33_I9_GAIN:
		if (ai.a.st && ai.a.hw)
			t32_awb_set_gain(&ai.a, w);
		break;
	case T33_I9_IRQ:
		awbi_runs[1] += b != T32_AWBI_IRQ;
		break;
	case T33_I9_EVENT:
		awbi_runs[1] += b != T32_AWBI_EVENT;
		break;
	default:	/* rcfg register 29 / 7, malloc_info, RISC-V: done by the driver / not used */
		break;
	}
}
static const struct t33_i9_ops awbi_ops = { awbi_kmalloc, awbi_vmalloc, awbi_fill, awbi_copy, awbi_hook };

static u32 drv_rd(void *p, u32 reg) { (void)p; return t33_hw_rd(reg); }

static void awbc_free(void)
{
	/* awbInfo / pst_awb_ct_detect first: nothing may reach the freed blocks */
	awbInfo[0] = 0;
	pst_awb_ct_detect[0] = 0;
	memset(awbv_blk, 0, sizeof(awbv_blk));
	kfree(ai.tbl); kfree(ai.ctx); kfree(ai.mode); kfree(ai.a.hw);
	vfree(ai.a.st);
	vfree(ctd);
	kfree(par_copy);
	memset(&ai, 0, sizeof(ai));
	memset(&awbv, 0, sizeof(awbv));
	ctd = NULL;
	par_copy = NULL;
}

static void awbc_work_fn(struct work_struct *w)
{
	struct t33_awb_out *o;
	const u32 *info, *stat;
	unsigned int i;
	int s;

	(void)w;
	o = kmalloc(sizeof(*o), GFP_KERNEL);
	if (!o)
		return;
	mutex_lock(&awbc_lock);
	if (!ready || !awbInfo[0] || !stAwbStatBuff[0])
		goto out;
	info = (const u32 *)(unsigned long)awbInfo[0];
	stat = (const u32 *)(unsigned long)stAwbStatBuff[0];
	/* tisp_awb_process (event 12) on the buffer num_stat[0] of tisp_awb_interrupt_static */
	if (t33_awb_process_v(&awbv, (u8 *)(unsigned long)pst_awb_ct_detect[0], (const u8 *)(unsigned long)stat[1],
			      ACCESS_ONCE(num_stat[0]), (u8 *)&info[T33_AWBV_DROPS], &counter, o)) {
		awbc_st.refused++;
		goto out;
	}
	for (i = 0; i < o->nspec; i++) {
		s = t33_awb_slot(o->spec[i].reg);
		if (s >= 0)
			t33_awb_list_slot(s, o->spec[i].val);
	}
	if (o->gain_set)
		t33_awb_list_gain(o->gain_r, o->gain_b);
	awbc_st.frames++;
	awbc_st.gain_r = o->gain_r;
	awbc_st.gain_b = o->gain_b;
	awbc_st.ct = o->ct;
	if (o->ct != ct_last) {
		ct_last = o->ct;
		ACCESS_ONCE(ct_new) = o->ct;
		t33_evq_queue(&awbc_ct_work);
	}
out:
	mutex_unlock(&awbc_lock);
	kfree(o);
}
static DECLARE_WORK(awbc_work, awbc_work_fn);

/* the AWB statistics interrupt: tisp_awb_interrupt_static's num_stat, then event 12 */
void t33_awbc_kick(void)
{
	if (ACCESS_ONCE(ready)) {
		ACCESS_ONCE(num_stat[0]) = t33_awb_buf_idx(t33_hw_rd(T33_AWB_IDX_REG));
		t33_evq_queue(&awbc_work);
	}
}

/*
 * AE events from the AE work (same ordered queue, after this frame's AWB
 * work as the vendor's event FIFO has them):
 *   6 (long EV, v0 / v1 = low / high word): tisp_long_ev_update ->
 *     tisp_awb_ev_update on an EV change;
 *   4 (long total gain, log2 Q16; v1 = force): tisp_long_tgain_update ->
 *     tisp_awb_tgain_update on a change: thresholds, list 29 words and
 *     tispAwbRefresh (list 29 at the next AWB interrupt).
 */
void t33_awbc_ae_event(u32 type, u32 v0, u32 v1)
{
	u8 slot[T33_AWB_TG_SLOTS];
	u32 val[T33_AWB_TG_SLOTS];
	unsigned int k;

	mutex_lock(&awbc_lock);
	if (!ready)
		goto out;
	if (type == 6 && (v0 != ev_last[0] || v1 != ev_last[1])) {
		t33_awb_ev_update(&ai.a, v0);
		ev_last[0] = v0;
		ev_last[1] = v1;
		awbc_st.ev_updates++;
	} else if (type == 4 && (v0 != tgain_old || v1)) {
		t33_awb_tgain_update(&ai.a, v0);
		t33_awb_tg_slots(&ai.a, slot, val);
		for (k = 0; k < T33_AWB_TG_SLOTS; k++)
			t33_awb_list_slot(slot[k], val[k]);
		t33_awb_list_slot(1, 1);	/* system_reg_set_awb_trig(1) */
		t33_awb_list_refresh();
		tgain_old = v0;
		awbc_st.tg_updates++;
	}
out:
	mutex_unlock(&awbc_lock);
}

void t33_awbc_detach(void)
{
	ACCESS_ONCE(ready) = false;
	cancel_work_sync(&awbc_work);
	cancel_work_sync(&awbc_ct_work);
	mutex_lock(&awbc_lock);
	awbc_free();
	mutex_unlock(&awbc_lock);
}

/*
 * tisp_awb_malloc_info (from tisp_awb_init, list 29 being built in list):
 * awbInfo[0] with its hardware parameter block, then the proven port: the
 * 12 B statistics block (stAwbStatBuff[0]: physical base, the buffers,
 * count 4) on the caller's DMA pages (4 x 0x7400, KSEG0) and list 29
 * slots 11..14.
 */
static void *am_kmalloc(u32 size, u32 flags)
{
	(void)flags;
	if (size == sizeof(awbv_stat))
		return awbv_stat;
	return size == 4 * T33_AWB_STAT_ONE ? (void *)awbv_pages : NULL;
}
static void am_kfree(void *p) { (void)p; }
static void am_reg(u32 ch, u32 mod, u32 slot, u32 val)
{
	(void)ch;
	if (mod == T33_MOD_AWB && awbv_list)
		T33_SW(awbv_list, slot, val);
}
static void am_err(int level, int line) { isp_printf(level, "tx-isp-t33: AWB info allocation failed (line %d)\n", line); }
static const struct t33_aemisc_ops am_ops = { am_kmalloc, am_kfree, am_reg, am_err };

int t33_awbc_malloc_info(u32 *list, unsigned long pages)
{
	t33_awbc_detach();
	mutex_lock(&awbc_lock);
	memset(awbv_aux, 0, sizeof(awbv_aux));
	memset(awbv_stat, 0, sizeof(awbv_stat));
	awbv_blk[T33_AWBV_AUX] = (u32)(unsigned long)awbv_aux;
	awbInfo[0] = (u32)(unsigned long)awbv_blk;
	stAwbStatBuff[0] = 0;
	awbv_pages = pages;
	awbv_list = list;
	t33_awb_malloc_info(0, &stAwbStatBuff[0], &awbInfo[0], &am_ops);
	awbv_list = NULL;
	awbInfo[0] = 0;		/* published again by t33_awbc_attach with the rest of the block */
	mutex_unlock(&awbc_lock);
	return awbv_stat[1] ? 0 : -ENOMEM;
}

/* tisp_awb_set_hardware_param (unless frozen): its list 29 words, the thresholds at st tgain, tispAwbRefresh */
static void awbc_set_hw_param(void)
{
	u8 slot[T33_AWB_TG_SLOTS];
	u32 val[T33_AWB_TG_SLOTS];
	unsigned int k;

	t33_awb_hw_slots(awbv_aux, slot, val);
	for (k = 0; k < T33_AWB_HW_SLOTS; k++)
		t33_awb_list_slot(slot[k], val[k]);
	t33_awb_regional_threshold(&ai.a);
	t33_awb_lum_th_freq(&ai.a);
	t33_awb_tg_slots(&ai.a, slot, val);
	for (k = 0; k < T33_AWB_TG_SLOTS; k++)
		t33_awb_list_slot(slot[k], val[k]);
	t33_awb_list_slot(1, 1);	/* system_reg_set_awb_trig */
	t33_awb_list_refresh();
}

/*
 * stream-on, after the AWB lists and t33_awbc_malloc_info: sec = the IQ bin
 * AWB section (T32 field offsets, 6740 B used), bufs = the uncached alias of
 * the statistics buffers.  The start gains of tisp_awb_init's set_gain are
 * already in list 7, its hardware parameters in list 29.
 */
int t33_awbc_attach(const u8 *sec, u32 len, u32 width, u32 height, const u8 *bufs)
{
	u8 sz[16] = { 0 };
	u32 *stat;
	int ret = 0;

	cancel_work_sync(&awbc_work);
	cancel_work_sync(&awbc_ct_work);
	t33_evq_get();
	mutex_lock(&awbc_lock);
	ACCESS_ONCE(ready) = false;
	awbc_free();
	memset(&awbc_st, 0, sizeof(awbc_st));
	counter = 0;
	stat = (u32 *)(unsigned long)stAwbStatBuff[0];
	if (len < sizeof(struct t32_awb_par) || !bufs || !stat || !stat[1]) {
		ret = -EINVAL;
		goto out;
	}
	stat[1] = (u32)(unsigned long)bufs;	/* uncached: the engine writes by DMA */
	par_copy = kmemdup(sec, sizeof(struct t32_awb_par), GFP_KERNEL);
	ctd = vzalloc(sizeof(*ctd));
	if (!par_copy || !ctd) {
		ret = -ENOMEM;
		goto out;
	}
	memset(&ai, 0, sizeof(ai));
	t33_aL_w16(sz, 12, width);
	t33_aL_w16(sz, 14, height);
	ai.a.par = (struct t32_awb_par *)par_copy;
	awbi_tp[0] = par_copy - 3400;	/* the vendor par = tparamsP[ch] + 3400 */
	t33_awb_init_ctx(0, sz, awbi_info, awbi_ctd, awbi_tp, (const u8 *)t32_awbi_gw_default, awbi_riscv,
			 &awbi_g_refresh, &awbi_refresh, &awbi_ops);
	awbi_runs[0]++;
	if (!ai.tbl || !ai.a.st || !ai.mode || !ai.a.hw || !awbi_info[0] || !awbi_ctd[0]) {
		ret = -ENOMEM;
		goto out;
	}
	ai.a.st->spec_ring = -1;	/* the T32 spec_calculate port's ring slot (no word of the T33 state) */
	ai.a.st->hist[0] = ai.a.hw->inv_r;
	ai.a.st->hist[1] = ai.a.hw->inv_b;
	t33_awb_tbl_set(ai.tbl, ai.a.st);
	awbv.a = &ai.a;
	awbv.ctd = ctd;
	awbv.rd = drv_rd;
	pst_awb_ct_detect[0] = (u32)(unsigned long)awbi_ctd[0];
	awbInfo[0] = (u32)(unsigned long)awbv_blk;
out:
	if (ret)
		awbc_free();
	ACCESS_ONCE(ready) = !ret;
	mutex_unlock(&awbc_lock);
	return ret;
}

/*
 * lib_tisp_refresh_para (IQ bin switch), after tisp_ae_switch_para:
 * tisp_bin_data_parse put the new AWB section into tparamsP (here: into
 * the section copy, in place), then tisp_awb_dn_params_refresh: no
 * detection history (st 0x94a8 = 0), the day / night reset (0x94a9 = 1),
 * tisp_awb_params_refresh (zone counts / sizes; the change state stays),
 * and unless frozen tisp_awb_set_hardware_param on the existing hardware
 * parameter block (the vendor does not refill it here).  Called without
 * t33_gr_lock (lock order awbc_lock -> list memory only, #572).
 */
void t33_awbc_dn_refresh(const u8 *sec, u32 len)
{
	mutex_lock(&awbc_lock);
	if (!ready || len < sizeof(struct t32_awb_par))
		goto out;
	memcpy(par_copy, sec, sizeof(struct t32_awb_par));
	ai.a.st->lg_have = 0;
	ai.a.st->lg_reset = 1;
	t32_awbi_params_refresh(&ai.a);
	if (!ai.a.st->frz)
		awbc_set_hw_param();
	awbc_st.dn_refresh++;
out:
	mutex_unlock(&awbc_lock);
}

void t33_awbc_show(struct seq_file *m)
{
	const u32 *info;

	mutex_lock(&awbc_lock);
	info = (const u32 *)(unsigned long)awbInfo[0];
	seq_printf(m, "t33 awb: %s frames %u drops %u refused %u ctd_bad %u gain r 0x%x b 0x%x ct %u tgain %u/0x%x ev %u/%u dn %u init %u bad %u\n",
		   ready ? "on" : "off", awbc_st.frames, info ? info[T33_AWBV_DROPS] : 0, awbc_st.refused,
		   awbv.ctd_bad, awbc_st.gain_r, awbc_st.gain_b, awbc_st.ct, awbc_st.tg_updates,
		   ready ? ai.a.st->tgain : 0, awbc_st.ev_updates, ready ? ai.a.st->ev : 0, awbc_st.dn_refresh,
		   awbi_runs[0], awbi_runs[1]);
	mutex_unlock(&awbc_lock);
}

/*
 * The tuning controls (../t32/tx_isp_t32_tunblk.c) on the live T33 AWB
 * blocks, as in ../t32/tx_isp_t32_awbc.c (same t32_awb_init state).
 */
bool t32_awbc_api_get(struct t32_awb_ctx *c)
{
	mutex_lock(&awbc_lock);
	if (!ready) {
		mutex_unlock(&awbc_lock);
		return false;
	}
	c->tune = (u8 *)ai.a.par;
	c->sec = ai.a.st->raw;
	c->mode = (u32 *)ai.mode;
	c->base = (u8 *)ai.a.par - T32_AWBI_PAR_OFF;
	return true;
}

void t32_awbc_api_put(void)
{
	mutex_unlock(&awbc_lock);
}

/*
 * tisp_awb_set_start's tail call tisp_awb_set_gain (lock held): the T33
 * takes the gain words (0x6040 / 0x6044 of the T32 write list) into the
 * AWBG list 7 and keeps the inverses in the history pair (t33_awb_process).
 */
void t32_awbc_api_set_gain(void)
{
	struct t32_stats_write w[T32_AWB_MAX_WRITES];
	unsigned int i, n = t32_awb_set_gain(&ai.a, w);
	u32 r = 0, b = 0;
	bool set = false;

	ai.a.st->hist[0] = ai.a.hw->inv_r;
	ai.a.st->hist[1] = ai.a.hw->inv_b;
	for (i = 0; i < n; i++) {
		if (w[i].reg == 0x6040) {
			r = w[i].val;
			set = true;
		} else if (w[i].reg == 0x6044) {
			b = w[i].val & 0xffff;
		}
	}
	if (set)
		t33_awb_list_gain(r, b);
}
