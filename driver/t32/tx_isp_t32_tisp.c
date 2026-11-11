// SPDX-License-Identifier: GPL-2.0
/*
 * T32 per-frame tisp paths: the oracle-proven ports of the vendor's per-frame
 * block refreshes (driver/t32/tx_isp_t32_<blk>.h, tests/t32_oracle) wired into
 * the driver.  Until now the driver programmed these blocks once at stream-on
 * (t32_tisp_init in tx_isp_t32_hw.c) and never again; the vendor refreshes
 * them on every AE run through tisp_long_tgain_update.
 *
 * Block 1, gain-interpolated filters: DMSC, YSP, CDNS, YDNS, SDNS; block 2 MDNS; block 3 DPC (long-frame gain);
 * block 4 GIB (black level, tisp_gib_again_update); block 5 RDNS (raw denoise, tisp_rdns_refresh, openimp #234);
 * block 6 LSC strength (tisp_lsc_tgain_update); block 7 LCE state and strengths (tisp_lce_init state,
 * tisp_lce_tgain_update; tisp_lce_par is not wired, see driver/t32/README.md).
 *  - At stream-on the init path hands each block its IQ bin section (the copy
 *    the init function already edited, as the vendor keeps tparams) and the
 *    context is built the way the vendor's tisp_<blk>_init leaves it: zeroed
 *    value block, last gain -1 (YDNS also hysteresis -1), DMSC sharpness 0x80.
 *  - Every AE histogram interrupt schedules one run of the ported
 *    tisp_long_tgain_update (t32_long_tgain_update, oracle t32_longtg) whose
 *    refresh hooks call the ported tisp_<blk>_refresh functions unchanged
 *    (oracles t32_<blk>p / t32_<blk>f); their register writes go out in order,
 *    direct register mode (t32_data_d03a0 = 0).
 *  - The total gain is tisp_tgain (default 0x10000, the gain the vendor init
 *    interpolates at) until the vendor AE runs in the driver.
 *
 * t32_ported_paths selects the path per wired block (device A/B): 1 (default)
 * every wired block on the ported path, 0 every block on the old path
 * (stream-on programming only), any other value a mask with bit 1 + block
 * (T32_PP_*) set for the blocks on the ported path.
 */
/*
 * tx_isp_t32_blkrest.h first: its struct t32_dnr_ops has a member wake_up,
 * which linux/wait.h (pulled in by mutex.h / workqueue.h) defines as a macro.
 */
#include "tx_isp_t32_blkrest.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/vmalloc.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_tp.h"
#include "tx_isp_t32_dmsc.h"
#include "tx_isp_t32_ysp.h"
#include "tx_isp_t32_cdns.h"
#include "tx_isp_t32_ydns.h"
#include "tx_isp_t32_sdns.h"
#include "tx_isp_t32_mdns.h"
#include "tx_isp_t32_dpc.h"
#include "tx_isp_t32_gib.h"
#include "tx_isp_t32_rdns.h"
#include "tx_isp_t32_rdnstab.h"
#include "tx_isp_t32_aeinit.h"
#include "tx_isp_t32_aemath.h"
#include "tx_isp_t32_fixp.h"
#include "tx_isp_t32_libwrap.h"
#include "tx_isp_t32_lsc.h"
#include "tx_isp_t32_lcetg.h"
#include "tx_isp_t32_lcem.h"

/*
 * Vendor t32_data_d03a0: 1 = registers go into the shadow image / register
 * list, 0 = direct writes.  The driver writes directly; the ported block
 * headers that read it (gamma, blkrest, blkglue) see 0.
 */
u8 t32_data_d03a0[4];

