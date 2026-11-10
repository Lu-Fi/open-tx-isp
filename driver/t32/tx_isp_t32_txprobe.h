/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp platform probes of the VIC, VIN, CSI and LDC subdevices
 * (vendor tx_isp_vic_probe, _vin_probe, _csi_probe, _ldc_probe).
 *
 * Each allocates its device (VIC 2532, VIN 504, CSI 868, LDC 424 bytes,
 * kmalloc flags 0x24000c0), clears it, hooks its PM callbacks into the ops
 * table's internal ops when the PM global d9004 is set, runs
 * tx_isp_subdev_init with the static ops table (vendor ced7c / cef08 /
 * cf048 / cf424) and initialises locks and states.  Platform data
 * (pdev +88): +2 / +3 module numbers for the error print.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txprobe_oracle).
 */
#ifndef TX_ISP_T32_TXPROBE_H
#define TX_ISP_T32_TXPROBE_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txvic.h"
#include "tx_isp_t32_txvin.h"
#include "tx_isp_t32_txcsi.h"
#include "tx_isp_t32_txremove.h"

#define T32_GFP_KERNEL		0x24000c0U
#define T32_ENOMEM		12U
#define T32_EBUSY		16U
#define T32_SD_CFG		56U		/* subdevice: config pointer */
#define T32_SD_SELF		280U
#define T32_IOPS_PREPARE	8U		/* internal ops slots */
#define T32_IOPS_SUSPEND	12U
#define T32_IOPS_RESUME		20U
#define T32_IOPS_COMPLETE	24U

#define T32_VIC_BYTES		2532U
#define T32_VIC_MLOCK		804U
#define T32_VIC_SNAP_MLOCK	892U
#define T32_VIC_DONE		880U
#define T32_VIN_BYTES		504U
#define T32_CSI_BYTES		868U
#define T32_CSI_PHY_BASE	0x10022000U
#define T32_CSI_PHY_LEN		4096U
#define T32_CSI_W748		748U
#define T32_CSI_W752		752U
#define T32_LDC_BYTES		424U
#define T32_LDC_MISC_BYTES	52U
#define T32_LDC_LOCK		308U
#define T32_MISC_DYNAMIC	255U

/* vendor objects, provided by the driver glue (kernel) or the oracle (host); addresses */
struct t32_tx_probe_env {
	u32 pm_on;			/* d9004 */
	u32 lock_key;			/* t32_small_bss: lockdep key */
	u32 cfg_src;			/* ce22c: copied into the VIC / LDC config */
	u32 vic_ops, vic_cfg;		/* ced7c, ced0c */
	u32 vin_ops, vin_cfg;		/* cef08, cee9c */
	u32 csi_ops, csi_cfg;		/* cf048, cefdc */
	u32 ldc_ops, ldc_cfg, ldc_fops;	/* cf424, cf3b8, cf34c */
	u32 vic_dev, csi_dev;		/* d9348, d9680 (out) */
	u32 ldc_dev, ldc_misc;		/* d9944, d9948 (out) */
};

/* provided by the driver glue (kernel) or by the oracle (host) */
u32 t32_tx_os_kmalloc(u32 n, u32 flags);
void t32_tx_os_kfree(u32 p);
void t32_tx_os_set_drvdata(u32 pdev, u32 data);
int t32_tx_os_subdev_init(u32 pdev, u32 sd, u32 ops);
void t32_tx_os_spin_lock_init(u32 lock);
void t32_tx_os_raw_mutex_init(u32 lock, const char *name, u32 key);
void t32_tx_os_init_completion(u32 c);
u32 t32_tx_os_request_mem_region(u32 start, u32 n, const char *name);
u32 t32_tx_os_ioremap(u32 start, u32 n);
int t32_tx_os_misc_register(u32 misc);
void t32_tx_os_subdev_deinit(u32 sd);

static inline void t32_tx_iops_set(u32 ops, u32 slot, u32 fn)
{
	t32_tx_st(t32_tx_ld(ops, T32_OPS_INTERNAL), slot, fn);
}

static inline u32 t32_tx_pdata_u8(u32 pdev, u32 off)
{
	return *(volatile u8 *)T32_TX_P(t32_tx_ld(pdev, T32_PDEV_DATA) + off);
}

