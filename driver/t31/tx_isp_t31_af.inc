/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 auto-focus statistics chain, stock libt31-firmware-1.1.6:
 * tiziano_af_init / _params_refresh / _dn_params_refresh,
 * tiziano_af_set_hardware_param, af_interrupt_static, Tiziano_af_fpga,
 * tisp_af_set_attr(_refresh), tisp_af_get_attr/_metric/_zone and the AF
 * weight.  Data handling in tx_isp_t31_af.h (host-tested).
 *
 * One spinlock (irqsave) covers the parameters, the attribute and the
 * results: the AF interrupt reads them, the tuning calls and the
 * day/night refresh write them.  Stock locks only the zone-value copy.
 */

static struct t31_af_params t31_af;
static struct t31_af_stats t31_af_st;
static struct t31_af_result t31_af_res;
static uint32_t t31_af_fv_last[T31_AF_ZONES];      /* stock fv_value_last */
static uint8_t t31_af_attr[T31_AF_ATTR_BYTES];     /* stock af_attr */
static uint8_t t31_af_enable;                      /* stock AF_Enable */
static uint8_t t31_af_set_trig;                    /* stock af_set_trig */
static int t31_af_first;                           /* stock af_first */
static uint32_t t31_af_width, t31_af_height;       /* IspAfStaticParam+48/52 */
static DEFINE_SPINLOCK(t31_af_lock);

struct af_param_entry { int id; void *buf; int size; };
static const struct af_param_entry af_params[] = {
	{ 0x3ad, t31_af.zone,      sizeof(t31_af.zone) },
	{ 0x3ae, t31_af.thres,     sizeof(t31_af.thres) },
	{ 0x3af, t31_af.fir0_v,    sizeof(t31_af.fir0_v) },
	{ 0x3b0, t31_af.fir0_ldg,  sizeof(t31_af.fir0_ldg) },
	{ 0x3b1, t31_af.fir0_cor,  sizeof(t31_af.fir0_cor) },
	{ 0x3b2, t31_af.fir1_v,    sizeof(t31_af.fir1_v) },
	{ 0x3b3, t31_af.fir1_ldg,  sizeof(t31_af.fir1_ldg) },
	{ 0x3b4, t31_af.fir1_cor,  sizeof(t31_af.fir1_cor) },
	{ 0x3b5, t31_af.iir0_h,    sizeof(t31_af.iir0_h) },
	{ 0x3b6, t31_af.iir0_ldg,  sizeof(t31_af.iir0_ldg) },
	{ 0x3b7, t31_af.iir0_cor,  sizeof(t31_af.iir0_cor) },
	{ 0x3b8, t31_af.iir1_h,    sizeof(t31_af.iir1_h) },
	{ 0x3b9, t31_af.iir1_ldg,  sizeof(t31_af.iir1_ldg) },
	{ 0x3ba, t31_af.iir1_cor,  sizeof(t31_af.iir1_cor) },
	{ 0x3bb, t31_af.point_pos, sizeof(t31_af.point_pos) },
	{ 0x3bc, t31_af.tilt,      sizeof(t31_af.tilt) },
	{ 0x3bd, t31_af.fv_wmean,  sizeof(t31_af.fv_wmean) },
	{ 0x3be, t31_af.fv,        sizeof(t31_af.fv) },
	{ 0x3bf, t31_af.weight,    sizeof(t31_af.weight) },
};

/* tiziano_af_set_hardware_param with t31_af_lock held */
static void t31_af_hw_write_locked(void)
{
	uint32_t regs[T31_AF_REGS_MAX][2];
	unsigned int n, i;

	t31_af_zone_layout(&t31_af, t31_af_width, t31_af_height);
	n = t31_af_hw_regs(&t31_af, !t31_af_first, regs);
	t31_af_first = 1;
	for (i = 0; i < n; i++) {
		if (regs[i][0] == 0xb828U)
			system_reg_write(0xb800, 1); /* system_reg_write_af(1, ..) */
		system_reg_write(regs[i][0], regs[i][1]);
	}
}

void tiziano_af_set_hardware_param(void)
{
	unsigned long flags;

	spin_lock_irqsave(&t31_af_lock, flags);
	t31_af_hw_write_locked();
	spin_unlock_irqrestore(&t31_af_lock, flags);
}