static unsigned int t32_ported_paths = 1;
module_param(t32_ported_paths, uint, 0644);
MODULE_PARM_DESC(t32_ported_paths,
		 "1 (default): every wired per-frame block on its ported path; 0: all on the old path; "
		 "else bit mask, bit 1+n = block n (0 dmsc, 1 ysp, 2 cdns, 3 ydns, 4 sdns, 5 mdns, 6 dpc, 7 gib, 8 rdns, 9 ae, "
		 "10 lsc, 11 lce, 12 tmo (not wired))");

static unsigned int tisp_tgain = 0x10000;
module_param(tisp_tgain, uint, 0644);
MODULE_PARM_DESC(tisp_tgain, "total gain handed to the per-frame refresh until the vendor AE runs (default 0x10000)");

bool t32_pp_on(enum t32_pp blk)
{
	unsigned int v = READ_ONCE(t32_ported_paths);

	if (v == 1)
		return true;
	return (v >> (1 + blk)) & 1;
}

#define T32_PF_MAX_WRITES	1024
#define T32_PF_PARAM_SMALL	64	/* >= the 55 / 39 / 22 values of SDNS / CDNS / YDNS */

static struct {
	struct t32_dmsc_ctx dmsc;
	struct t32_ysp_ctx ysp;
	struct t32_cdns_ctx cdns;
	struct t32_ydns_ctx ydns;
	struct t32_sdns_ctx sdns;
	struct t32_mdns_ctx mdns;
	struct t32_dpc_ctx dpc;
	struct t32_gib_ctx gib;
	struct t32_rdns_ctx rdns;
	struct t32_lsc_ctx lsc;		/* only sec, s, cur, img, dlog are used (tgain_update) */
	struct t32_lsc_cur lsc_cur;
	u32 lsc_img[T32_LSC_IMG_MESH];
	struct t32_lsc_dwr lsc_dlog[2];
	u8 *lce_st;		/* vendor t32_data_e1da8[0]: 0x22850 B, vmalloc */
	u8 *lce_sec;		/* the section copy the state works on (state word 0) */
	bool have[T32_PP_COUNT];
	bool ready;		/* every block of this stream attached */
	u32 last_tgain[1];	/* vendor t32_data_dd... last low gain word, per channel */
} pf;

static struct t32_front_write *pf_w;
static DEFINE_MUTEX(pf_lock);
struct t32_pf_stats t32_pf_stats;

static void pf_flush(int n)
{
	int i;

	if (n <= 0)
		return;
	if (n > T32_PF_MAX_WRITES) {
		isp_printf(2, "tx-isp-t32: per-frame write list overflow (%d)\n", n);
		return;
	}
	for (i = 0; i < n; i++)
		t32_hw_wr(pf_w[i].reg, pf_w[i].val);
	t32_pf_stats.writes += n;
}

/* ---- the hooks of the ported tisp_long_tgain_update ---- */

static u32 pf_reg_read(u32 ch, u32 reg)
{
	(void)ch;
	return t32_hw_rd(reg);
}

/* tisp_lsc_tgain_update (a2 = gain low word): direct mode, the strength register 0x5050 */
static int pf_lsc(u32 ch, u32 gain)
{
	unsigned int i;

	pf.lsc.ndlog = 0;
	t32_lsc_tgain_update(&pf.lsc, (u8)ch, gain);
	for (i = 0; i < pf.lsc.ndlog; i++) {
		pf_w[i].reg = pf.lsc.dlog[i].reg;
		pf_w[i].val = pf.lsc.dlog[i].val;
	}
	return pf.lsc.ndlog;
}

/* tisp_lce_tgain_update's strength hook: tisp_lce_tgain_interp_strength on the channel's state */
static void pf_lce_strength(u32 ch)
{
	(void)ch;
	t32_lce_tgain_interp_strength(pf.lce_st, pf.lce_sec);
}

static const struct t32_lcem_ops pf_lcem_ops = {
	.interp_strength = pf_lce_strength,
};

