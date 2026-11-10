/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIC subdevice glue: vendor tx_isp_vic_start, _index_cal, _reset
 * (.isra), _activate_subdev, _slake_subdev, _suspend_module,
 * _resume_module, _save_raw.
 *
 * Subdevice: +256 / +260 clocks / count, +280 VIC device.  VIC device:
 *   +288 + 104 i sensor attribute block of input i (+0..+8 copied into the
 *   VIC init message, +52 VIC init argument), +340 sensor, +736 / +784 index
 *   state, +768 + 4 i input state (1 init, 2 active, 3 re-init, 4 streaming;
 *   2 inputs), +804 lock (mutex in activate / slake, spin lock in reset),
 *   +820 started, +824 reset busy, +1096 sensor count, +2464 + 2 i + j
 *   "switch" GPIO armed flags (u8) with the GPIO number / level at
 *   +2472 / +2476 + 32 i + 16 j.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txvic_oracle).
 */
#ifndef TX_ISP_T32_TXVIC_H
#define TX_ISP_T32_TXVIC_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txirq.h"
#include "tx_isp_t32_txcore.h"

#define T32_SD_VIC		280U
#define T32_VIC_ATTR(i)		(288U + 104U * (i))
#define T32_VIC_ATTR_INIT	52U
#define T32_VIC_SENSOR		340U
#define T32_VIC_INDEX		736U
#define T32_VIC_INDEX2		784U
#define T32_VIC_STATE(i)	(768U + 4U * (i))
#define T32_VIC_INPUTS		2U
#define T32_VIC_LOCK		804U
#define T32_VIC_STARTED		820U
#define T32_VIC_RESET_BUSY	824U
#define T32_VIC_SNS_NUM		1096U
#define T32_VIC_GPIO_ON(i, j)	(2464U + 2U * (i) + (j))
#define T32_VIC_GPIO(i, j)	(2472U + 32U * (i) + 16U * (j))
#define T32_VIC_GPIO_LVL(i, j)	(2476U + 32U * (i) + 16U * (j))
#define T32_VIC_GPIO_HIGH	5U

#define T32_SNS_MODE		148U	/* sensor +148: 2 selects VIC control 17424, else 17472 */
#define T32_VIC_CTRL_A		17424U
#define T32_VIC_CTRL_B		17472U

/* provided by the driver glue (kernel) or by the oracle (host) */
void t32_tx_os_mutex_lock(u32 m);
void t32_tx_os_mutex_unlock(u32 m);
void t32_tx_os_vic_reset(void);
void t32_tx_os_vic_stream_on(void);
void t32_tx_os_vic_deinit(void);
int t32_tx_os_vic_init(u32 index, u32 arg, void *msg);
void t32_tx_os_vic_ctrl(u32 which, u32 val);
int t32_tx_os_vic_set_par(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);
int t32_tx_os_vic_dma_start(void);
int t32_tx_os_vic_core_s_stream(u32 sd, void *ctl);
int t32_tx_os_vic_core_ops_init(u32 sd, void *ctl);
void t32_tx_os_gpio_request(u32 gpio, const char *label);
u32 t32_tx_os_gpio_to_desc(u32 gpio);
u32 t32_tx_os_gpiod_get_raw_value(u32 desc);
void t32_tx_os_gpio_direction_output(u32 gpio, u32 value);
void t32_tx_os_gpio_free(u32 gpio);

/* the VIC device of a subdevice, 0 when either is NULL or an ERR_PTR */
static inline u32 t32_tx_vic_dev(u32 sd)
{
	u32 vic;

	if (!t32_tx_sd_ok(sd))
		return 0;
	vic = t32_tx_ld(sd, T32_SD_VIC);
	return t32_tx_sd_ok(vic) ? vic : 0;
}

/* tx_isp_vic_start(vic, force) */
static inline int t32_tx_vic_start(u32 vic, u32 force)
{
	if (t32_tx_ld(vic, T32_VIC_SNS_NUM) == 1 || force == 1) {
		t32_tx_os_vic_reset();
		t32_tx_os_vic_stream_on();
		t32_tx_st(vic, T32_VIC_STARTED, 1);
	}
	return 0;
}

