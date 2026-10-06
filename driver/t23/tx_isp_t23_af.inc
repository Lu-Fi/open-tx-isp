/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T23 auto-focus statistics chain of the stock tx-isp-t23.ko, on the shared
 * data handling of tx_isp_t23_af.h.  On by default (source_af=1), as in
 * the stock module, whose tisp_init always runs tiziano_af_init and
 * registers the AF interrupt (core status bit 31).  source_af=0 leaves the
 * AF block off and the AF controls unrouted.  Device cost on the Jooan A6M
 * (T23N, sc1a4t 720p15, raptor): no measurable CPU or interrupt-rate change.
 *
 * One spinlock (irqsave) covers the parameters, the attribute and the
 * results: the AF interrupt reads them, the tuning calls and the day/night
 * refresh write them.  Stock locks only the zone-value copy.
 */

#include "tx_isp_t23_af.h"

module_param_named(source_af, regtrace_t23_source_af, bool, 0444);
MODULE_PARM_DESC(source_af,
		 "T23 AF statistics chain of the stock module: AF block, interrupt and the GetAfHist/SetAfHist/GetAfWeight/SetAfWeight/GetAFMetrices/GetAfZone controls (default 1 = stock, 0 off)");

static struct t31_af_params t23_af;
static struct t31_af_stats t23_af_st;
static struct t31_af_result t23_af_res;
static uint32_t t23_af_fv_last[T31_AF_ZONES];      /* stock fv_value_last */
static uint8_t t23_af_attr[T31_AF_ATTR_BYTES];     /* stock af_attr */
static uint8_t t23_af_enable;                      /* stock AF_Enable */
static uint8_t t23_af_set_trig;                    /* stock af_set_trig */
static int t23_af_first;                           /* stock af_first */
static uint32_t t23_af_width, t23_af_height;       /* IspAfStaticParam+48/52 */
static bool t23_af_ready;                          /* init done, core running */
static DEFINE_SPINLOCK(t23_af_lock);

/* tiziano_af_set_hardware_param with t23_af_lock held */
static void t23_af_hw_write_locked(void)
{
	uint32_t regs[T31_AF_REGS_MAX][2];
	unsigned int n, i;

	t31_af_zone_layout(&t23_af, t23_af_width, t23_af_height);
	n = t31_af_hw_regs(&t23_af, !t23_af_first, regs);
	t23_af_first = 1;
	for (i = 0; i < n; i++) {
		if (regs[i][0] == 0xb828U)
			system_reg_write(0xb800U, 1U); /* system_reg_write_af */
		system_reg_write(regs[i][0], regs[i][1]);
	}
}

/*
 * tiziano_af_params_refresh: the 19 arrays from the active IQ bank
 * (tparams + 0x27c74, 608 + 900 bytes).
 */
static int t23_af_params_refresh(void)
{
	unsigned long flags;

	BUILD_BUG_ON(T23_AF_ACTIVE_OFFSET + sizeof(struct t31_af_params) >
		     T23_TPARAMS_ACTIVE_OFFSET + T23_TPARAMS_BANK_SIZE);
	if (!tparams_day)
		return -ENODATA;
	spin_lock_irqsave(&t23_af_lock, flags);
	memcpy(&t23_af, tparams + T23_AF_ACTIVE_OFFSET, sizeof(t23_af));
	spin_unlock_irqrestore(&t23_af_lock, flags);
	return 0;
}

/*
 * tiziano_af_init(height, width): parameters, hardware, a fresh attribute.
 * A bank without an AF grid (zero rows or columns) leaves AF off.
 * Process context at core start (after the statistics ring is programmed).
 */