/*
 * The 64-bit gain hooks.  DPC: tisp_dpc_l_gain_update reads the low word (a2
 * of the o32 a2:a3 pair); LSC: tisp_lsc_tgain_update likewise; LCE:
 * tisp_lce_tgain_update(ch, gain, EV) on the state (no register write: the
 * strengths feed tisp_lce_par).  TMO / AWB: see below.
 */
static void pf_gain(enum t32_ltg_gain blk, u32 ch, u64 gain, u32 x, u32 y)
{
	int n = 0;

	(void)x; (void)y;
	if (blk == T32_LTG_DPC && pf.have[T32_PP_DPC] && t32_pp_on(T32_PP_DPC))
		n = t32_dpc_l_gain_update(&pf.dpc, (u8)ch, (u32)gain, pf_w);
	else if (blk == T32_LTG_LSC && pf.have[T32_PP_LSC] && t32_pp_on(T32_PP_LSC))
		n = pf_lsc(ch, (u32)gain);
	else if (blk == T32_LTG_LCE && pf.have[T32_PP_LCE] && t32_pp_on(T32_PP_LCE)) {
		struct t32_lcem l = { .ops = &pf_lcem_ops, .st = { pf.lce_st } };

		t32_lce_tgain_update(&l, 0, (u32)gain, (u32)(gain >> 32), x, y);
	}
	pf_flush(n);
}

static void pf_refresh(enum t32_ltg_ref blk, u32 ch, u32 gain)
{
	int n = 0;

	switch (blk) {
	case T32_LTG_DMSC:
		if (pf.have[T32_PP_DMSC] && t32_pp_on(T32_PP_DMSC))
			n = t32_dmsc_refresh(&pf.dmsc, gain, pf_w);
		break;
	case T32_LTG_YSP:
		if (pf.have[T32_PP_YSP] && t32_pp_on(T32_PP_YSP))
			n = t32_ysp_refresh(&pf.ysp, (u8)ch, gain, pf_w);
		break;
	case T32_LTG_CDNS:
		if (pf.have[T32_PP_CDNS] && t32_pp_on(T32_PP_CDNS))
			n = t32_cdns_refresh(&pf.cdns, (u8)ch, gain, pf_w);
		break;
	case T32_LTG_YDNS:
		if (pf.have[T32_PP_YDNS] && t32_pp_on(T32_PP_YDNS))
			n = t32_ydns_refresh(&pf.ydns, (u8)ch, gain, pf_w);
		break;
	case T32_LTG_SDNS:
		if (pf.have[T32_PP_SDNS] && t32_pp_on(T32_PP_SDNS))
			n = t32_sdns_refresh(&pf.sdns, (u8)ch, gain, pf_w);
		break;
	case T32_LTG_MDNS:
		if (pf.have[T32_PP_MDNS] && t32_pp_on(T32_PP_MDNS))
			n = t32_mdns_refresh(&pf.mdns, gain, pf_w);
		break;
	case T32_LTG_GIB:	/* tisp_gib_again_update(ch, gain low word) */
		if (pf.have[T32_PP_GIB] && t32_pp_on(T32_PP_GIB))
			n = t32_gib_again_update(&pf.gib, (u8)ch, gain, pf_w);
		break;
	case T32_LTG_RDNS:	/* tisp_rdns_refresh(gain low word), threshold 256 */
		if (pf.have[T32_PP_RDNS] && t32_pp_on(T32_PP_RDNS))
			n = t32_rdns_refresh(&pf.rdns, gain, pf_w);
		break;
	default:	/* AF: not wired yet */
		return;
	}
	pf_flush(n);
}

static const struct t32_ltg_ops pf_ltg_ops = {
	.reg_read = pf_reg_read,
	.gain = pf_gain,
	.refresh = pf_refresh,
};

static bool pf_any_filter(void)
{
	int b;

	for (b = 0; b < T32_PP_COUNT; b++)
		if (pf.have[b] && t32_pp_on(b))
			return true;
	return false;
}