/* tx_isp_vic_probe */
static inline int t32_tx_vic_probe(struct t32_tx_probe_env *e, u32 pdev)
{
	u32 vic = t32_tx_os_kmalloc(T32_VIC_BYTES, T32_GFP_KERNEL), pd, i;

	if (!vic) {
		t32_tx_os_isp_printf(2, "Failed to allocate vic device\n");
		return -1;
	}
	t32_tx_os_memset(vic, 0, T32_VIC_BYTES);
	pd = t32_tx_ld(pdev, T32_PDEV_DATA);
	if (e->pm_on) {
		t32_tx_iops_set(e->vic_ops, T32_IOPS_SUSPEND, T32_TX_A(t32_tx_vic_suspend_module));
		t32_tx_iops_set(e->vic_ops, T32_IOPS_RESUME, T32_TX_A(t32_tx_vic_resume_module));
	}
	if (t32_tx_os_subdev_init(pdev, vic, e->vic_ops)) {
		t32_tx_os_isp_printf(2, "Failed to init isp module(%d.%d)\n", *(volatile u8 *)T32_TX_P(pd + 2),
				     *(volatile u8 *)T32_TX_P(pd + 3));
		t32_tx_os_kfree(vic);
		return -(int)T32_ENOMEM;
	}
	t32_tx_os_set_drvdata(pdev, vic);
	t32_tx_st(e->vic_cfg, 0, t32_tx_ld(e->cfg_src, 0));
	t32_tx_st(vic, T32_SD_CFG, e->vic_cfg);
	t32_tx_os_spin_lock_init(vic + T32_VIC_MLOCK);
	t32_tx_os_raw_mutex_init(vic + T32_VIC_MLOCK, "&vsd->mlock", e->lock_key);
	t32_tx_os_raw_mutex_init(vic + T32_VIC_SNAP_MLOCK, "&vsd->snap_mlock", e->lock_key);
	t32_tx_os_init_completion(vic + T32_VIC_DONE);
	for (i = 0; i < 3; i++)
		t32_tx_st(vic, T32_VIC_STATE(i), 1);
	t32_tx_st(vic, T32_VIC_SNS_NUM, 1);
	e->vic_dev = vic;
	t32_tx_st(vic, T32_SD_SELF, vic);
	for (i = 0; i < 3; i++)
		t32_tx_st(vic, T32_VIC_INDEX + 4 * i, 4);
	t32_tx_st(vic, T32_VIC_SNS_NUM + 4, 0);
	return 0;
}

/* tx_isp_vin_probe */
static inline int t32_tx_vin_probe(struct t32_tx_probe_env *e, u32 pdev)
{
	u32 vin = t32_tx_os_kmalloc(T32_VIN_BYTES, T32_GFP_KERNEL), i;

	if (!vin) {
		t32_tx_os_isp_printf(2, "ERROR: [ %s:%d ] Failed to allocate sensor subdev\n", "tx_isp_vin_probe", 1249);
		return -(int)T32_ENOMEM;
	}
	t32_tx_os_memset(vin, 0, T32_VIN_BYTES);
	t32_tx_os_raw_mutex_init(vin + T32_VIN_MUTEX, "&vin->mlock", e->lock_key);
	for (i = 0; i < T32_VIN_INPUTS; i++)
		t32_tx_os_raw_mutex_init(vin + T32_VIN_IN_MUTEX(i), "&vin->sen_mlock[i]", e->lock_key);
	t32_tx_st(vin, T32_VIN_LIST, vin + T32_VIN_LIST);
	t32_tx_st(vin, T32_VIN_LIST + 4, vin + T32_VIN_LIST);
	t32_tx_st(vin, T32_VIN_SLAKED, 1);
	t32_tx_st(vin, T32_VIN_ACTIVE, 0);
	for (i = 0; i < T32_VIN_INPUTS; i++)
		t32_tx_st(vin, T32_VIN_SENSOR(i), 0);
	t32_tx_st(vin, T32_VIN_SLAKED + 4, 0);
	if (e->pm_on) {
		t32_tx_iops_set(e->vin_ops, T32_IOPS_PREPARE, T32_TX_A(t32_tx_vin_prepare_module));
		t32_tx_iops_set(e->vin_ops, T32_IOPS_SUSPEND, T32_TX_A(t32_tx_vin_suspend_module));
		t32_tx_iops_set(e->vin_ops, T32_IOPS_RESUME, T32_TX_A(t32_tx_vin_resume_module));
		t32_tx_iops_set(e->vin_ops, T32_IOPS_COMPLETE, T32_TX_A(t32_tx_vin_complete_module));
	}
	if (t32_tx_os_subdev_init(pdev, vin, e->vin_ops)) {
		t32_tx_os_isp_printf(2, "ERROR: [ %s:%d ] Failed to init isp module(%d.%d)\n", "tx_isp_vin_probe", 1280,
				     t32_tx_pdata_u8(pdev, 2), t32_tx_pdata_u8(pdev, 3));
		t32_tx_os_kfree(vin);
		return -(int)T32_ENOMEM;
	}
	t32_tx_st(vin, T32_VIN_LIST - 4, vin);
	t32_tx_os_set_drvdata(pdev, vin);
	t32_tx_st(vin, T32_SD_CFG, e->vin_cfg);
	for (i = 0; i < T32_VIN_INPUTS; i++)
		t32_tx_st(vin, T32_VIN_STATE(i), T32_VIN_ST_INIT);
	t32_tx_st(vin, T32_VIN_BYTES - 4, 0);
	return 0;
}

