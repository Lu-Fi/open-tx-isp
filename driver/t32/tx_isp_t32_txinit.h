/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp module init (vendor tx_isp_init): the RISC-V fast-start
 * prepare hook (f3830) when d9008 is set, the platform device (ce280) and
 * driver (cf620: +28 from ce22c, +76 PM ops cf67c when d9004 is set), the
 * /proc entries ispbuf_len, ivdcbuf_len (d9014 > 0), vpuBs (d9008) and
 * camera_mode (f3760 >= 2).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txinit_oracle).
 */
#ifndef TX_ISP_T32_TXINIT_H
#define TX_ISP_T32_TXINIT_H

#include "tx_isp_t32_txos.h"

#define T32_PDRV_W28		28U
#define T32_PDRV_PM		76U

struct t32_tx_init_env {
	u32 fast_start;		/* d9008 */
	u32 pm_on;		/* d9004 */
	u32 link_mode;		/* d9014 */
	u32 sensor_num;		/* f3760 */
	u32 hf_prepare;		/* f3830: int (*)(void) or 0 */
	u32 pdev, pdrv, pm_ops, cfg_src;			/* ce280, cf620, cf67c, ce22c */
	u32 fops_ispbuf, fops_ivdcbuf, fops_vpubs, fops_mode;	/* bec88, bec1c, bebb0, becf4 */
};

typedef int (*t32_tx_void_fn)(void);

/* provided by the driver glue (kernel) or by the oracle (host) */
int t32_tx_os_platform_device_register(u32 pdev);
int t32_tx_os_platform_driver_register(u32 pdrv);
void t32_tx_os_platform_device_unregister(u32 pdev);
u32 t32_tx_os_proc_create_data(const char *name, u32 mode, u32 parent, u32 fops, u32 data);

/* tx_isp_init.  Vendor: a failed driver register still creates the /proc entries; its result is returned */
static inline int t32_tx_isp_init(const struct t32_tx_init_env *e)
{
	int r;

	if (e->fast_start && e->hf_prepare) {
		r = ((t32_tx_void_fn)T32_TX_P(e->hf_prepare))();
		if (r < 0) {
			t32_tx_os_isp_printf(1, "tx_isp_riscv_hf_prepare failed\n");
			return r;
		}
	}
	r = t32_tx_os_platform_device_register(e->pdev);
	if (r) {
		t32_tx_os_isp_printf(1, "[ %s:%d ] Failed to insmod isp driver!!!\n", "tx_isp_init", 3762);
		return r;
	}
	if (e->pm_on)
		t32_tx_st(e->pdrv, T32_PDRV_PM, e->pm_ops);
	t32_tx_st(e->pdrv, T32_PDRV_W28, t32_tx_ld(e->cfg_src, 0));
	r = t32_tx_os_platform_driver_register(e->pdrv);
	if (r)
		t32_tx_os_platform_device_unregister(e->pdev);
	if (e->fast_start) {
		t32_tx_os_proc_create_data("ispbuf_len", 0444, 0, e->fops_ispbuf, 0);
		if ((int)e->link_mode > 0)
			t32_tx_os_proc_create_data("ivdcbuf_len", 0444, 0, e->fops_ivdcbuf, 0);
		t32_tx_os_proc_create_data("vpuBs", 0444, 0, e->fops_vpubs, 0);
	}
	if ((int)e->sensor_num >= 2)
		t32_tx_os_proc_create_data("camera_mode", 0444, 0, e->fops_mode, 0);
	return r;
}

#endif