static void pf_work_fn(struct work_struct *w)
{
	(void)w;
	mutex_lock(&pf_lock);
	if (pf_w && pf.ready && pf_any_filter()) {
		t32_long_tgain_update(&pf_ltg_ops, t32_data_d03a0, pf.last_tgain, 0,
				      READ_ONCE(tisp_tgain), 0, 0, 0, 0);
		t32_pf_stats.runs++;
	}
	mutex_unlock(&pf_lock);
}
static DECLARE_WORK(pf_work, pf_work_fn);

/* AE histogram interrupt (the vendor runs tisp_long_tgain_update from its AE process) */
void t32_pf_kick(void)
{
	if (READ_ONCE(t32_ported_paths))
		schedule_work(&pf_work);
}

static void ae_free(void);

static void pf_free_locked(void)
{
	kfree(pf.dmsc.sec);
	kfree(pf.dmsc.param);
	kfree(pf.ysp.sec);
	kfree(pf.ysp.param);
	kfree(pf.cdns.sec);
	kfree(pf.cdns.param);
	kfree(pf.ydns.sec);
	kfree(pf.ydns.param);
	kfree(pf.sdns.sec);
	kfree(pf.sdns.param);
	kfree(pf.mdns.sec);
	kfree(pf.mdns.val);
	kfree(pf.dpc.sec);
	kfree(pf.dpc.o);
	kfree(pf.gib.sec);
	kfree(pf.gib.cp);
	kfree(pf.gib.gw);
	kfree(pf.gib.ptrow);
	kfree(pf.gib.hold);
	kfree(pf.rdns.sec);
	kfree(pf.rdns.param);
	kfree(pf.lsc.sec);
	vfree(pf.lce_st);
	kfree(pf.lce_sec);
	memset(&pf, 0, sizeof(pf));
	ae_free();
}

/* stream-on, before the init blocks: drop the contexts of the last stream */
void t32_pf_reset(void)
{
	cancel_work_sync(&pf_work);
	mutex_lock(&pf_lock);
	pf_free_locked();
	if (!pf_w)
		pf_w = kmalloc_array(T32_PF_MAX_WRITES, sizeof(*pf_w), GFP_KERNEL);
	mutex_unlock(&pf_lock);
}

/*
 * Take over the section copy sec (kmalloc'd, may be NULL) of block blk after
 * its init function ran; the context is left as the vendor's tisp_<blk>_init
 * leaves it.  sec is freed when the block is not taken.
 */
void t32_pf_attach(enum t32_pp blk, u8 *sec)
{
	size_t vals;
	u8 *param;

	if (!sec)
		return;
	switch (blk) {
	case T32_PP_DMSC:
		vals = T32_DMSC_VALUES;
		break;
	case T32_PP_YSP:
		vals = T32_YSP_VALUES;
		break;
	case T32_PP_CDNS:
	case T32_PP_YDNS:
	case T32_PP_SDNS:
		vals = T32_PF_PARAM_SMALL;
		break;
	case T32_PP_DPC:
		vals = T32_DPC_VALUES;
		break;
	default:
		kfree(sec);
		return;
	}
	param = kzalloc(vals, GFP_KERNEL);
	if (!param) {
		kfree(sec);
		return;
	}
	mutex_lock(&pf_lock);
	switch (blk) {
	case T32_PP_DMSC:
		pf.dmsc.sec = sec;
		pf.dmsc.param = param;
		pf.dmsc.gain = (u32)-1;
		pf.dmsc.sharpness = 0x80;
		break;
	case T32_PP_YSP:
		pf.ysp.sec = sec;
		pf.ysp.param = param;
		pf.ysp.gain = (u32)-1;
		break;
	case T32_PP_CDNS:
		pf.cdns.sec = sec;
		pf.cdns.param = param;
		pf.cdns.gain = (u32)-1;
		break;
	case T32_PP_YDNS:
		pf.ydns.sec = sec;
		pf.ydns.param = param;
		pf.ydns.gain = (u32)-1;
		pf.ydns.threshold = (u32)-1;	/* vendor tisp_ydns_init: ctx +12 = -1 */
		break;
	case T32_PP_SDNS:
		pf.sdns.sec = sec;
		pf.sdns.param = param;
		pf.sdns.gain = (u32)-1;
		break;
	case T32_PP_DPC:
		/*
		 * tisp_dpc_init (recovered C): the four 11-point tables from the
		 * section, WDR byte 0, wide byte (0 with this front end), both gains
		 * 0, zeroed value block, then the gain interpolation of both sets.
		 */
		pf.dpc.sec = sec;
		pf.dpc.o = param;
		memcpy(pf.dpc.tbl, sec + 6, 22);
		memcpy(pf.dpc.tbl + 22, sec + 270, 22);
		memcpy(pf.dpc.tbl + 44, sec + 28, 22);
		memcpy(pf.dpc.tbl + 66, sec + 292, 22);
		pf.dpc.wdr = 0;
		pf.dpc.wide = 0;
		pf.dpc.gain = 0;
		pf.dpc.gain1 = 0;
		t32_dpc_gain_interp(&pf.dpc, 0);
		t32_dpc_gain_interp(&pf.dpc, 1);
		break;
	default:
		break;
	}
	pf.have[blk] = true;
	mutex_unlock(&pf_lock);
}

