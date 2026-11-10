/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 CSI subdevice glue: vendor tx_isp_csi_suspend_module,
 * _resume_module, _core_ops_ioctl (PM stream on), _activate_subdev,
 * _slake_subdev.
 *
 * Subdevice: +256 / +260 clocks / count, +280 CSI device.  CSI device:
 * +704 + 4 i state of input i (1 init, 2 active, 3 re-init, 4 streaming;
 * 2 inputs), +720 mutex, +732 spin lock, +856 PHY reset count
 * (csd->phy_rstz, decremented before every PHY stream on).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txcsi_oracle).
 */
#ifndef TX_ISP_T32_TXCSI_H
#define TX_ISP_T32_TXCSI_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txirq.h"
#include "tx_isp_t32_txcore.h"

#define T32_SD_CSI		280U
#define T32_CSI_STATE(i)	(704U + 4U * (i))
#define T32_CSI_INPUTS		2U
#define T32_CSI_MUTEX		720U
#define T32_CSI_LOCK		732U
#define T32_CSI_PHY_RSTZ	856U
#define T32_CSI_PM_STREAMON	0x1000003U

/* provided by the driver glue (kernel) or by the oracle (host) */
int t32_tx_os_mipi_phy_stream_on(u32 sd, void *ctl);
int t32_tx_os_csi_video_s_stream(u32 sd, void *ctl);
int t32_tx_os_csi_core_ops_init(u32 sd, void *ctl);
void t32_tx_os_mutex_unlock(u32 m);

/* the CSI device of a subdevice, 0 when either is NULL or an ERR_PTR */
static inline u32 t32_tx_csi_dev(u32 sd)
{
	u32 csi;

	if (!t32_tx_sd_ok(sd))
		return 0;
	csi = t32_tx_ld(sd, T32_SD_CSI);
	return t32_tx_sd_ok(csi) ? csi : 0;
}

/* tx_isp_csi_suspend_module */
static inline int t32_tx_csi_suspend_module(u32 sd)
{
	if (!t32_tx_csi_dev(sd)) {
		t32_tx_os_isp_printf(2, "\n [%s %d] CSI is null \n", "tx_isp_csi_suspend_module", 463);
		return -(int)T32_EINVAL;
	}
	t32_tx_os_isp_printf(0, "[%s %d] Enter the suspend ... \n", "tx_isp_csi_suspend_module", 467);
	return 0;
}

/* PHY stream on with { 1, 0 } after one PHY reset count down */
static inline int t32_tx_csi_phy_on(u32 sd, u32 csi)
{
	u32 ctl[2] = { 1, 0 };

	t32_tx_st(csi, T32_CSI_PHY_RSTZ, t32_tx_ld(csi, T32_CSI_PHY_RSTZ) - 1);
	return t32_tx_os_mipi_phy_stream_on(sd, ctl);
}

/* tx_isp_csi_resume_module */
static inline int t32_tx_csi_resume_module(u32 sd)
{
	u32 csi = t32_tx_csi_dev(sd);

	if (!csi) {
		t32_tx_os_isp_printf(2, "\n [%s %d] CSI is null \n", "tx_isp_csi_resume_module", 478);
		return -(int)T32_EINVAL;
	}
	t32_tx_os_isp_printf(0, "[%s %d] Enter the resume ... \n", "tx_isp_csi_resume_module", 482);
	if (t32_tx_csi_phy_on(sd, csi)) {
		t32_tx_os_isp_printf(2, "\n [%s %d] Fail to csi resume \n", "tx_isp_csi_resume_module", 489);
		return -(int)T32_EINVAL;
	}
	return 0;
}

/* tx_isp_csi_core_ops_ioctl: only the PM stream on (tx_isp_csi_pm_streamon) */
static inline int t32_tx_csi_core_ops_ioctl(u32 sd, u32 cmd)
{
	u32 csi;

	if (cmd != T32_CSI_PM_STREAMON)
		return 0;
	csi = t32_tx_csi_dev(sd);
	if (!csi) {
		t32_tx_os_isp_printf(2, "\n [%s %d] CSI is null \n", "tx_isp_csi_pm_streamon", 503);
		return -(int)T32_EINVAL;
	}
	if (t32_tx_csi_phy_on(sd, csi)) {
		t32_tx_os_isp_printf(2, "\n [%s %d] Fail to csi stream on \n", "tx_isp_csi_pm_streamon", 512);
		return -(int)T32_EINVAL;
	}
	t32_tx_os_isp_printf(0, "[%s %d] csi is streamon [csd->phy_rstz %d]!!!\n", "tx_isp_csi_pm_streamon", 515,
			     t32_tx_ld(csi, T32_CSI_PHY_RSTZ));
	return 0;
}

/*
 * tx_isp_csi_activate_subdev: inputs 1 -> 2 under the spin lock, clocks on.
 * Vendor: ends with a mutex_unlock of +720 that has no matching lock.
 */
static inline int t32_tx_csi_activate_subdev(u32 sd)
{
	u32 csi = t32_tx_csi_dev(sd), flags, clks, i;

	if (!csi)
		return -(int)T32_EINVAL;
	flags = t32_tx_os_spin_lock_irqsave(csi + T32_CSI_LOCK);
	for (i = 0; i < T32_CSI_INPUTS; i++)
		if (t32_tx_ld(csi, T32_CSI_STATE(i)) == 1)
			t32_tx_st(csi, T32_CSI_STATE(i), 2);
	t32_tx_os_spin_unlock_irqrestore(csi + T32_CSI_LOCK, flags);
	clks = t32_tx_ld(sd, T32_SD_CLKS);
	if (t32_tx_sd_ok(clks))
		for (i = 0; i < t32_tx_ld(sd, T32_SD_NCLK); i++)
			t32_tx_os_clk_prepare_enable(t32_tx_ld(clks, 4 * i));
	t32_tx_os_mutex_unlock(csi + T32_CSI_MUTEX);
	return 0;
}

/* tx_isp_csi_slake_subdev: stop / re-init each input, 2 -> 1, clocks off in reverse */
static inline int t32_tx_csi_slake_subdev(u32 sd)
{
	u32 csi = t32_tx_csi_dev(sd), ctl[2] = { 0, 0 }, flags, clks, i;
	int k;

	if (!csi)
		return -(int)T32_EINVAL;
	for (i = 0; i < T32_CSI_INPUTS; i++) {
		if (t32_tx_ld(csi, T32_CSI_STATE(i)) == 4) {
			ctl[1] = i;
			t32_tx_os_csi_video_s_stream(sd, ctl);
		}
		if (t32_tx_ld(csi, T32_CSI_STATE(i)) == 3) {
			ctl[1] = i;
			t32_tx_os_csi_core_ops_init(sd, ctl);
		}
		flags = t32_tx_os_spin_lock_irqsave(csi + T32_CSI_LOCK);
		if (t32_tx_ld(csi, T32_CSI_STATE(i)) == 2)
			t32_tx_st(csi, T32_CSI_STATE(i), 1);
		t32_tx_os_spin_unlock_irqrestore(csi + T32_CSI_LOCK, flags);
	}
	clks = t32_tx_ld(sd, T32_SD_CLKS);
	if (t32_tx_sd_ok(clks))
		for (k = (int)(t32_tx_ld(sd, T32_SD_NCLK) - 1); k >= 0; k--)
			t32_tx_os_clk_disable_unprepare(t32_tx_ld(clks, 4 * (u32)k));
	return 0;
}

#endif