/* tx_isp_csi_probe: the MIPI PHY region 0x10022000 + 4 KiB */
static inline int t32_tx_csi_probe(struct t32_tx_probe_env *e, u32 pdev)
{
	u32 csi = t32_tx_os_kmalloc(T32_CSI_BYTES, T32_GFP_KERNEL), pd, res;
	int r;

	if (!csi) {
		t32_tx_os_isp_printf(2, "Failed to allocate csi device\n");
		return -(int)T32_ENOMEM;
	}
	t32_tx_os_memset(csi, 0, T32_CSI_BYTES);
	pd = t32_tx_ld(pdev, T32_PDEV_DATA);
	if (e->pm_on) {
		t32_tx_iops_set(e->csi_ops, T32_IOPS_SUSPEND, T32_TX_A(t32_tx_csi_suspend_module));
		t32_tx_iops_set(e->csi_ops, T32_IOPS_RESUME, T32_TX_A(t32_tx_csi_resume_module));
	}
	if (t32_tx_os_subdev_init(pdev, csi, e->csi_ops)) {
		t32_tx_os_isp_printf(2, "Failed to init isp module(%d.%d)\n", *(volatile u8 *)T32_TX_P(pd + 2),
				     *(volatile u8 *)T32_TX_P(pd + 3));
		r = -(int)T32_ENOMEM;
		goto fail;
	}
	res = t32_tx_os_request_mem_region(T32_CSI_PHY_BASE, T32_CSI_PHY_LEN, "mipi-phy");
	if (!res) {
		t32_tx_os_isp_printf(2, "%s[%d] Not enough memory for resources\n", "tx_isp_csi_probe", 718);
		t32_tx_os_subdev_deinit(csi);
		r = -(int)T32_EBUSY;
		goto fail;
	}
	t32_tx_st(csi, T32_CSI_MAP, t32_tx_os_ioremap(t32_tx_ld(res, 0), t32_tx_ld(res, 4) + 1 - t32_tx_ld(res, 0)));
	t32_tx_st(csi, T32_SD_CFG, e->csi_cfg);
	t32_tx_st(csi, T32_CSI_RES, res);
	t32_tx_os_spin_lock_init(csi + T32_CSI_LOCK);
	t32_tx_os_raw_mutex_init(csi + T32_CSI_MUTEX, "&csd->mlock", e->lock_key);
	t32_tx_os_set_drvdata(pdev, csi);
	t32_tx_st(csi, T32_CSI_STATE(0), 1);
	t32_tx_st(csi, T32_CSI_STATE(1), 1);
	t32_tx_st(csi, T32_CSI_W748, 1);
	e->csi_dev = csi;
	t32_tx_st(csi, T32_SD_SELF, csi);
	t32_tx_st(csi, T32_CSI_W752, 0);
	return 0;
fail:
	t32_tx_os_kfree(csi);
	return r;
}

/* tx_isp_ldc_probe.  Vendor: a failed misc_register is printed, the probe still returns 0 */
static inline int t32_tx_ldc_probe(struct t32_tx_probe_env *e, u32 pdev)
{
	u32 ldc = t32_tx_os_kmalloc(T32_LDC_BYTES, T32_GFP_KERNEL), misc, i;

	if (!ldc) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to allocate sensor device\n", "tx_isp_ldc_probe", 384);
		return -(int)T32_ENOMEM;
	}
	t32_tx_os_memset(ldc, 0, T32_LDC_BYTES);
	misc = t32_tx_os_kmalloc(T32_LDC_MISC_BYTES, T32_GFP_KERNEL);
	e->ldc_misc = misc;
	if (!misc) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to allocate offline ldc device\n", "tx_isp_ldc_probe", 392);
		return -(int)T32_ENOMEM;
	}
	t32_tx_os_memset(misc, 0, T32_LDC_MISC_BYTES);
	e->ldc_dev = ldc;
	if (t32_tx_os_subdev_init(pdev, ldc, e->ldc_ops)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to init isp module(%d.%d)\n", "tx_isp_ldc_probe", 403,
				     t32_tx_pdata_u8(pdev, 2), t32_tx_pdata_u8(pdev, 3));
		t32_tx_os_kfree(ldc);
		return -(int)T32_ENOMEM;
	}
	t32_tx_os_spin_lock_init(ldc + T32_LDC_LOCK);
	t32_tx_os_raw_mutex_init(ldc + T32_LDC_LOCK, "&ldc_dev->mlock", e->lock_key);
	t32_tx_st(ldc, T32_LDC_LOCK - 4, t32_tx_ld(pdev, T32_PDEV_DATA));
	for (i = 0; i < 4; i++)
		t32_tx_st(ldc, 288 + 4 * i, 1);
	t32_tx_os_set_drvdata(pdev, ldc);
	t32_tx_st(e->ldc_cfg, 0, t32_tx_ld(e->cfg_src, 0));
	t32_tx_st(ldc, T32_SD_CFG, e->ldc_cfg);
	t32_tx_st(ldc, T32_SD_SELF, ldc);
	t32_tx_st(misc, 0, T32_MISC_DYNAMIC);
	t32_tx_st(misc, 4, T32_TX_A("offline_ldc"));
	t32_tx_st(misc, 8, e->ldc_fops);
	if (t32_tx_os_misc_register(misc) < 0)
		t32_tx_os_isp_printf(2, "Failed to register offline_ldc!\n");
	return 0;
}

#endif