/*
 * MDNS: the section copy and the value block of t32_mdns_init (both
 * kmalloc'd) with the frame size and compression setting it ran with; the
 * vendor's tisp_mdns_init leaves the context gain at -1, the hold flags
 * (t32_data_f0900, t32_data_e2150) are 0 outside AOV.
 */
void t32_pf_attach_mdns(u8 *sec, u8 *val, u32 width, u32 height, u32 memopt)
{
	if (!sec || !val) {
		kfree(sec);
		kfree(val);
		return;
	}
	mutex_lock(&pf_lock);
	pf.mdns.sec = sec;
	pf.mdns.val = val;
	pf.mdns.gain = (u32)-1;
	pf.mdns.width = width;
	pf.mdns.height = height;
	pf.mdns.memopt = memopt;
	pf.have[T32_PP_MDNS] = true;
	mutex_unlock(&pf_lock);
}

/*
 * GIB: the section copy t32_gib_init edited (kmalloc'd) and the Bayer
 * pattern it ran with.  The context is left as the vendor's tisp_gib_init
 * leaves it (recovered C): gain block = the vendor template (0x400, 0x400)
 * with +8 the pattern, +12 pattern >= 4, WDR byte +16 = 0; held levels
 * zeroed, flag 0; the working block from tisp_gib_interp_by_again,
 * tisp_gib_slope_update and tisp_gib_interp_by_again_s at gain word 0x400.
 * Rerunning them on the section the init already edited gives the same
 * working block and section (host check: wake-up writes = init writes).
 */
void t32_pf_attach_gib(u8 *sec, u32 bayer)
{
	struct t32_gib_ctx c = { .sec = sec };
	int row = t32_gib_ptr_row(bayer);

	if (!sec)
		return;
	if (row < 0)
		goto fail;
	c.cp = kzalloc(T32_GIB_VALUES, GFP_KERNEL);
	c.gw = kzalloc(24, GFP_KERNEL);
	c.ptrow = kmemdup(t32_gib_ptr_tab[row], sizeof(t32_gib_ptr_tab[row]), GFP_KERNEL);
	c.hold = kzalloc(T32_GIB_HOLD_BYTES, GFP_KERNEL);
	if (!c.cp || !c.gw || !c.ptrow || !c.hold)
		goto fail;
	t32_gib_put32(c.gw, 0x400);
	t32_gib_put32(c.gw + 4, 0x400);
	t32_gib_put32(c.gw + 8, bayer & 0x1f);
	t32_gib_put32(c.gw + 12, (bayer & 0x1f) >= 4);
	t32_gib_interp_set(&c, 0x400, 0);
	t32_gib_slope_update(&c);
	t32_gib_interp_set(&c, 0x400, 1);
	mutex_lock(&pf_lock);
	pf.gib = c;
	pf.have[T32_PP_GIB] = true;
	mutex_unlock(&pf_lock);
	return;
fail:
	kfree(c.cp);
	kfree(c.gw);
	kfree(c.ptrow);
	kfree(c.hold);
	kfree(sec);
}