/* tiziano_af_params_refresh: the 19 arrays from the IQ bank in use */
static int tiziano_af_params_refresh(void)
{
	const u8 *bank = (const u8 *)(tparams_active ? tparams_active : tparams_day);
	unsigned long flags;

	if (!bank || !tuning_bin_loaded)
		return -ENODATA;
	BUILD_BUG_ON(T31_AF_BANK_OFFSET + sizeof(struct t31_af_params) >
		     TISP_PARAM_BLOCK_SIZE);
	spin_lock_irqsave(&t31_af_lock, flags);
	memcpy(&t31_af, bank + T31_AF_BANK_OFFSET, sizeof(t31_af));
	spin_unlock_irqrestore(&t31_af_lock, flags);
	return 0;
}

/* tisp_af_set_attr_refresh with t31_af_lock held */
static void t31_af_attr_refresh_locked(void)
{
	t31_af_enable = t31_af_attr_apply(t31_af_attr, &t31_af);
	t31_af_hw_write_locked();
}

int tisp_af_set_attr_refresh(void)
{
	unsigned long flags;

	spin_lock_irqsave(&t31_af_lock, flags);
	t31_af_attr_refresh_locked();
	spin_unlock_irqrestore(&t31_af_lock, flags);
	return 0;
}

/* tiziano_af_dn_params_refresh: reload the bank, keep a user attribute */
void tiziano_af_dn_params_refresh(void)
{
	unsigned long flags;

	t31_af_first = 0;
	if (tiziano_af_params_refresh())
		return;
	spin_lock_irqsave(&t31_af_lock, flags);
	if (t31_af_set_trig)
		t31_af_enable = t31_af_attr_apply(t31_af_attr, &t31_af);
	t31_af_hw_write_locked();
	spin_unlock_irqrestore(&t31_af_lock, flags);
}

/*
 * af_interrupt_static: the statistics bank the AF block wrote last (0xb8bc,
 * 4 KiB pages), unpacked for the configured grid, then Tiziano_af_fpga.
 */
int af_interrupt_static(void)
{
	uint32_t bank;
	void *buf;
	unsigned long flags;

	if (data_a2f80 == 0)
		return 0;
	bank = system_reg_read(0xb8bc);
	if (data_a2f7c && bank >= data_a2f7c)
		return 1;	/* not a page of the AF buffer */
	buf = (void *)(unsigned long)(data_a2f80 + (bank << 12));
	private_dma_cache_sync(NULL, buf, 0x1000, 0);
	spin_lock_irqsave(&t31_af_lock, flags);
	t31_af_unpack(buf, t31_af_rows(&t31_af), t31_af_cols(&t31_af),
		      &t31_af_st);
	t31_af_fv_run(&t31_af, &t31_af_st, &t31_af_res);
	memcpy(t31_af_fv_last, t31_af_res.fv_value, sizeof(t31_af_fv_last));
	spin_unlock_irqrestore(&t31_af_lock, flags);
	return 1;
}

static irqreturn_t af_interrupt_static_wrapper(int irq, void *dev_id)
{
	(void)irq;
	(void)dev_id;
	return af_interrupt_static() ? IRQ_HANDLED : IRQ_NONE;
}

/* tiziano_af_init(height, width) */
int tiziano_af_init(uint32_t height, uint32_t width)
{
	unsigned long flags;

	t31_af_first = 0;
	tiziano_af_params_refresh();
	spin_lock_irqsave(&t31_af_lock, flags);
	t31_af_width = width;
	t31_af_height = height;
	t31_af_hw_write_locked();
	t31_af_attr_init(&t31_af, t31_af_attr);
	t31_af_set_trig = 0;
	spin_unlock_irqrestore(&t31_af_lock, flags);
	system_irq_func_set(0x1f, af_interrupt_static_wrapper);
	return 0;
}

int tisp_af_param_array_get(int param_id, void *out_buf, int *size_buf)
{
	unsigned long flags;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(af_params); i++) {
		if (af_params[i].id != param_id)
			continue;
		if (out_buf) {
			spin_lock_irqsave(&t31_af_lock, flags);
			memcpy(out_buf, af_params[i].buf, af_params[i].size);
			spin_unlock_irqrestore(&t31_af_lock, flags);
		}
		if (size_buf)
			*size_buf = af_params[i].size;
		return 0;
	}
	pr_err("tisp_af_param_array_get: unsupported param id 0x%x\n", param_id);
	if (size_buf)
		*size_buf = 0;
	return -1;
}

