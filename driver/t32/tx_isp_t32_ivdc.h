/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IVDC (the ISP's video direct connection to the encoder) core and module
 * power-management glue from the vendor module (Matt Davis' recovered C,
 * PR #15): ivdc_core_ops_init (0xe7f4), ivdc_prepare_module (0xd7ac),
 * ivdc_resume_module (0xe64c), ivdc_resume_early_module (0xe9c4),
 * ivdc_suspend_module (0xea84), ivdc_slake_module (0xed18),
 * ivdc_isp_info_clear (0xfc5c), ivdc_enable_irq (0xeff8), ivdc_disable_irq
 * (0xf098), ivdc_core_ops_ioctl (0xe6bc) and the empty callbacks ivdc_link_setup, ivdc_misc_open,
 * ivdc_misc_release, ivdc_misc_read and ivdc_video_s_stream (return 0).
 *
 * Objects (vendor offsets): the IVDC subdev (struct tx_isp_subdev: clocks
 * +0x100, clock count +0x104, irq device +0x9c, irq enabled +0xa8, irq
 * enable / disable callbacks +0xb8 / +0xbc, private data +0x118) and its
 * private data (register base +0x11c, state +0x1bc, spinlock +0x1c8).
 * Globals: the IVDC mode (t32_data_d9014, 0 = off, 1..4), the mode the core
 * was started with (d9894), the private data used by isp_info_clear (d9890),
 * the register save buffer of prepare (d98b0, 1708 bytes), d98b4 (cleared by
 * slake), four words d98e8..d98f4 that suspend copies to d98a0..d98ac and
 * three flags cf2dc..cf2e4 (set to 1 by slake).
 *
 * The OS calls (spinlock, clocks, delays) and the CPM clock-gate register
 * (0xb00000c4, bit 19 pulsed around a 1 ms sleep when the mode changes) are
 * hooks of struct t32_ivdc_os.  isp_printf keeps the vendor's level and line
 * argument (the vendor source line) so the oracle can match the call sites.
 *
 * Vendor quirks kept: core_ops_init polls the busy bit 0 of register 0 with
 * 200 x udelay(1000) per poll and no limit; with the mode 0 it writes 1 to
 * register 0x5c and returns without changing the state; the printf of an
 * invalid subdev in core_ops_init passes no line.  The return values of
 * enable_irq / disable_irq are leftover registers in the vendor code.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_ivdc_oracle).
 */
#ifndef TX_ISP_T32_IVDC_H
#define TX_ISP_T32_IVDC_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_IVDC_SD_CLKS	0x100U
#define T32_IVDC_SD_CLK_NUM	0x104U
#define T32_IVDC_SD_IRQDEV	0x09cU
#define T32_IVDC_SD_IRQ_ON	0x0a8U
#define T32_IVDC_SD_IRQ_EN	0x0b8U	/* void (*)(irqdev, 0) */
#define T32_IVDC_SD_IRQ_DIS	0x0bcU
#define T32_IVDC_SD_PRIV	0x118U
#define T32_IVDC_PRIV_REGS	0x11cU
#define T32_IVDC_PRIV_STATE	0x1bcU	/* 1 slaked/suspended, 2 stopped/resumed early, 3 running */
#define T32_IVDC_PRIV_LOCK	0x1c8U

#define T32_IVDC_REG_CTRL	0x00U	/* bit 0: start, busy */
#define T32_IVDC_REG_IRQ_MASK	0x40U
#define T32_IVDC_REG_INFO	0x64U	/* bit 1 cleared by isp_info_clear */
#define T32_IVDC_REG_MODE	0x5cU
#define T32_IVDC_IRQ_MASK	0xffbffbffU
#define T32_IVDC_SAVE_BYTES	1708U
#define T32_CPM_CLKGATE		0xc4U
#define T32_CPM_IVDC_RST	0x80000U

struct t32_ivdc_os {
	volatile u8 *cpm;			/* 0xb0000000 */
	void (*lock)(u8 *lock, u32 *flags);	/* __private_spin_lock_irqsave */
	void (*unlock)(u8 *lock, u32 flags);	/* private_spin_unlock_irqrestore */
	void (*clka)(void);			/* get_isp_clka */
	void (*clkv)(void);			/* get_isp_clkv */
	void (*clk_disable_unprepare)(u32 clk);
	void (*msleep)(u32 ms);
	void (*udelay)(u32 us);			/* __udelay */
};

struct t32_ivdc {
	u32 mode;		/* t32_data_d9014 */
	u32 started_mode;	/* t32_data_d9894 */
	u8 *info_priv;		/* t32_data_d9890 */
	u8 *save_buf;		/* t32_data_d98b0 */
	u32 d98b4;
	u32 saved[4];		/* t32_data_d98a0 .. d98ac */
	u32 save_src[4];	/* t32_data_d98e8 .. d98f4 */
	u32 flag[3];		/* t32_data_cf2dc .. cf2e4 */
	u32 ioc_flag;		/* t32_data_d989c: 1 after ioctl 0x100000f, 0 after 0x100000e */
	const struct t32_ivdc_os *os;
};

static inline int t32_ivdc_valid(const u8 *p)
{
	return p && (uintptr_t)p < (uintptr_t)(u32)-4095;
}

static inline u8 *t32_ivdc_priv(const u8 *sd)
{
	return t32_ivdc_valid(sd) ? T32_PTR(t32_g_rd32(sd, T32_IVDC_SD_PRIV)) : 0;
}

static inline volatile u32 *t32_ivdc_reg(const u8 *priv, u32 reg)
{
	return (volatile u32 *)(T32_PTR(t32_g_rd32(priv, T32_IVDC_PRIV_REGS)) + reg);
}

/* ---- empty callbacks ---- */
static inline int t32_ivdc_link_setup(void) { return 0; }
static inline int t32_ivdc_misc_open(void) { return 0; }
static inline int t32_ivdc_misc_release(void) { return 0; }
static inline int t32_ivdc_misc_read(void) { return 0; }
static inline int t32_ivdc_video_s_stream(void) { return 0; }

/* ---- core start / stop ---- */
static inline int t32_ivdc_core_ops_init(struct t32_ivdc *d, u8 *sd, const u32 *on)
{
	static const u32 mode_word[5] = { 0, 0x00041100U, 0x03c41100U, 0x05c41100U, 0x07c41100U };
	u8 *priv = t32_ivdc_priv(sd);
	u32 state, n;

	if (!t32_ivdc_valid(priv)) {
		isp_printf(2, "ivdc_core_ops_init: invalid subdev\n", 0, 0);
		return -22;
	}
	state = t32_g_rd32(priv, T32_IVDC_PRIV_STATE);
	if (!*on) {
		if (state == 2)
			return 0;
		t32_g_wr32(priv, T32_IVDC_PRIV_STATE, 2);
		d->started_mode = d->mode;
		return 0;
	}
	if (state == 3)
		return 0;
	if (!d->mode) {
		d->started_mode = 0;
		*t32_ivdc_reg(priv, T32_IVDC_REG_MODE) = 1;
		return 0;
	}
	if (d->mode != d->started_mode) {	/* pulse the IVDC reset in the CPM clock gate */
		*(volatile u32 *)(d->os->cpm + T32_CPM_CLKGATE) |= T32_CPM_IVDC_RST;
		d->os->msleep(1);
		*(volatile u32 *)(d->os->cpm + T32_CPM_CLKGATE) &= ~T32_CPM_IVDC_RST;
	}
	*t32_ivdc_reg(priv, T32_IVDC_REG_CTRL) = 1;
	while (*t32_ivdc_reg(priv, T32_IVDC_REG_CTRL) & 1)
		for (n = 200; n; n--)
			d->os->udelay(1000);
	if (d->mode >= 1 && d->mode <= 4)
		*t32_ivdc_reg(priv, T32_IVDC_REG_MODE) = mode_word[d->mode];
	isp_printf(1, "ivdc mode %d\n", d->mode, 0);
	t32_g_wr32(priv, T32_IVDC_PRIV_STATE, 3);
	d->started_mode = d->mode;
	return 0;
}

/* ---- module power management ---- */
static inline int t32_ivdc_prepare_module(struct t32_ivdc *d, u8 *sd)
{
	u8 *priv = t32_ivdc_priv(sd);
	u32 i;

	if (!d->mode)
		return 0;
	if (!t32_ivdc_valid(priv)) {
		isp_printf(2, "%s[%d]: invalid subdev\n", "ivdc_prepare_module", 659);
		return -22;
	}
	if (!d->save_buf) {
		isp_printf(2, "%s[%d]: no save buffer\n", "ivdc_prepare_module", 671);
		return -22;
	}
	for (i = 0; i < T32_IVDC_SAVE_BYTES; i += 4)	/* save the IVDC registers */
		t32_g_wr32(d->save_buf, i, *t32_ivdc_reg(priv, i));
	isp_printf(0, "%s[%d]: registers saved\n", "ivdc_prepare_module", 674);
	return 0;
}

static inline int t32_ivdc_resume_module(struct t32_ivdc *d, u8 *sd)
{
	u8 *priv = t32_ivdc_priv(sd);

	if (!d->mode)
		return 0;
	if (!t32_ivdc_valid(priv)) {
		isp_printf(2, "%s[%d]: invalid subdev\n", "ivdc_resume_module", 731);
		return -22;
	}
	return 0;
}

static inline int t32_ivdc_resume_early_module(struct t32_ivdc *d, u8 *sd)
{
	u8 *priv = t32_ivdc_priv(sd);
	u32 on = 1;

	if (!d->mode)
		return 0;
	if (!t32_ivdc_valid(priv)) {
		isp_printf(2, "%s[%d]: invalid subdev\n", "ivdc_resume_early_module", 710);
		return -22;
	}
	t32_g_wr32(priv, T32_IVDC_PRIV_STATE, 2);
	t32_ivdc_core_ops_init(d, sd, &on);
	isp_printf(0, "%s[%d]: resumed\n", "ivdc_resume_early_module", 718);
	return 0;
}

static inline int t32_ivdc_suspend_module(struct t32_ivdc *d, u8 *sd)
{
	u8 *priv = t32_ivdc_priv(sd);
	u32 on = 0, k;

	if (!d->mode)
		return 0;
	if (!t32_ivdc_valid(priv)) {
		isp_printf(2, "%s[%d]: invalid subdev\n", "ivdc_suspend_module", 686);
		return -22;
	}
	t32_ivdc_core_ops_init(d, sd, &on);
	t32_g_wr32(priv, T32_IVDC_PRIV_STATE, 1);
	for (k = 0; k < 4; k++)
		d->saved[k] = d->save_src[k];
	isp_printf(0, "%s[%d]: suspended\n", "ivdc_suspend_module", 698);
	return 0;
}

static inline int t32_ivdc_slake_module(struct t32_ivdc *d, u8 *sd)
{
	u8 *priv = t32_ivdc_priv(sd), *clks;
	u32 on = 0, flags = 0, state;
	int i;

	if (!t32_ivdc_valid(priv))
		return -22;
	state = t32_g_rd32(priv, T32_IVDC_PRIV_STATE);
	if (state == 1)
		return 0;
	if ((int)state >= 3)
		t32_ivdc_core_ops_init(d, sd, &on);
	d->os->lock(priv + T32_IVDC_PRIV_LOCK, &flags);
	t32_g_wr32(priv, T32_IVDC_PRIV_STATE, 1);
	d->os->unlock(priv + T32_IVDC_PRIV_LOCK, flags);
	clks = T32_PTR(t32_g_rd32(sd, T32_IVDC_SD_CLKS));
	d->os->clka();
	d->os->clkv();
	for (i = (int)t32_g_rd32(sd, T32_IVDC_SD_CLK_NUM) - 1; i >= 0; i--)
		d->os->clk_disable_unprepare(t32_g_rd32(clks, (u32)i * 4));
	d->d98b4 = 0;
	d->flag[2] = 1;
	d->flag[1] = 1;
	d->flag[0] = 1;
	return 0;
}

static inline int t32_ivdc_isp_info_clear(struct t32_ivdc *d)
{
	if (!t32_ivdc_valid(d->info_priv)) {
		isp_printf(2, "%s[%d]: no ivdc\n", "ivdc_isp_info_clear", 1407);
		return -22;
	}
	*t32_ivdc_reg(d->info_priv, T32_IVDC_REG_INFO) &= ~2U;
	return 0;
}

/* ---- core ioctl ---- */
#define T32_IVDC_IOC_INIT	0x1000000U	/* sensor-style core op +4 of the subdev ops */
#define T32_IVDC_IOC_MODE	0x1000003U	/* register 0x64 = 7 (mode 1) / 3 (mode >= 2) */
#define T32_IVDC_IOC_FLAG_CLR	0x100000eU
#define T32_IVDC_IOC_FLAG_SET	0x100000fU
#define T32_IVDC_SD_OPS		0x108U
typedef int (*t32_ivdc_sd_fn)(u8 *sd, void *arg);

static inline int t32_ivdc_core_ops_ioctl(struct t32_ivdc *d, u8 *sd, u32 cmd, void *arg)
{
	u8 *priv;
	u32 core, fn;
	int ret;

	switch (cmd) {
	case T32_IVDC_IOC_MODE:
		priv = t32_ivdc_priv(sd);
		if (!t32_ivdc_valid(priv)) {
			isp_printf(2, "%s[%d]: invalid subdev\n", "ivdc_core_ops_ioctl", 537);
			return -22;
		}
		if (d->mode == 1)
			*t32_ivdc_reg(priv, T32_IVDC_REG_INFO) = 7;
		else if ((int)d->mode >= 2)
			*t32_ivdc_reg(priv, T32_IVDC_REG_INFO) = 3;
		return 0;
	case T32_IVDC_IOC_INIT:
		if (!sd)
			return -19;
		core = t32_g_rd32(T32_PTR(t32_g_rd32(sd, T32_IVDC_SD_OPS)), 0);
		if (!core)
			return 0;
		fn = t32_g_rd32(T32_PTR(core), 4);
		if (!fn)
			return 0;
		ret = ((t32_ivdc_sd_fn)T32_PTR(fn))(sd, arg);
		return ret == -515 ? 0 : ret;
	case T32_IVDC_IOC_FLAG_CLR:
		d->ioc_flag = 0;
		return 0;
	case T32_IVDC_IOC_FLAG_SET:
		d->ioc_flag = 1;
		return 0;
	default:
		return 0;
	}
}

/* ---- irq enable / disable of the IVDC subdev ---- */
typedef void (*t32_ivdc_irq_fn)(u8 *irqdev, u32 zero);

static inline void t32_ivdc_enable_irq(struct t32_ivdc *d, u8 *sd)
{
	u8 *priv = T32_PTR(t32_g_rd32(sd, T32_IVDC_SD_PRIV));
	u32 flags = 0, fn;

	d->os->lock(priv + T32_IVDC_PRIV_LOCK, &flags);
	fn = t32_g_rd32(sd, T32_IVDC_SD_IRQ_EN);
	if (fn && !t32_g_rd32(sd, T32_IVDC_SD_IRQ_ON)) {
		((t32_ivdc_irq_fn)T32_PTR(fn))(sd + T32_IVDC_SD_IRQDEV, 0);
		t32_g_wr32(sd, T32_IVDC_SD_IRQ_ON, 1);
		*t32_ivdc_reg(priv, T32_IVDC_REG_IRQ_MASK) = T32_IVDC_IRQ_MASK;
	}
	d->os->unlock(priv + T32_IVDC_PRIV_LOCK, flags);
}

static inline void t32_ivdc_disable_irq(struct t32_ivdc *d, u8 *sd)
{
	u8 *priv = T32_PTR(t32_g_rd32(sd, T32_IVDC_SD_PRIV));
	u32 flags = 0, fn;

	d->os->lock(priv + T32_IVDC_PRIV_LOCK, &flags);
	fn = t32_g_rd32(sd, T32_IVDC_SD_IRQ_DIS);
	if (fn && t32_g_rd32(sd, T32_IVDC_SD_IRQ_ON) == 1) {
		((t32_ivdc_irq_fn)T32_PTR(fn))(sd + T32_IVDC_SD_IRQDEV, 0);
		t32_g_wr32(sd, T32_IVDC_SD_IRQ_ON, 0);
	}
	d->os->unlock(priv + T32_IVDC_PRIV_LOCK, flags);
}

#endif