/*
 * RDNS (the vendor has no stream-on RDNS programming outside tisp_rdns_init,
 * which lib_tisp_init calls after GSM): take over the section copy sec
 * (kmalloc'd, freed when not taken), build the context as tisp_rdns_init
 * leaves it (t32_rdns_ctx_init: parameter block from the unity-gain
 * interpolation, gain 0x10000, the vendor row table) and return its writes in
 * w (all RDNS registers, trigger, block id), <= 0 when not taken.  Taken only
 * when every row selector of the section stays inside the 128-row vendor
 * table (true for all 67 SDK bins); the vendor would read past it otherwise.
 */
int t32_pf_attach_rdns(u8 *sec, struct t32_front_write *w)
{
	struct t32_rdns_ctx c;
	u8 *param;
	int n;

	if (!sec)
		return -ENOMEM;
	if (!t32_rdns_rows_ok(sec)) {
		isp_printf(2, "tx-isp-t32: RDNS row selector outside the vendor table, RDNS not programmed\n");
		kfree(sec);
		return -EINVAL;
	}
	param = kzalloc(T32_RDNS_PARAM_BYTES, GFP_KERNEL);
	if (!param) {
		kfree(sec);
		return -ENOMEM;
	}
	n = t32_rdns_ctx_init(&c, sec, param, w);
	mutex_lock(&pf_lock);
	pf.rdns = c;
	pf.have[T32_PP_RDNS] = true;
	mutex_unlock(&pf_lock);
	return n;
}

/*
 * LSC (stream-on list, t32_rcfg_stream_on_fill): the 120 section bytes, the
 * info block of t32_lsc_setup (geometry, strengths at gain 0: the vendor's
 * after tisp_lsc_init) and the current values as tisp_lsc_init leaves them
 * (t32_lsc_cur_init; both checked by t32_lscx_oracle).  Direct register mode
 * (t32_data_d03a0 = 0): tisp_lsc_tgain_update writes 0x5050.
 */
void t32_pf_attach_lsc(const u8 *sec, const struct t32_lsc *s, u32 width, u32 height, u32 bayer, u32 ct)
{
	u8 *cp;

	if (!t32_pp_on(T32_PP_LSC))
		return;
	cp = kmemdup(sec, T32_LSC_RING_TABLES, GFP_KERNEL);
	if (!cp)
		return;
	mutex_lock(&pf_lock);
	kfree(pf.lsc.sec);
	memset(&pf.lsc, 0, sizeof(pf.lsc));
	pf.lsc.sec = cp;
	pf.lsc.s = *s;
	t32_lsc_cur_init(&pf.lsc_cur, width, height, bayer, ct);
	pf.lsc.cur = &pf.lsc_cur;
	pf.lsc.img = pf.lsc_img;
	pf.lsc.dlog = pf.lsc_dlog;
	pf.lsc.width = width;
	pf.lsc.height = height;
	pf.lsc.bayer = bayer;
	pf.lsc.direct = t32_data_d03a0[0];
	pf.have[T32_PP_LSC] = true;
	mutex_unlock(&pf_lock);
}

/*
 * LCE (after its stream-on registers, t32_lce_build): the state the vendor's
 * tisp_lce_init leaves (t32_lce_state_init, host check t32_lceinit_oracle)
 * on a copy of the section (the vendor edits its tuning block, the state's
 * first word points to it).  vmalloc'd like the vendor's, freed at
 * stream-off (t32_pf_stop) or the next stream-on.
 */
