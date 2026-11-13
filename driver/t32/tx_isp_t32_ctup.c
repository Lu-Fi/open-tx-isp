// SPDX-License-Identifier: GPL-2.0
/*
 * T32 colour temperature fan-out in the driver (openimp #234), behind
 * t32_ported_paths bit 15 (T32_PP_CT).
 *
 * The vendor AWB ends every frame with event 11 (the colour temperature);
 * its handler tisp_ct_update (t32_ct_update, tx_isp_t32_dispatch.h, oracle
 * t32_dispatch_oracle) hands it to the blocks whose bypass bit (register
 * 0x40) is clear: tisp_ccm_ct_update, tisp_lsc_ct_update,
 * tisp_bcsh_ct_update, tisp_clm_ct_update.  Here, after each AWB frame
 * (t32_awbc_kick work), with force = keep = 0 (the vendor's event 11 leaves
 * those two words unset on its stack; the normal path is the one it takes
 * when they are not 1 / 0):
 *   - CCM: the state of tisp_ccm_init kept at stream-on (t32_ccm_start
 *     instead of t32_ccm_init), t32_ccm_ct_update (oracle t32_ccmf_oracle);
 *   - BCSH: the state of tisp_bcsh_init kept (t32_bcsh_start),
 *     t32_bcsh_ct_update (oracle t32_bcshf_oracle); its write request goes
 *     out right after (the vendor writes it from the frame end interrupt,
 *     tisp_bcsh_irq_frame_down = t32_bcsh_frame_done);
 *   - CLM: the state as tisp_clm_init leaves it (tables at 5000 K, zone code,
 *     threshold 1) built next to the one-shot t32_clm_init, then
 *     t32_clm_ct_update_st (tx_isp_t32_clmct.h, oracle t32_clmct_oracle);
 *   - LSC: tisp_lsc_ct_update (t32_lscct_ct_update, tx_isp_t32_lscct.h,
 *     oracle t32_lscct_oracle) on the LSC state and list of
 *     tx_isp_t32_lscct.c (also behind bit 19, T32_PP_LSCCT): a changed mesh
 *     goes into the LSC register list, which runs at the next LSC frame
 *     done interrupt.
 *
 * The AE EV (event 6) runs tisp_long_ev_update (t32_long_ev_update,
 * tx_isp_t32_blkrest.h, oracle t32_longev_oracle) here: the AWB part
 * (t32_awbc_ev, behind bit 14), CCM (t32_ccm_ev_update) and BCSH
 * (t32_bcsh_ev_update with x = the AE brightness word, its write request at
 * once) behind t32_ported_paths bit 16 (T32_PP_EV, on the states of bit 15).
 * Gamma, defog, TMO and WDR EV updates are not wired here.
 */
/* tx_isp_t32_blkrest.h first (its wake_up member vs the linux/wait.h macro) */
#include "tx_isp_t32_blkrest.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_front.h"
#include "tx_isp_t32_ccm.h"
/* t32_bcsh_init is noinline in the kernel build and not used here (hw.c uses it) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "tx_isp_t32_bcsh.h"
#pragma GCC diagnostic pop
#include "tx_isp_t32_dispatch.h"
#include "tx_isp_t32_clmct.h"

extern u8 t32_data_d03a0[4];

int isp_printf(unsigned int level, const char *fmt, ...);

struct t32_ctup_drv_stats t32_ctup_stats;

static struct t32_ccm ccm;
static struct t32_bcsh bcsh;
static u8 *ccm_mem, *bcsh_mem;	/* section copy + knots behind it */
static bool ccm_on, bcsh_on, clm_on;
static struct t32_clm_ctx clm;
static u8 *clm_mem;
static u64 ev_last[1];		/* tisp_long_ev_update's last EV (vendor dd678) */
static const u8 ev_pipe[T32_LEV_PIPE_STRIDE];	/* WDR byte of the pipe: read on the forced path only */
static struct t32_front_write ct_w[T32_CTUP_MAX_WRITES];
static DEFINE_MUTEX(ctup_lock);

