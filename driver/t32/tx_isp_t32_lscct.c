// SPDX-License-Identifier: GPL-2.0
/*
 * T32 LSC colour temperature path in the driver (openimp #234), behind
 * t32_ported_paths bit 19 (T32_PP_LSCCT) together with bit 11 (T32_PP_LSC,
 * the LSC state) and bit 15 (T32_PP_CT, the fan-out).
 *
 * The vendor's tisp_ct_update hands every AWB colour temperature to
 * tisp_lsc_ct_update (LSC bypass bit clear): tisp_lsc_interp_by_ct, and when
 * the mesh changed tisp_lsc_write_all_reg(ch, 2, 0, 1), which writes the new
 * mesh into its LSC register list (one sensor) and arms it; the LSC frame done
 * interrupt (bit 36, tisp_lsc_irq_frame_done) runs that list through the
 * register list DMA (tisp_single_lsc_rcfg_cfg_trig).  All of it is the ported
 * flow of tx_isp_t32_lscct.h (oracle t32_lscct_oracle); here only the hooks:
 *  - the LSC list: 16 KiB of pages, written uncached (the core reads it by
 *    DMA), headers of tisp_single_lsc_rcfg_reg_init (t32_rcfgv_lsc_reg_init)
 *    and the values tisp_lsc_init leaves (t32_lscct_img_fill) at stream-on;
 *    the trigger is t32_hw_rcfg_lsc_trig (table word 49, bit 17, 0x2c = 2);
 *  - the state: a copy of the LSC block, the info / mesh planes of the
 *    stream-on list (t32_lsc_setup at lsc_ct) and the current values;
 *  - tisp_lsc_tgain_update (tisp_long_tgain_update, tx_isp_t32_tisp.c) runs
 *    on the same state and list (strength 0x5050 and list word 8), so a list
 *    run never brings back an old strength; like the vendor's it drops a
 *    pending list run (write_all_reg step 1);
 *  - a running register list (0x22014 != 0) is waited for (frame done
 *    completion, 100 jiffies) as the vendor does;
 *  - one mutex serialises the entry points (the vendor's mutex sits inside
 *    write_all_reg; its interp_by_ct / interp_by_tgain run outside it);
 *  - the vendor's channel lock (ctx + 16) is taken nested by
 *    tisp_lsc_mesh_itp: here it only masks interrupts (local_irq_save).
 * Direct register mode (t32_data_d03a0 = 0), one sensor.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/gfp.h>
#include <linux/io.h>

#include "tx_isp_t32.h"
#include "tx_isp_t32_rcfgv.h"
#include "tx_isp_t32_lscct.h"

extern u8 t32_data_d03a0[4];

int isp_printf(unsigned int level, const char *fmt, ...);

struct t32_lsc_drv_stats t32_lsc_drv_stats;

static DEFINE_MUTEX(lsc_lock);
static DEFINE_SPINLOCK(lsc_irq_lock);	/* vendor t32_small_bss */
static struct completion lsc_done;	/* vendor t32_data_e17f4 */
static struct t32_lsc_ctx lc;
static struct t32_lsc_cur lcur;
static struct t32_lscct lw;
static bool lsc_on;
static u8 lsc_flips[12];		/* vendor lsc_para[ch] + 0xc: [9] flips, [10] mirrors (kept over streams) */
static u8 lsc_mf;			/* orientation the live tables / ring centre carry (#728; 0 at attach) */
static unsigned long img_pages;		/* the LSC list (T32_LSCCT_IMG_WORDS words) */
static u32 img_pa;
#define LSC_IMG_ORDER	get_order(T32_LSCCT_IMG_WORDS * 4)

static u32 h_rd(void *p, u32 reg) { (void)p; return t32_hw_rd(reg); }

/* tisp_reg_write / _reg_lut_write in the direct mode: system_reg_write (the LUT offset is not used) */
static void h_wr(void *p, u8 ch, u32 reg, u32 val)
{
	(void)p; (void)ch;
	if (t32_data_d03a0[0] != 1)
		t32_hw_wr(reg, val);
}

static void h_lut_wr(void *p, u8 ch, u32 reg, u32 val, u32 off)
{
	(void)off;
	h_wr(p, ch, reg, val);
}

static u32 h_sensors(void *p) { (void)p; return 1; }
static void h_nop(void *p) { (void)p; }

static unsigned long h_lock(void *p)
{
	unsigned long f;

	(void)p;
	spin_lock_irqsave(&lsc_irq_lock, f);
	return f;
}

static void h_unlock(void *p, unsigned long f) { (void)p; spin_unlock_irqrestore(&lsc_irq_lock, f); }

static unsigned long h_ch_lock(void *p, u8 ch)
{
	unsigned long f;

	(void)p; (void)ch;
	local_irq_save(f);
	return f;
}