static int t23_af_start(uint32_t width, uint32_t height)
{
	unsigned long flags;

	if (!regtrace_t23_source_af)
		return 0;
	t23_af_first = 0;
	if (t23_af_params_refresh())
		return 0;
	if (!t31_af_rows(&t23_af) || !t31_af_cols(&t23_af) ||
	    !regtrace_t23_core_dma_bufs[5].virt) {
		printk(KERN_WARNING "tx_isp_t23_recovered: source AF off (no AF grid in the IQ bank or no ring)\n");
		return 0;
	}
	spin_lock_irqsave(&t23_af_lock, flags);
	t23_af_width = width;
	t23_af_height = height;
	t23_af_hw_write_locked();
	t31_af_attr_init(&t23_af, t23_af_attr);
	t23_af_set_trig = 0;
	t23_af_enable = 0;
	memset(&t23_af_st, 0, sizeof(t23_af_st));
	memset(&t23_af_res, 0, sizeof(t23_af_res));
	memset(t23_af_fv_last, 0, sizeof(t23_af_fv_last));
	t23_af_ready = true;
	spin_unlock_irqrestore(&t23_af_lock, flags);
	printk(KERN_WARNING "tx_isp_t23_recovered: source AF statistics (stock chain) active %ux%u grid %ux%u\n",
	       width, height, t31_af_cols(&t23_af), t31_af_rows(&t23_af));
	return 0;
}

/* core stop (any context): no further interrupt work */
static void t23_af_halt(void)
{
	unsigned long flags;

	spin_lock_irqsave(&t23_af_lock, flags);
	t23_af_ready = false;
	spin_unlock_irqrestore(&t23_af_lock, flags);
}

/*
 * af_interrupt_static: core status bit 31.  The statistics page the AF
 * block wrote last (0xb8bc, 4 KiB pages of ring 5), unpacked for the
 * configured grid, then Tiziano_af_fpga.  The page index is checked
 * against the 4-page ring (stock does not).
 */
static void t23_af_irq(uint32_t status)
{
	uint32_t bank;
	void *buf;
	unsigned long flags;

	if (!(status & BIT(T23_AF_IRQ_BIT)))
		return;
	spin_lock_irqsave(&t23_af_lock, flags);
	if (!t23_af_ready || !regtrace_t23_core_dma_bufs[5].virt)
		goto out;
	bank = system_reg_read(T23_AF_STAT_BANK_REG);
	if (!t23_af_bank_ok(bank))
		goto out;
	buf = (u8 *)regtrace_t23_core_dma_bufs[5].virt +
	      bank * T23_AF_STAT_PAGE_BYTES;
	private_dma_cache_sync(NULL, buf, T23_AF_STAT_PAGE_BYTES, DMA_FROM_DEVICE);
	t31_af_unpack(buf, t31_af_rows(&t23_af), t31_af_cols(&t23_af), &t23_af_st);
	t31_af_fv_run(&t23_af, &t23_af_st, &t23_af_res);
	memcpy(t23_af_fv_last, t23_af_res.fv_value, sizeof(t23_af_fv_last));
out:
	spin_unlock_irqrestore(&t23_af_lock, flags);
}

/*
 * tiziano_af_dn_params_refresh: reload the bank, keep a user attribute
 * (stock applies the attribute and writes the registers twice, the second
 * time without the grid; the register state is the same).
 */
static void t23_af_dn_refresh(void)
{
	unsigned long flags;

	if (!regtrace_t23_source_af || !t23_af_ready)
		return;
	t23_af_first = 0;
	if (t23_af_params_refresh())
		return;
	spin_lock_irqsave(&t23_af_lock, flags);
	if (t23_af_set_trig)
		t23_af_enable = t31_af_attr_apply(t23_af_attr, &t23_af);
	t23_af_hw_write_locked();
	spin_unlock_irqrestore(&t23_af_lock, flags);
}

/* SetAfHist: apical_isp_af_hist_s_attr + tisp_af_set_attr (+ _refresh) */
static long t23_af_hist_set(uint32_t uptr)
{
	uint8_t u[T31_AF_ATTR_BYTES], a[T31_AF_ATTR_BYTES];
	unsigned long flags;
	long ret;

	ret = regtrace_t23_copy_in(u, uptr, sizeof(u));
	if (ret)
		return ret;
	if (t31_af_hist_from_user(u, a))
		return -EINVAL;
	spin_lock_irqsave(&t23_af_lock, flags);
	if (!t23_af_ready) {		/* registers only with the core running */
		spin_unlock_irqrestore(&t23_af_lock, flags);
		return -ENODEV;
	}
	t23_af_first = 0;
	memcpy(t23_af_attr, a, sizeof(a));
	t23_af_enable = t31_af_attr_apply(t23_af_attr, &t23_af);
	t23_af_hw_write_locked();
	t23_af_set_trig = 1;
	spin_unlock_irqrestore(&t23_af_lock, flags);
	return 0;
}

