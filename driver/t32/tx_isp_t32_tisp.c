// SPDX-License-Identifier: GPL-2.0
/*
 * T32 per-frame tisp paths: the oracle-proven ports of the vendor's per-frame
 * block refreshes (driver/t32/tx_isp_t32_<blk>.h, tests/t32_oracle) wired into
 * the driver.  Until now the driver programmed these blocks once at stream-on
 * (t32_tisp_init in tx_isp_t32_hw.c) and never again; the vendor refreshes
 * them on every AE run through tisp_long_tgain_update.
 *
 * Block 1, gain-interpolated filters: DMSC, YSP, CDNS, YDNS, SDNS; block 2 MDNS; block 3 DPC (long-frame gain).
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

#include "tx_isp_t32.h"
#include "tx_isp_t32_tp.h"
#include "tx_isp_t32_dmsc.h"
#include "tx_isp_t32_ysp.h"
#include "tx_isp_t32_cdns.h"
#include "tx_isp_t32_ydns.h"
#include "tx_isp_t32_sdns.h"
#include "tx_isp_t32_mdns.h"
#include "tx_isp_t32_dpc.h"

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
		 "else bit mask, bit 1+n = block n (0 dmsc, 1 ysp, 2 cdns, 3 ydns, 4 sdns, 5 mdns, 6 dpc)");

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

/*
 * The 64-bit gain hooks.  DPC: tisp_dpc_l_gain_update reads the low word (a2
 * of the o32 a2:a3 pair).  LSC / LCE / TMO / AWB: not wired yet (old path).
 */
static void pf_gain(enum t32_ltg_gain blk, u32 ch, u64 gain, u32 x, u32 y)
{
	int n = 0;

	(void)x; (void)y;
	if (blk == T32_LTG_DPC && pf.have[T32_PP_DPC] && t32_pp_on(T32_PP_DPC))
		n = t32_dpc_l_gain_update(&pf.dpc, (u8)ch, (u32)gain, pf_w);
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
	default:	/* GIB, RDNS, AF: not wired yet */
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
	memset(&pf, 0, sizeof(pf));
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

/* end of the stream-on init: the per-frame runs may start */
void t32_pf_start(void)
{
	mutex_lock(&pf_lock);
	pf.ready = true;
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