void t32_pf_attach_lce(const u8 *sec, u32 width, u32 height)
{
	u8 *cp, *st;

	if (!t32_pp_on(T32_PP_LCE) || width < 64 || height < 36)	/* size_para_calc divides by the zone count */
		return;
	cp = kmemdup(sec, T32_LCE_SEC_BYTES, GFP_KERNEL);
	st = vmalloc(T32_LCETG_STATE_BYTES);
	if (!cp || !st) {
		kfree(cp);
		vfree(st);
		return;
	}
	t32_lce_state_init(st, cp, width, height);
	t32_lcetg_wr32(st, (u32)(uintptr_t)cp);
	mutex_lock(&pf_lock);
	vfree(pf.lce_st);
	kfree(pf.lce_sec);
	pf.lce_st = st;
	pf.lce_sec = cp;
	pf.have[T32_PP_LCE] = true;
	mutex_unlock(&pf_lock);
}

/* ---- vendor AE context (openimp #234): tisp_ae_init state part, not yet run per frame ---- */

static u32 ae_div32(u32 sh, u32 a, u32 b) { return t32_aem_div32(sh, a, b); }
static u32 ae_mult2_32(u32 sh, u32 a, u32 b) { return t32_aem_mult2_32(sh, a, b); }
static u32 ae_mult3_32(u32 sh, u32 a, u32 b, u32 c) { return t32_aem_mult3_32(sh, a, b, c); }
static t32_u64 ae_mult2(u32 sh, t32_u64 a, t32_u64 b) { return t32_fixp_mult2(sh, a, b); }
static t32_u64 ae_mult3_64(u32 sh, t32_u64 a, t32_u64 b, t32_u64 c) { return t32_aem_mult3_64(sh, a, b, c); }
static u32 ae_exp2(u32 v, u32 iq, u32 oq) { return t32_aem_exp2(v, iq, oq); }
static void ae_warn(u32 line)
{
	isp_printf(2, "tx-isp-t32: AE sensor ratio word without integer part, vendor default (line %u)\n", line);
}
static const struct t32_ae_init_ops pf_ae_ops = {
	ae_div32, ae_mult2_32, ae_mult3_32, ae_mult2, ae_mult3_64, ae_exp2, ae_warn,
};

/* tisp_ae_par_sensor_trig: the trigger flag (e1514[ch]) is 0 until tisp_ae_init, nothing to mark yet */
static u32 ae_sensor_trig(void *ctx, u32 ch) { (void)ctx; (void)ch; return 0; }

static struct {
	struct t32_ae_blk b;
	u8 S[T32_SENSOR_ATTR_COPY];	/* the channel's t32_data_dd2d4 block */
	bool ready;
} ae;

static void ae_free(void)
{
	kfree(ae.b.ctx); kfree(ae.b.T); kfree(ae.b.B); kfree(ae.b.R); kfree(ae.b.D); kfree(ae.b.C);
	memset(&ae, 0, sizeof(ae));
}

/*
 * The dd2d4 sensor block as the vendor builds it: ispcore_sync_sensor_attr
 * (tx_isp_t32_ispsync.h, the 124 B block from the sensor's attribute block
 * and its struct tx_isp_video_in), then lib_tisp_sync_sensor_attr
 * (t32_lib_sync_sensor_attr) copies its fields into the 232 B copy.
 */