/* GetAfHist: tisp_af_get_attr + apical_isp_af_hist_g_attr */
static long t23_af_hist_get(uint32_t uptr)
{
	uint8_t g[T31_AF_ATTR_BYTES];
	unsigned long flags;

	spin_lock_irqsave(&t23_af_lock, flags);
	t31_af_attr_read(&t23_af, t23_af_res.fv_alt, t23_af_enable,
			 t23_af_attr[17], t23_af_st.frame_num, g);
	spin_unlock_irqrestore(&t23_af_lock, flags);
	return regtrace_t23_copy_out(uptr, g, sizeof(g));
}

/* GetAFMetrices (tisp_af_get_metric): fv[2] >> attribute byte 17, 4 bytes */
static long t23_af_metric_get(uint32_t uptr)
{
	uint32_t v;
	unsigned long flags;

	spin_lock_irqsave(&t23_af_lock, flags);
	v = t23_af.fv[2] >> (t23_af_attr[17] & 31U);
	spin_unlock_irqrestore(&t23_af_lock, flags);
	return regtrace_t23_copy_out(uptr, &v, sizeof(v));
}

/*
 * SetAfWeight: apical_isp_af_weight_s_attr (225 bytes, each 0..8) and
 * tisp_s_af_weight: the active and the day and night bank, then the
 * runtime table (stock tisp_api_af_weight_set; no register write).
 */
static long t23_af_weight_set(uint32_t uptr)
{
	uint8_t in[T31_AF_ZONES];
	uint32_t *w;
	unsigned long flags;
	long ret;

	ret = regtrace_t23_copy_in(in, uptr, sizeof(in));
	if (ret)
		return ret;
	w = kmalloc(T31_AF_ZONES * sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;
	if (t31_af_weight_from_user(in, w)) {
		kfree(w);
		return -EINVAL;
	}
	t23x_tparams_put(T23_AF_ACTIVE_WEIGHT_OFFSET, w, T31_AF_ZONES * sizeof(*w));
	spin_lock_irqsave(&t23_af_lock, flags);
	memcpy(t23_af.weight, w, sizeof(t23_af.weight));
	spin_unlock_irqrestore(&t23_af_lock, flags);
	kfree(w);
	return 0;
}

/* GetAfWeight: the low byte of each runtime word (225 bytes) */
static long t23_af_weight_get(uint32_t uptr)
{
	uint8_t out[T31_AF_ZONES];
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&t23_af_lock, flags);
	for (i = 0; i < T31_AF_ZONES; i++)
		out[i] = (uint8_t)t23_af.weight[i];
	spin_unlock_irqrestore(&t23_af_lock, flags);
	return regtrace_t23_copy_out(uptr, out, sizeof(out));
}

/* GetAfZone (tisp_af_get_zone): fv_value_last, 225 words */
static long t23_af_zone_get(uint32_t uptr)
{
	uint32_t out[T31_AF_ZONES];
	unsigned long flags;

	spin_lock_irqsave(&t23_af_lock, flags);
	memcpy(out, t23_af_fv_last, sizeof(out));
	spin_unlock_irqrestore(&t23_af_lock, flags);
	return regtrace_t23_copy_out(uptr, out, sizeof(out));
}

/*
 * tisp_af_param_array_get/_set: the 19 arrays as one 1508-byte block in
 * stock order (the tuning-bin layout).  Set stores them and runs
 * tiziano_af_set_hardware_param (only with the core running).
 */
static int t23_af_param_array_get(void *out, size_t size)
{
	unsigned long flags;

	if (size < sizeof(t23_af))
		return -EINVAL;
	spin_lock_irqsave(&t23_af_lock, flags);
	memcpy(out, &t23_af, sizeof(t23_af));
	spin_unlock_irqrestore(&t23_af_lock, flags);
	return 0;
}

static int t23_af_param_array_set(const void *in, size_t size)
{
	unsigned long flags;

	if (size < sizeof(t23_af))
		return -EINVAL;
	spin_lock_irqsave(&t23_af_lock, flags);
	memcpy(&t23_af, in, sizeof(t23_af));
	if (t23_af_ready)
		t23_af_hw_write_locked();
	spin_unlock_irqrestore(&t23_af_lock, flags);
	return 0;
}
