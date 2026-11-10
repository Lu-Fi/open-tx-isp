/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp platform remove paths: vendor tx_isp_vic_remove, _vin_remove,
 * _csi_remove, _ivdc_remove, _ldc_remove, _fs_remove, _core_remove,
 * tx_isp_remove and tx_isp_frame_chan_deinit.
 *
 * Every remove takes the platform device; its drvdata is the subdevice
 * (+280 the block's private device).  Vendor globals cleared here: d9890
 * (IVDC), d9940 / d9948 (LDC buffers / misc device), d9950 (frame source).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txremove_oracle): every hook call in
 * order with its arguments, the CSI PHY register stores (traced window),
 * the objects and globals, the return values.
 */
#ifndef TX_ISP_T32_TXREMOVE_H
#define TX_ISP_T32_TXREMOVE_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txirq.h"
#include "tx_isp_t32_txcore.h"

#define T32_SD_PRIV		280U

/* frame source: +288 channels (816 bytes each), +292 channel count */
#define T32_FS_CHANS		288U
#define T32_FS_NCHAN		292U
#define T32_FS_CHAN_BYTES	816U
#define T32_CHAN_VBQ		44U	/* vb2 queue */
#define T32_CHAN_VBQ_ARG	532U
#define T32_CHAN_DEAD		736U

/* CSI device: +244 / +248 PHY register bases (+16 reset), +732 resource, +736 mapping */
#define T32_CSI_PHY0		244U
#define T32_CSI_PHY1		248U
#define T32_CSI_PHY_RST		16U
#define T32_CSI_RES		732U
#define T32_CSI_MAP		736U

/* ISP core device */
#define T32_CORE_TUNING		1704U
#define T32_CORE_STATE		300U
#define T32_CORE_BUF		1132U
#define T32_CORE_BUF_FREED	1140U
#define T32_CORE_TISP1		1196U

/* ISP device */
#define T32_ISP_MISC		12U
#define T32_ISP_PROC		564U
#define T32_ISP_PLATS		160U

/* vendor globals, provided by the driver glue (kernel) or the oracle (host) */
extern u32 t32_tx_ivdc_dev;		/* d9890 */
extern u32 t32_tx_ldc_buf;		/* d9940 */
extern u32 t32_tx_ldc_misc;		/* d9948 */
extern u32 t32_tx_fs_dev;		/* d9950 */

/* provided by the driver glue (kernel) or by the oracle (host) */
void t32_tx_os_set_drvdata(u32 pdev, u32 data);
void t32_tx_os_kfree(u32 p);
void t32_tx_os_subdev_deinit(u32 sd);
void t32_tx_os_misc_deregister(u32 misc);
void t32_tx_os_iounmap(u32 a);
void t32_tx_os_release_mem_region(u32 start, u32 n);
void t32_tx_os_vb2_queue_cancel(u32 q);
void t32_tx_os_vb2_queue_free(u32 q, u32 arg);
void t32_tx_os_riscv_remove(void);
void t32_tx_os_proc_remove(u32 p);
void t32_tx_os_core_tuning_deinit(u32 t);
void t32_tx_os_ispcore_slake_module(u32 core);
void t32_tx_os_tisp_deinit(u32 which);
void t32_tx_os_tisp_remove(void);

/* tx_isp_frame_chan_deinit */
static inline void t32_tx_frame_chan_deinit(u32 ch)
{
	if (!t32_tx_sd_ok(ch))
		return;
	t32_tx_os_vb2_queue_cancel(ch + T32_CHAN_VBQ);
	t32_tx_os_vb2_queue_free(ch + T32_CHAN_VBQ, t32_tx_ld(ch, T32_CHAN_VBQ_ARG));
	t32_tx_st(ch, T32_CHAN_DEAD, 1);
}

/* tx_isp_vic_remove: the VIC device is freed */
static inline int t32_tx_vic_remove(u32 pdev)
{
	u32 sd = t32_tx_os_get_drvdata(pdev), priv = 0;

	if (!t32_tx_sd_ok(sd))
		sd = 0;
	else
		priv = t32_tx_ld(sd, T32_SD_PRIV);
	t32_tx_os_set_drvdata(pdev, 0);
	t32_tx_os_subdev_deinit(sd);
	t32_tx_os_kfree(priv);
	return 0;
}

/* tx_isp_vin_remove: the VIN device starts with its subdevice */
static inline int t32_tx_vin_remove(u32 pdev)
{
	u32 sd = t32_tx_os_get_drvdata(pdev);

	if (!t32_tx_sd_ok(sd))
		sd = 0;
	t32_tx_os_set_drvdata(pdev, 0);
	t32_tx_os_subdev_deinit(sd);
	t32_tx_os_kfree(sd);
	return 0;
}