/* tx_isp_vic_index_cal(vic, input): VIC index of an input for 1..4 sensors */
static inline int t32_tx_vic_index_cal(u32 vic, u32 input)
{
	u32 n = t32_tx_ld(vic, T32_VIC_SNS_NUM);

	t32_tx_st(vic, T32_VIC_INDEX, input);
	t32_tx_st(vic, T32_VIC_INDEX2, 0);
	if (n && n < 3)
		return (int)input;
	if (n && n < 5)
		return (int)input % 2;
	t32_tx_os_isp_printf(2, "[ %s:%d ] Can not support this sensor num!!!\n", "tx_isp_vic_index_cal", 132);
	return 0;
}

/*
 * tx_isp_vic_reset (.isra).  Vendor: a NULL / ERR_PTR subdevice reads the
 * VIC sensor from address 340 (fault); not reachable here.
 */
static inline int t32_tx_vic_reset(u32 sd)
{
	u32 vic = t32_tx_sd_ok(sd) ? t32_tx_ld(sd, T32_SD_VIC) : 0, flags, ctrl;

	if (!t32_tx_ld(vic, T32_VIC_SENSOR))
		return -(int)T32_EINVAL;
	flags = t32_tx_os_spin_lock_irqsave(vic + T32_VIC_LOCK);
	t32_tx_os_vic_reset();
	if (!t32_tx_ld(vic, T32_VIC_RESET_BUSY)) {
		ctrl = t32_tx_ld(t32_tx_ld(vic, T32_VIC_SENSOR), T32_SNS_MODE) == 2 ? T32_VIC_CTRL_A : T32_VIC_CTRL_B;
		t32_tx_os_vic_ctrl(0, ctrl);
		t32_tx_os_vic_ctrl(1, ctrl);
	}
	t32_tx_os_spin_unlock_irqrestore(vic + T32_VIC_LOCK, flags);
	return 0;
}

/* tx_isp_vic_activate_subdev: inputs 1 -> 2; clocks on when one changed or input 1 was 1 */
static inline int t32_tx_vic_activate_subdev(u32 sd)
{
	u32 vic, i, changed = 0, clks;

	if (!t32_tx_sd_ok(sd))
		return -(int)T32_EINVAL;
	vic = t32_tx_ld(sd, T32_SD_VIC);
	if (!t32_tx_sd_ok(vic))
		return -(int)T32_EINVAL;
	t32_tx_os_mutex_lock(vic + T32_VIC_LOCK);
	for (i = 0; i < T32_VIC_INPUTS; i++)
		if (t32_tx_ld(vic, T32_VIC_STATE(i)) == 1) {
			t32_tx_st(vic, T32_VIC_STATE(i), 2);
			changed = 1;
		}
	if (changed) {
		clks = t32_tx_ld(sd, T32_SD_CLKS);
		for (i = 0; i < t32_tx_ld(sd, T32_SD_NCLK); i++)
			t32_tx_os_clk_prepare_enable(t32_tx_ld(clks, 4 * i));
	}
	t32_tx_os_mutex_unlock(vic + T32_VIC_LOCK);
	return 0;
}

/*
 * tx_isp_vic_slake_subdev: stop / re-init each input, 2 -> 1.  Vendor: once
 * an input went back to 1 the clocks are switched off (reverse) at the end
 * of this and of every later input's step, i.e. possibly twice.
 */
static inline int t32_tx_vic_slake_subdev(u32 sd)
{
	u32 vic, i, changed = 0, ctl;
	int k;

	if (!t32_tx_sd_ok(sd))
		return -(int)T32_EINVAL;
	vic = t32_tx_ld(sd, T32_SD_VIC);
	if (!t32_tx_sd_ok(vic))
		return -(int)T32_EINVAL;
	for (i = 0; i < T32_VIC_INPUTS; i++) {
		ctl = 0;
		if (t32_tx_ld(vic, T32_VIC_STATE(i)) == 4)
			t32_tx_os_vic_core_s_stream(sd, &ctl);
		if (t32_tx_ld(vic, T32_VIC_STATE(i)) == 3)
			t32_tx_os_vic_core_ops_init(sd, &ctl);
		t32_tx_os_mutex_lock(vic + T32_VIC_LOCK);
		if (t32_tx_ld(vic, T32_VIC_STATE(i)) == 2) {
			changed++;
			t32_tx_st(vic, T32_VIC_STATE(i), 1);
		}
		if (changed)
			for (k = (int)(t32_tx_ld(sd, T32_SD_NCLK) - 1); k >= 0; k--)
				t32_tx_os_clk_disable_unprepare(t32_tx_ld(t32_tx_ld(sd, T32_SD_CLKS), 4 * (u32)k));
		t32_tx_os_mutex_unlock(vic + T32_VIC_LOCK);
	}
	return 0;
}

