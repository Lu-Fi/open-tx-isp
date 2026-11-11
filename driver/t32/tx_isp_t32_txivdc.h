/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IVDC platform probe (vendor tx_isp_ivdc_probe): 512 byte device
 * (+0 misc device: minor 70 with fast start d9008, else dynamic, name
 * "misc-ivdc", fops cf19c; +40 subdevice with its pads at +308 / +310
 * (u16 counts) and +312 / +316 (36 byte pads, +28 event handler, +32
 * owner); +96 config cf208; +320 self; +444 state; +452 platform data;
 * +456 lock; +468 completion), the 4 KiB IVDC buffer (d98b0) and the PM
 * callbacks in the ops table cf274 when d9004 is set; d9890 / d9894 =
 * device / link mode (d9014) on success.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txivdc_oracle).
 */
#ifndef TX_ISP_T32_TXIVDC_H
#define TX_ISP_T32_TXIVDC_H

#include "tx_isp_t32_txos.h"

#define T32_IVDC_BYTES		512U
#define T32_IVDC_SD		40U
#define T32_IVDC_NPAD_IN	308U	/* u16 */
#define T32_IVDC_NPAD_OUT	310U	/* u16 */
#define T32_IVDC_PAD_IN		312U
#define T32_IVDC_PAD_OUT	316U
#define T32_PAD_BYTES		36U
#define T32_PAD_EVENT		28U
#define T32_PAD_OWNER		32U
#define T32_IVDC_CFG		96U
#define T32_IVDC_SELF		320U
#define T32_IVDC_STATE		444U
#define T32_IVDC_PDATA		452U
#define T32_IVDC_LOCK		456U
#define T32_IVDC_DONE		468U
#define T32_IVDC_BUF_BYTES	4096U
#define T32_IVDC_FAST_MINOR	70U

struct t32_tx_ivdc_env {
	u32 pm_on, fast_start, link_mode;	/* d9004, d9008, d9014 */
	u32 lock_key, ops, cfg, fops;		/* t32_small_bss, cf274, cf208, cf19c */
	u32 buf, dev, mode;			/* d98b0, d9890, d9894 (out) */
};

/* provided by the driver glue (kernel) or by the oracle (host) */
u32 t32_tx_os_kmalloc(u32 n, u32 flags);
void t32_tx_os_kfree_plain(u32 p);
void t32_tx_os_memset(u32 a, u32 c, u32 n);
void t32_tx_os_set_drvdata(u32 pdev, u32 data);
int t32_tx_os_subdev_init(u32 pdev, u32 sd, u32 ops);
void t32_tx_os_spin_lock_init(u32 lock);
void t32_tx_os_raw_mutex_init(u32 lock, const char *name, u32 key);
void t32_tx_os_init_completion(u32 c);
int t32_tx_os_misc_register(u32 misc);
int t32_tx_ivdc_suspend_module(u32 sd);
int t32_tx_ivdc_resume_early_module(u32 sd);
int t32_tx_ivdc_resume_module(u32 sd);
int t32_tx_ivdc_prepare_module(u32 sd);
int t32_tx_ivdc_complete_module(u32 sd);
int t32_tx_ivdc_pad_event_handle(u32 pad, u32 ev, u32 arg);

#define T32_IVDC_GFP		0x24000c0U

/*
 * tx_isp_ivdc_probe(pdev).  Vendor: a failed buffer allocation and a failed
 * misc_register leave the device allocated.
 */
static inline int t32_tx_ivdc_probe(struct t32_tx_ivdc_env *e, u32 pdev)
{
	u32 dev = t32_tx_os_kmalloc(T32_IVDC_BYTES, T32_IVDC_GFP), pd, ops, i;

	if (!dev) {
		t32_tx_os_isp_printf(2, "Failed to allocate ivdc device\n");
		return -12;
	}
	t32_tx_os_memset(dev, 0, T32_IVDC_BYTES);
	pd = t32_tx_ld(pdev, 88);
	if (e->pm_on) {
		ops = t32_tx_ld(e->ops, 16);
		t32_tx_st(ops, 12, T32_TX_A(t32_tx_ivdc_suspend_module));
		t32_tx_st(ops, 16, T32_TX_A(t32_tx_ivdc_resume_early_module));
		t32_tx_st(ops, 20, T32_TX_A(t32_tx_ivdc_resume_module));
		t32_tx_st(ops, 8, T32_TX_A(t32_tx_ivdc_prepare_module));
		t32_tx_st(ops, 24, T32_TX_A(t32_tx_ivdc_complete_module));
		e->buf = t32_tx_os_kmalloc(T32_IVDC_BUF_BYTES, T32_IVDC_GFP);
		if (!e->buf) {
			t32_tx_os_isp_printf(2, "[%s %d] IVDC malloc is NULL\n", "tx_isp_ivdc_probe", 1778);
			return -22;
		}
	}
	if (t32_tx_os_subdev_init(pdev, dev + T32_IVDC_SD, e->ops)) {
		t32_tx_os_isp_printf(2, "Failed to init isp module(%d.%d)\n", *(volatile u8 *)T32_TX_P(pd + 2),
				     *(volatile u8 *)T32_TX_P(pd + 3));
		t32_tx_os_kfree_plain(dev);
		return -12;
	}
	t32_tx_os_spin_lock_init(dev + T32_IVDC_LOCK);
	t32_tx_os_raw_mutex_init(dev + T32_IVDC_LOCK, "&ivdc->mlock", e->lock_key);
	t32_tx_os_init_completion(dev + T32_IVDC_DONE);
	t32_tx_st(dev, T32_IVDC_PDATA, t32_tx_ld(pdev, 88));
	for (i = 0; i < *(volatile u16 *)T32_TX_P(dev + T32_IVDC_NPAD_OUT); i++) {
		t32_tx_st(t32_tx_ld(dev, T32_IVDC_PAD_OUT) + i * T32_PAD_BYTES, T32_PAD_EVENT, T32_TX_A(t32_tx_ivdc_pad_event_handle));
		t32_tx_st(t32_tx_ld(dev, T32_IVDC_PAD_OUT) + i * T32_PAD_BYTES, T32_PAD_OWNER, dev);
	}
	for (i = 0; i < *(volatile u16 *)T32_TX_P(dev + T32_IVDC_NPAD_IN); i++) {
		t32_tx_st(t32_tx_ld(dev, T32_IVDC_PAD_IN) + i * T32_PAD_BYTES, T32_PAD_EVENT, T32_TX_A(t32_tx_ivdc_pad_event_handle));
		t32_tx_st(t32_tx_ld(dev, T32_IVDC_PAD_IN) + i * T32_PAD_BYTES, T32_PAD_OWNER, dev);
	}
	t32_tx_st(dev, T32_IVDC_STATE, 1);
	t32_tx_os_set_drvdata(pdev, dev + T32_IVDC_SD);
	t32_tx_st(dev, T32_IVDC_CFG, e->cfg);
	t32_tx_st(dev, T32_IVDC_SELF, dev);
	t32_tx_st(dev, 0, e->fast_start ? T32_IVDC_FAST_MINOR : 255U);
	t32_tx_st(dev, 4, T32_TX_A("misc-ivdc"));
	t32_tx_st(dev, 8, e->fops);
	if (t32_tx_os_misc_register(dev) < 0) {
		t32_tx_os_isp_printf(2, "Failed to register isp-ivdc!\n");
		return -2;
	}
	e->dev = dev;
	e->mode = e->link_mode;
	return 0;
}

#endif