/* tx_isp_csi_remove: PHY resets pulsed, mapping and region released.  Vendor: no NULL check */
static inline int t32_tx_csi_remove(u32 pdev)
{
	u32 sd = t32_tx_os_get_drvdata(pdev), res;

	if (!t32_tx_sd_ok(sd))
		sd = 0;
	res = t32_tx_ld(sd, T32_CSI_RES);
	t32_tx_wr(t32_tx_ld(sd, T32_CSI_PHY0) + T32_CSI_PHY_RST, 0);
	t32_tx_wr(t32_tx_ld(sd, T32_CSI_PHY0) + T32_CSI_PHY_RST, 1);
	t32_tx_wr(t32_tx_ld(sd, T32_CSI_PHY1) + T32_CSI_PHY_RST, 0);
	t32_tx_wr(t32_tx_ld(sd, T32_CSI_PHY1) + T32_CSI_PHY_RST, 1);
	t32_tx_os_set_drvdata(pdev, 0);
	t32_tx_os_iounmap(t32_tx_ld(sd, T32_CSI_MAP));
	t32_tx_os_release_mem_region(t32_tx_ld(res, 0), t32_tx_ld(res, 4) + 1 - t32_tx_ld(res, 0));
	t32_tx_os_subdev_deinit(sd);
	t32_tx_os_kfree(sd);
	return 0;
}

/* tx_isp_ivdc_remove */
static inline int t32_tx_ivdc_remove(u32 pdev)
{
	u32 sd = t32_tx_os_get_drvdata(pdev), priv = t32_tx_ld(sd, T32_SD_PRIV);

	t32_tx_ivdc_dev = 0;
	t32_tx_os_misc_deregister(priv);
	t32_tx_os_subdev_deinit(sd);
	t32_tx_os_kfree(priv);
	return 0;
}

/* tx_isp_ldc_remove */
static inline int t32_tx_ldc_remove(u32 pdev)
{
	u32 sd = t32_tx_os_get_drvdata(pdev);

	t32_tx_os_misc_deregister(t32_tx_ldc_misc);
	t32_tx_os_subdev_deinit(sd);
	if (t32_tx_ldc_misc) {
		t32_tx_os_kfree(t32_tx_ldc_misc);
		t32_tx_ldc_misc = 0;
	}
	if (t32_tx_ldc_buf) {
		t32_tx_os_kfree(t32_tx_ldc_buf);
		t32_tx_ldc_buf = 0;
	}
	return 0;
}

/* tx_isp_fs_remove: every frame channel, the channel array, the subdevice, the frame source */
static inline int t32_tx_fs_remove(u32 pdev)
{
	u32 sd = t32_tx_os_get_drvdata(pdev), fs = t32_tx_ld(sd, T32_SD_PRIV);
	int i;

	for (i = 0; i < (int)t32_tx_ld(fs, T32_FS_NCHAN); i++)
		t32_tx_frame_chan_deinit(t32_tx_ld(fs, T32_FS_CHANS) + (u32)i * T32_FS_CHAN_BYTES);
	if (t32_tx_ld(fs, T32_FS_CHANS))
		t32_tx_os_kfree(t32_tx_ld(fs, T32_FS_CHANS));
	t32_tx_os_subdev_deinit(sd);
	t32_tx_fs_dev = 0;
	t32_tx_os_kfree(fs);
	return 0;
}

/* tx_isp_core_remove */
static inline int t32_tx_core_remove(u32 pdev)
{
	u32 sd = t32_tx_os_get_drvdata(pdev), core = t32_tx_ld(sd, T32_SD_PRIV);

	if (t32_tx_ld(core, T32_CORE_TUNING)) {
		t32_tx_os_core_tuning_deinit(t32_tx_ld(core, T32_CORE_TUNING));
		t32_tx_st(core, T32_CORE_TUNING, 0);
	}
	if ((int)t32_tx_ld(core, T32_CORE_STATE) >= 2)
		t32_tx_os_ispcore_slake_module(core);
	t32_tx_os_kfree(t32_tx_ld(core, T32_CORE_BUF));
	t32_tx_st(core, T32_CORE_BUF_FREED, 1);
	t32_tx_st(core, T32_CORE_BUF, 0);
	t32_tx_os_subdev_deinit(sd);
	t32_tx_os_tisp_deinit(0);
	t32_tx_os_tisp_remove();
	if (t32_tx_ld(core, T32_CORE_TISP1) != 1)
		t32_tx_os_tisp_deinit(1);
	t32_tx_os_kfree(core);
	return 0;
}

/* tx_isp_remove: the ISP device */
static inline int t32_tx_isp_remove(u32 pdev)
{
	u32 dev = t32_tx_os_get_drvdata(pdev);

	t32_tx_os_riscv_remove();
	t32_tx_os_misc_deregister(dev + T32_ISP_MISC);
	t32_tx_os_proc_remove(t32_tx_ld(dev, T32_ISP_PROC));
	t32_tx_isp_unregister_platforms(dev + T32_ISP_PLATS);
	t32_tx_os_set_drvdata(pdev, 0);
	t32_tx_os_kfree(dev);
	return 0;
}

#endif