static void ct_flush(int n)
{
	int i;

	if (n <= 0)
		return;
	if (n > T32_CTUP_MAX_WRITES) {
		isp_printf(2, "tx-isp-t32: ct update write list overflow (%d)\n", n);
		return;
	}
	for (i = 0; i < n; i++)
		t32_hw_wr(ct_w[i].reg, ct_w[i].val);
	t32_ctup_stats.writes += n;
}

static u32 ct_rd(u32 ch, u32 reg) { (void)ch; return t32_hw_rd(reg); }
static void ct_wr(u32 ch, u32 reg, u32 val) { (void)ch; t32_hw_wr(reg, val); }

static void ct_ccm(u32 ch, u64 ct)
{
	(void)ch;
	if (ccm_on)
		ct_flush(t32_ccm_ct_update(&ccm, (u32)ct, ct_w));
}

static void ct_bcsh(u32 ch, u64 ct)
{
	(void)ch;
	if (!bcsh_on)
		return;
	t32_bcsh_ct_update(&bcsh, (u32)ct);
	ct_flush(t32_bcsh_frame_done(&bcsh, ct_w));
}

static void ct_clm(u32 ch, u64 ct)
{
	(void)ch;
	if (clm_on)
		ct_flush(t32_clm_ct_update_st(&clm, T32_CLMCT_THR, (u32)ct, ct_w));
}

static void ct_lsc(u32 ch, u64 ct)
{
	t32_lsc_drv_ct(ch, (u32)ct);
}

static void ct_none(u32 ch, u64 v) { (void)ch; (void)v; }
static void ct_none32(u32 ch, u32 v) { (void)ch; (void)v; }

static const struct t32_dispatch_ops ct_ops = {
	.reg_read = ct_rd,
	.reg_write = ct_wr,
	.ccm_ct_update = ct_ccm,
	.lsc_ct_update = ct_lsc,
	.bcsh_ct_update = ct_bcsh,
	.clm_ct_update = ct_clm,
	.gib_again_s_update = ct_none32,
	.dpc_s_gain_update = ct_none,
	.lsc_tgain_update_s = ct_none,
	.wdr_short_ev_update = ct_none,
};

/* one copy: the section (len bytes) and need bytes of the knots behind it */
static u8 *ct_copy(const u8 *sec, u32 len, u32 need, u32 avail)
{
	u8 *m;

	if (need > avail)
		return NULL;
	m = kmalloc(len + need, GFP_KERNEL);
	if (m)
		memcpy(m, sec, len + need);
	return m;
}

/* stream-on, CCM section (len bytes, then avail bytes of the bin): tisp_ccm_init keeping its state */
int t32_ctup_attach_ccm(const u8 *sec, u32 len, u32 avail)
{
	int n = -1;

	mutex_lock(&ctup_lock);
	ccm_on = false;
	kfree(ccm_mem);
	ccm_mem = len > 0x364 ? ct_copy(sec, len, (u32)sec[0x364] * 40U, avail) : NULL;
	memset(&ccm, 0, sizeof(ccm));	/* the vendor state is .bss */
	if (ccm_mem)
		n = t32_ccm_start(&ccm, ccm_mem, ccm_mem + len, (u32)sec[0x364] * 40U, ct_w);
	if (n > 0) {
		ct_flush(n);
		ccm_on = true;
	}
	mutex_unlock(&ctup_lock);
	return n < 0 ? -EINVAL : 0;
}

/* stream-on, BCSH section and the CSC section: tisp_bcsh_init keeping its state */
int t32_ctup_attach_bcsh(const u8 *sec, u32 len, u32 avail, const u8 *csc)
{
	int n = -1;

	mutex_lock(&ctup_lock);
	bcsh_on = false;
	kfree(bcsh_mem);
	bcsh_mem = len > 0x4af ? ct_copy(sec, len, (u32)sec[0x4af] * 40U, avail) : NULL;
	if (bcsh_mem)
		n = t32_bcsh_start(&bcsh, bcsh_mem, bcsh_mem + len, (u32)sec[0x4af] * 40U, csc, ct_w);
	if (n > 0) {
		ct_flush(n);
		bcsh_on = true;
	}
	mutex_unlock(&ctup_lock);
	return n < 0 ? -EINVAL : 0;
}