static int ae_sensor_block(u8 *S)
{
	struct t32_lib_hooks h = { .ae_sensor_trig = ae_sensor_trig };
	const u8 *vin, *si;
	u8 blk[124] = { 0 };

	if (t32_core_sensor_ae_src(&vin, &si))
		return -ENODEV;
	memcpy(blk + 0, si + 276, 2);	/* total width */
	memcpy(blk + 2, si + 278, 2);	/* total height */
	memcpy(blk + 36, si + 160, 4);
	memcpy(blk + 40, si + 192, 4);
	memcpy(blk + 56, vin + 68, 12);
	memcpy(blk + 68, si + 220, 4);
	memcpy(blk + 72, si + 224, 4);
	memcpy(blk + 80, si + 224, 4);
	memcpy(blk + 84, si + 280, 4);
	memcpy(blk + 88, si + 236, 4);
	memcpy(blk + 92, si + 240, 4);
	memcpy(blk + 96, si + 176, 4);
	memcpy(blk + 108, si + 152, 4);
	memcpy(blk + 112, si + 216, 4);
	blk[116] = si[228];		/* low byte of the u16 */
	blk[117] = si[164];
	blk[123] = si[196] ? si[196] : 1;
	memset(S, 0, T32_SENSOR_ATTR_COPY);
	t32_lib_sync_sensor_attr(&h, S, 0, blk);
	return 0;
}

/*
 * Stream-on, after the AE statistics init: the AE context as tisp_ae_init
 * leaves it (t32_ae_ctx_init, oracle t32_aeinit_oracle) from a copy of the
 * IQ bin AE section (vendor tparams + 0x68) and the sensor block, plus the
 * vendor's eight AE register writes of its tail.  The per-frame AE
 * (tisp_ae_interrupt_static / calc_process) does not run on it yet.
 */
int t32_pf_attach_ae(const u8 *sec, u32 width, u32 height)
{
	struct t32_stats_write w[8];
	u8 info[64] = { 0 };
	int n, i;

	mutex_lock(&pf_lock);
	ae_free();
	if (ae_sensor_block(ae.S)) {
		mutex_unlock(&pf_lock);
		return -ENODEV;
	}
	ae.b.T = kmemdup(sec, T32_AEI_T_BYTES, GFP_KERNEL);
	ae.b.ctx = kzalloc(T32_AEI_CTX_BYTES, GFP_KERNEL);
	ae.b.B = kzalloc(T32_AEI_B_BYTES, GFP_KERNEL);
	ae.b.R = kzalloc(T32_AEI_R_BYTES, GFP_KERNEL);
	ae.b.D = kzalloc(T32_AEI_D_BYTES, GFP_KERNEL);
	ae.b.C = kzalloc(T32_AEI_C_BYTES, GFP_KERNEL);
	if (!ae.b.T || !ae.b.ctx || !ae.b.B || !ae.b.R || !ae.b.D || !ae.b.C) {
		ae_free();
		mutex_unlock(&pf_lock);
		return -ENOMEM;
	}
	t32_ae_state_tmpl(ae.b.R);
	t32_wr32(info, 0, width);
	t32_wr32(info, 4, height);
	t32_wr32(info, 12, t32_core_sensor_fps());
	n = t32_ae_ctx_init(0, &ae.b, ae.S, info, 1, &pf_ae_ops, w);
	if (n < 0) {
		ae_free();
		mutex_unlock(&pf_lock);
		return n;
	}
	for (i = 0; i < n; i++)
		t32_hw_wr(w[i].reg, w[i].val);
	ae.ready = true;
	mutex_unlock(&pf_lock);
	return 0;
}

/* end of the stream-on init: the per-frame runs may start */
void t32_pf_start(void)
{
	mutex_lock(&pf_lock);
	pf.ready = true;
	mutex_unlock(&pf_lock);
}

/* stream-off: no more per-frame runs, the contexts (LCE state included) are freed */
void t32_pf_stop(void)
{
	cancel_work_sync(&pf_work);
	mutex_lock(&pf_lock);
	pf_free_locked();
	mutex_unlock(&pf_lock);
}

void t32_pf_exit(void)
{
	cancel_work_sync(&pf_work);
	mutex_lock(&pf_lock);
	pf_free_locked();
	kfree(pf_w);
	pf_w = NULL;
	mutex_unlock(&pf_lock);
}