static void h_ch_unlock(void *p, u8 ch, unsigned long f) { (void)p; (void)ch; local_irq_restore(f); }
static void h_comp_init(void *p) { (void)p; init_completion(&lsc_done); }

static long h_comp_wait(void *p, u32 t)
{
	long r;

	(void)p;
	t32_lsc_drv_stats.waits++;
	r = (long)wait_for_completion_timeout(&lsc_done, t);
	if (!r)
		t32_lsc_drv_stats.timeouts++;
	return r;
}

static void h_complete(void *p) { (void)p; complete(&lsc_done); }
static void h_print(void *p) { (void)p; isp_printf(0, "tx-isp-t32: LSC waits for the running register list\n"); }

static void h_trig(void *p, u8 ch)
{
	(void)p; (void)ch;
	t32_hw_rcfg_lsc_trig(img_pa);
	t32_lsc_drv_stats.list_runs++;
}

static const struct t32_lscct_ops lsc_ops = {
	.reg_read = h_rd, .reg_write = h_wr, .reg_lut_write = h_lut_wr, .sensor_num = h_sensors,
	.mutex_lock = h_nop, .mutex_unlock = h_nop, .lock = h_lock, .unlock = h_unlock,
	.ch_lock = h_ch_lock, .ch_unlock = h_ch_unlock, .comp_init = h_comp_init,
	.comp_wait = h_comp_wait, .complete = h_complete, .print_wait = h_print, .cfg_trig = h_trig,
};

bool t32_lsc_drv_on(void)
{
	return READ_ONCE(lsc_on) && t32_pp_on(T32_PP_LSCCT);
}

void t32_lsc_drv_detach(void)
{
	unsigned long f;

	spin_lock_irqsave(&lsc_irq_lock, f);
	lsc_on = false;
	lw.armed = 0;
	spin_unlock_irqrestore(&lsc_irq_lock, f);
	mutex_lock(&lsc_lock);
	vfree((void *)lc.sec);
	lc.sec = NULL;
	mutex_unlock(&lsc_lock);
}

/*
 * Stream-on, after the stream-on list was built from blk (t32_lsc_setup at ct): the
 * state and the LSC list as the vendor's tisp_lsc_init leaves them.
 */
void t32_lsc_drv_attach(const u8 *blk, const struct t32_lsc *s, const struct t32_lsc_work *wk,
			u32 width, u32 height, u32 bayer, u32 ct)
{
	unsigned long f;
	u32 *img;
	u8 *cp;

	t32_lsc_drv_detach();
	if (!t32_pp_on(T32_PP_LSC) || !t32_pp_on(T32_PP_CT) || !t32_pp_on(T32_PP_LSCCT))
		return;
	if (!img_pages) {
		img_pages = t32_dma_zpages(LSC_IMG_ORDER);	/* written through KSEG1 below (openimp #530) */
		if (!img_pages) {
			isp_printf(2, "tx-isp-t32: no memory for the LSC list, LSC colour temperature path off\n");
			return;
		}
		img_pa = virt_to_phys((void *)img_pages);
	}
	cp = vmalloc(T32_LSC_BLOCK_BYTES);
	if (!cp)
		return;
	memcpy(cp, blk, T32_LSC_BLOCK_BYTES);
	img = (u32 *)CKSEG1ADDR(img_pa);
	mutex_lock(&lsc_lock);
	memset(&lc, 0, sizeof(lc));
	lc.sec = cp;
	lsc_mf = 0;			/* a fresh copy of the bin's tables (#728) */
	lc.s = *s;
	lc.wk = *wk;
	t32_lsc_cur_init(&lcur, width, height, bayer, ct);
	lc.cur = &lcur;
	lc.img = img;
	lc.width = width;
	lc.height = height;
	lc.bayer = bayer;
	lc.direct = t32_data_d03a0[0];
	memset(img, 0, T32_LSCCT_IMG_WORDS * 4);
	t32_rcfgv_lsc_reg_init(img);
	t32_lscct_img_fill(&lc, 0);
	memset(&lw, 0, sizeof(lw));
	lw.lsc = &lc;
	lw.ops = &lsc_ops;
	lw.shadow = t32_data_d03a0;
	init_completion(&lsc_done);
	spin_lock_irqsave(&lsc_irq_lock, f);
	lsc_on = true;
	spin_unlock_irqrestore(&lsc_irq_lock, f);
	mutex_unlock(&lsc_lock);
	t32_lsc_drv_stats.attaches++;
}

/* tisp_lsc_ct_update (tisp_ct_update, tx_isp_t32_ctup.c) */
void t32_lsc_drv_ct(u32 ch, u32 ct)
{
	(void)ch;
	if (!t32_lsc_drv_on())
		return;
	mutex_lock(&lsc_lock);
	if (lsc_on && lc.sec) {
		u32 before = lcur.ct;

		t32_lscct_ct_update(&lw, 0, ct);
		t32_lsc_drv_stats.ct_calls++;
		if (lcur.ct != before)
			t32_lsc_drv_stats.mesh_writes++;
		t32_lsc_drv_stats.ct = lcur.ct;
	}
	mutex_unlock(&lsc_lock);
}