/* stream-on, CLM section 20 (after the one-shot t32_clm_init): the block state tisp_clm_init leaves */
int t32_ctup_attach_clm(const u8 *sec, u32 len, u32 avail)
{
	u32 need = len > 0x23 ? (u32)sec[0x23] * (T32_CLM_ZONE_BYTES + T32_CLM_TABLE) : 0;
	int ret = -EINVAL;

	mutex_lock(&ctup_lock);
	clm_on = false;
	kfree(clm_mem);
	clm_mem = need ? ct_copy(sec, len, need, avail) : NULL;
	memset(&clm, 0, sizeof(clm));
	if (clm_mem) {
		clm.sec = clm_mem;
		clm.lut = clm_mem + len;
		clm.lutbytes = need;
		/* tisp_clm_init: ct 5000, zone 0xff, threshold 1, then the forced interp at 5000 K */
		clm.ct = T32_CLM_CT;
		clm.zone = T32_CLMCT_ZONE_INIT;
		(void)t32_clm_ct_interp(&clm, T32_CLMCT_THR, T32_CLM_CT, 1);	/* -1: tables kept, as the vendor */
		clm_on = true;
		ret = 0;
	}
	mutex_unlock(&ctup_lock);
	return ret;
}

void t32_ctup_detach(void)
{
	mutex_lock(&ctup_lock);
	ccm_on = bcsh_on = clm_on = false;
	kfree(ccm_mem);
	kfree(bcsh_mem);
	kfree(clm_mem);
	ccm_mem = bcsh_mem = clm_mem = NULL;
	ev_last[0] = 0;
	mutex_unlock(&ctup_lock);
}

/* event 11 of the vendor AWB: tisp_ct_update(ch 0, ct, force 0, keep 0) */
void t32_ctup_run(u32 ct)
{
	if (!t32_pp_on(T32_PP_CT))
		return;
	mutex_lock(&ctup_lock);
	if (ccm_on || bcsh_on || clm_on || t32_lsc_drv_on()) {
		t32_ct_update(&ct_ops, 0, ct, 0, 0);
		t32_ctup_stats.runs++;
		t32_ctup_stats.ct = ct;
	}
	mutex_unlock(&ctup_lock);
}

/* ---- AE event 6: tisp_long_ev_update ---- */
static u32 ev_rd(u32 ch, u32 reg) { (void)ch; return t32_hw_rd(reg); }

static void ev_block(enum t32_lev_blk blk, u32 ch, u64 ev, u32 x, u32 y)
{
	(void)ch;
	switch (blk) {
	case T32_LEV_AWB:	/* tisp_awb_ev_update: the low word */
		t32_awbc_ev((u32)ev);
		return;
	case T32_LEV_CCM:
		if (ccm_on && t32_pp_on(T32_PP_EV))
			ct_flush(t32_ccm_ev_update(&ccm, (u32)ev, (u32)(ev >> 32), ct_w));
		return;
	case T32_LEV_BCSH:
		if (bcsh_on && t32_pp_on(T32_PP_EV)) {
			t32_bcsh_ev_update(&bcsh, (u32)ev, (u32)(ev >> 32), x, y);
			ct_flush(t32_bcsh_frame_done(&bcsh, ct_w));
		}
		return;
	default:		/* gamma, defog, TMO, WDR: not wired here */
		return;
	}
}

static const struct t32_lev_ops ev_ops = { ev_rd, ev_block };

/* AE event 6 (handler words: EV lo / hi, brightness, 0): tisp_long_ev_update(ch 0, ev, x, y, 0, 0) */
void t32_ctup_ev(u32 lo, u32 hi, u32 x, u32 y)
{
	mutex_lock(&ctup_lock);
	t32_long_ev_update(&ev_ops, t32_data_d03a0, ev_last, ev_pipe, 0, lo | (u64)hi << 32, x, y, 0, 0);
	t32_ctup_stats.ev_runs++;
	mutex_unlock(&ctup_lock);
}