/* tx_isp_vic_suspend_module: drive every armed "switch" GPIO to its level (5 = high), then free it */
static inline int t32_tx_vic_suspend_module(u32 sd)
{
	u32 vic = t32_tx_vic_dev(sd), n, i, j;

	if (!vic) {
		t32_tx_os_isp_printf(2, "\n [%s %d] VIC is null \n", "tx_isp_vic_suspend_module", 1326);
		return -(int)T32_EINVAL;
	}
	n = t32_tx_ld(vic, T32_VIC_SNS_NUM) < 2 ? 1 : 2;
	for (i = 0; i < n; i++)
		for (j = 0; j < 2; j++) {
			u8 *on = T32_TX_P(vic + T32_VIC_GPIO_ON(i, j));
			u32 gpio, high;

			if (!*on)
				continue;
			gpio = t32_tx_ld(vic, T32_VIC_GPIO(i, j));
			high = t32_tx_ld(vic, T32_VIC_GPIO_LVL(i, j)) == T32_VIC_GPIO_HIGH;
			t32_tx_os_gpio_request(gpio, "switch");
			if (t32_tx_os_gpiod_get_raw_value(t32_tx_os_gpio_to_desc(gpio)) != high)
				t32_tx_os_gpio_direction_output(gpio, high);
			t32_tx_os_gpio_free(t32_tx_ld(vic, T32_VIC_GPIO(i, j)));
			*on = 0;
		}
	t32_tx_os_isp_printf(0, "[%s %d] Enter the suspend ... \n", "tx_isp_vic_suspend_module", 1361);
	return 0;
}

/* tx_isp_vic_resume_module: VIC re-init per input from its sensor attributes, stream on */
static inline int t32_tx_vic_resume_module(u32 sd)
{
	u32 vic = t32_tx_vic_dev(sd), n, i, msg[4];
	int r = 0;

	if (!vic) {
		t32_tx_os_isp_printf(2, "\n [%s %d] VIC is null \n", "tx_isp_vic_resume_module", 1374);
		return -(int)T32_EINVAL;
	}
	t32_tx_os_vic_deinit();
	n = t32_tx_ld(vic, T32_VIC_SNS_NUM) < 2 ? 1 : 2;
	for (i = 0; i < n; i++) {
		u32 idx = (u32)t32_tx_vic_index_cal(vic, i), at = vic + T32_VIC_ATTR(i);

		msg[0] = t32_tx_ld(at, 0);
		msg[1] = t32_tx_ld(at, 4);
		msg[2] = t32_tx_ld(at, 8);
		memcpy(&msg[3], T32_TX_P(vic + T32_VIC_SNS_NUM), 4);
		r = t32_tx_os_vic_init(idx & 0xff, t32_tx_ld(at, T32_VIC_ATTR_INIT), msg);
	}
	t32_tx_os_vic_stream_on();
	if (r) {
		t32_tx_os_isp_printf(2, "\n [%s %d] Fail to vic suspend\n", "tx_isp_vic_resume_module", 1402);
		return -(int)T32_EINVAL;
	}
	t32_tx_os_isp_printf(0, "[%s %d] Enter the resume ... \n", "tx_isp_vic_resume_module", 1405);
	return 0;
}

/*
 * tx_isp_vic_save_raw(ch, a, b, c): VIC parameters (mode 3), then the DMA
 * start.  Vendor: on a parameter error it returns the result of its
 * isp_printf; ours returns 0 there.
 */
static inline int t32_tx_vic_save_raw(u32 ch, u32 a, u32 b, u32 c)
{
	if (t32_tx_os_vic_set_par(ch & 0xff, a & 0xff, 3, b & 0xff, c & 0xff, 0)) {
		t32_tx_os_isp_printf(1, "\n[%s %d] save raw error !!!\n", "tx_isp_vic_save_raw", 2005);
		return 0;
	}
	return t32_tx_os_vic_dma_start();
}

#endif