/* tisp_lsc_tgain_update (tisp_long_tgain_update, tx_isp_t32_tisp.c): true when it ran here */
bool t32_lsc_drv_tgain(u32 ch, u32 gain)
{
	bool done = false;

	(void)ch;
	if (!t32_lsc_drv_on())
		return false;
	mutex_lock(&lsc_lock);
	if (lsc_on && lc.sec) {
		t32_lscct_tgain_update(&lw, 0, gain);
		t32_lsc_drv_stats.tgains++;
		done = true;
	}
	mutex_unlock(&lsc_lock);
	return done;
}

/*
 * tisp_lsc_hvflip (vendor 0xc258c, tuning id 0x73 after the sensor mirror / flip, openimp #722) ->
 * tisp_lsc_mirror_flip (0x55b88) on the live LSC state: under the channel lock (lsc_para[ch] + 0x10)
 * flip == 1 exchanges the mesh lines, mirror == 1 the elements of every line, of every table and plane
 * (t32_lsc_mesh_flip, the exchange of t32_lsc_mirror_flip, oracle t32_lscm_oracle; the tables are the
 * section copy's at + T32_LSC_TABLES = vendor stLutInfo[ch] + 0x14 + 2, the grid the rows / columns of
 * tisp_lsc_calc_para_refresh, the table count section byte 0x74); then cur + 0xc = 5,
 * tisp_lsc_interp_by_ct(ch, ct, 1) and tisp_lsc_write_all_reg(ch, 2, 0, 1): the flipped mesh goes out
 * through the LSC list at the next LSC frame done; the vendor's printk of the two counters.  Always 0.
 * Beyond vendor (openimp #728): only the change against the orientation the tables carry is exchanged
 * (t32_lsc_mf_delta: the vendor's toggles add up), and with ring-format bins (section byte 65) the optical
 * centre is mirrored / flipped too (t32_lsc_ring_centre, the geometry rebuilt) and all registers are written
 * (mode 0; the vendor's mode 2 leaves the centre register 0x5054 / r5 untouched).
 */
int t32_lsc_drv_hvflip(u32 ch, u32 flip, u32 mirror)
{
	unsigned long f;

	(void)ch;
	if (!t32_lsc_drv_on())
		return 0;
	mutex_lock(&lsc_lock);
	if (lsc_on && lc.sec) {
		u8 *tab = (u8 *)lc.sec + T32_LSC_TABLES + 2;
		u32 ntab = lc.sec[116], df, dm, ring = lc.sec[65];

		t32_lsc_mf_delta(&lsc_mf, flip, mirror, &df, &dm);
		if (df) {
			f = h_ch_lock(NULL, 0);
			t32_lsc_mesh_flip(tab, lc.s.rows, lc.s.cols, ntab, 1, 0, lsc_flips, &t32_lsc_xops_default);
			h_ch_unlock(NULL, 0, f);
		}
		if (dm) {
			f = h_ch_lock(NULL, 0);
			t32_lsc_mesh_flip(tab, lc.s.rows, lc.s.cols, ntab, 0, 1, lsc_flips, &t32_lsc_xops_default);
			h_ch_unlock(NULL, 0, f);
		}
		if (ring && (df || dm)) {
			t32_lsc_ring_centre((u8 *)lc.sec, lc.width, lc.height, df, dm);
			t32_lsc_geometry(lc.sec, &lc.s, lc.width, lc.height);
		}
		lcur.rsvd1[0] = 5;
		t32_lscct_interp_by_ct(&lw, 0, lcur.ct, 1);
		t32_lscct_write_all_reg(&lw, 0, ring ? 0 : 2, 0, 1);
		t32_lsc_drv_stats.flips = lsc_flips[9];
		t32_lsc_drv_stats.mf = lsc_mf;
		t32_lsc_drv_stats.mirrors = lsc_flips[10];
		printk("\n  [func is %s, line is %d] lsc filp times is %d, mirror times is %d\n",
		       "tisp_lsc_mirror_flip", 0x7f9, lsc_flips[9], lsc_flips[10]);
	}
	mutex_unlock(&lsc_lock);
	return 0;
}

/* tisp_lsc_irq_frame_done, from the ISP interrupt (status 0x300a8 bit 4 = interrupt bit 36) */
void t32_lsc_drv_frame_done(void)
{
	if (!READ_ONCE(lsc_on))
		return;
	t32_lsc_drv_stats.frames++;
	t32_lscct_frame_done(&lw, 0);
}

void t32_lsc_drv_exit(void)
{
	t32_lsc_drv_detach();
	if (img_pages)
		free_pages(img_pages, LSC_IMG_ORDER);
	img_pages = 0;
}