/* stock: copy into the parameter, then tiziano_af_set_hardware_param */
int tisp_af_param_array_set(int param_id, void *in_buf, int *size_buf)
{
	unsigned long flags;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(af_params); i++) {
		if (af_params[i].id != param_id)
			continue;
		spin_lock_irqsave(&t31_af_lock, flags);
		memcpy(af_params[i].buf, in_buf, af_params[i].size);
		t31_af_hw_write_locked();
		spin_unlock_irqrestore(&t31_af_lock, flags);
		if (size_buf)
			*size_buf = af_params[i].size;
		return 0;
	}
	pr_err("tisp_af_param_array_set: unsupported param id 0x%x\n", param_id);
	return -1;
}

/* SetAfHist (apical_isp_af_hist_s_attr + tisp_af_set_attr) */
static int t31_af_hist_set(const uint8_t *user_attr)
{
	uint8_t a[T31_AF_ATTR_BYTES];
	unsigned long flags;

	if (t31_af_hist_from_user(user_attr, a))
		return -EINVAL;
	spin_lock_irqsave(&t31_af_lock, flags);
	t31_af_first = 0;
	memcpy(t31_af_attr, a, sizeof(a));
	t31_af_attr_refresh_locked();
	t31_af_set_trig = 1;
	spin_unlock_irqrestore(&t31_af_lock, flags);
	return 0;
}

/* GetAfHist (tisp_af_get_attr + apical_isp_af_hist_g_attr) */
static void t31_af_hist_get(uint8_t *g)
{
	unsigned long flags;

	spin_lock_irqsave(&t31_af_lock, flags);
	t31_af_attr_read(&t31_af, t31_af_res.fv_alt, t31_af_enable,
			 t31_af_attr[17], t31_af_st.frame_num, g);
	spin_unlock_irqrestore(&t31_af_lock, flags);
}

/* GetAFMetrices (tisp_af_get_metric): fv[2] >> attribute byte 17 */
int tisp_af_get_metric(uint32_t *v)
{
	unsigned long flags;

	if (!v)
		return -EINVAL;
	spin_lock_irqsave(&t31_af_lock, flags);
	*v = t31_af.fv[2] >> (t31_af_attr[17] & 31U);
	spin_unlock_irqrestore(&t31_af_lock, flags);
	return 0;
}

/* SetAfWeight (apical_isp_af_weight_s_attr + tisp_s_af_weight) */
static int t31_af_weight_set(const uint8_t *u)
{
	uint32_t *w;
	void *blocks[3] = { tparams_active, tparams_day, tparams_night };
	unsigned long flags;
	unsigned int i;

	w = kmalloc(T31_AF_ZONES * sizeof(*w), GFP_KERNEL);
	if (!w)
		return -ENOMEM;
	if (t31_af_weight_from_user(u, w)) {
		kfree(w);
		return -EINVAL;
	}
	for (i = 0; i < ARRAY_SIZE(blocks); i++)
		if (blocks[i])
			memcpy((u8 *)blocks[i] + T31_AF_BANK_WEIGHT_OFFSET, w,
			       T31_AF_ZONES * sizeof(*w));
	spin_lock_irqsave(&t31_af_lock, flags);
	memcpy(t31_af.weight, w, sizeof(t31_af.weight));
	t31_af_hw_write_locked();
	spin_unlock_irqrestore(&t31_af_lock, flags);
	kfree(w);
	return 0;
}

/* GetAfWeight: the low byte of each word */
static void t31_af_weight_get(uint8_t *out)
{
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&t31_af_lock, flags);
	for (i = 0; i < T31_AF_ZONES; i++)
		out[i] = (uint8_t)t31_af.weight[i];
	spin_unlock_irqrestore(&t31_af_lock, flags);
}

/* GetAfZone (tisp_af_get_zone): the focus value of each zone, last frame */
static void t31_af_zone_get(uint32_t *out)
{
	unsigned long flags;

	spin_lock_irqsave(&t31_af_lock, flags);
	memcpy(out, t31_af_fv_last, sizeof(t31_af_fv_last));
	spin_unlock_irqrestore(&t31_af_lock, flags);
}
