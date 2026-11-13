/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp platform probe (vendor tx_isp_probe with the inlined
 * tx_isp_match_and_register_platforms): the 568 byte ISP device, the
 * module platform devices of the platform data (+4 u8 count < 17, +8
 * device table) matched by name against the 7 vendor drivers (bed60:
 * +0 probe hook, +20 name, +28 owner from ce22c) and registered into the
 * ISP's platform table (+160 + 8 i: device, driver; +156 count), the
 * module init, the "tx-isp" misc device (+12; fops cf6d8 at +52 / +20,
 * minor 71 with fast start), /proc/jz/isp (+564), the module graph, the
 * memory init, the version (+288), the fast-start sensor start (d9008 1:
 * f3760 entries of f384c, +32 init; 2: add_sensor_chose) and the
 * f3834 hook, then the RISC-V probe / run.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txisp_oracle).
 */
#ifndef TX_ISP_T32_TXISP_H
#define TX_ISP_T32_TXISP_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txcore.h"
#include "tx_isp_t32_txgraph.h"
#include "tx_isp_t32_txinit.h"

#define T32_ISP_BYTES		568U
#define T32_ISP_LOCK		304U
#define T32_ISP_VERSION		288U
#define T32_ISP_MAX_PLAT	17U
#define T32_DRV_N		7U
#define T32_DRV_PROBE		0U
#define T32_DRV_NAME		20U
#define T32_DRV_OWNER		28U
#define T32_ISP_FAST_MINOR	71U
#define T32_SNS_INIT_BYTES	40U
#define T32_SNS_INIT_FN		32U
#define T32_ISP_GFP		0x24000c0U

struct t32_tx_isp_env {
	u32 fast_start;		/* d9008 */
	u32 drivers;		/* bed60: 7 driver pointers */
	u32 owner;		/* ce22c */
	u32 fops;		/* cf6d8 */
	u32 nsensor;		/* f3760 */
	u32 sensors;		/* f384c: 40 byte entries, +32 init */
	u32 sensor_one;		/* f3764 */
	u32 start_hook;		/* f3834: int (*)(void) or 0 */
	u32 isp, fast_isp;	/* da784, f35c0 (out) */
};

typedef int (*t32_tx_pdev_fn)(u32 pdev);

/* provided by the driver glue (kernel) or by the oracle (host) */
u32 t32_tx_os_kmalloc(u32 n, u32 flags);
void t32_tx_os_kfree(u32 p);
void t32_tx_os_memset(u32 a, u32 c, u32 n);
int t32_tx_os_platform_device_register(u32 pdev);
void t32_tx_os_spin_lock_init(u32 lock);
void t32_tx_os_set_drvdata(u32 pdev, u32 data);
u32 t32_tx_os_jz_proc_mkdir(const char *name);
void t32_tx_os_proc_remove(u32 p);
void t32_tx_os_isp_mem_init(void);
int t32_tx_os_add_sensor_chose(void);
void t32_tx_os_riscv_probe(void);

/* tx_isp_match_and_register_platforms (inlined): 0 or the register error */
static inline int t32_tx_isp_match_platforms(struct t32_tx_isp_env *e, u32 isp, u32 pd)
{
	u32 n = t32_tx_b(pd + 4), i, k;

	for (i = 0; (int)i < (int)t32_tx_b(pd + 4); i++) {
		u32 pdev = t32_tx_ld(t32_tx_ld(pd, 8), 4 * i), ent = isp + T32_ISP_PLAT(i);

		t32_tx_st(ent, 0, pdev);
		for (k = 0; k < T32_DRV_N; k++) {
			u32 drv = t32_tx_ld(e->drivers, 4 * k), fn;
			int r;

			t32_tx_st(drv, T32_DRV_OWNER, t32_tx_ld(e->owner, 0));
			if (strcmp((const char *)T32_TX_P(t32_tx_ld(pdev, 0)),
				   (const char *)T32_TX_P(t32_tx_ld(drv, T32_DRV_NAME))))
				continue;
			r = t32_tx_os_platform_device_register(pdev);
			if (r) {
				/* vendor: the device number is a stale stack word */
				t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to register isp device(%d)\n",
						     "tx_isp_match_and_register_platforms", 2863, i);
				t32_tx_st(ent, 0, 0);
				return r;
			}
			fn = t32_tx_ld(drv, T32_DRV_PROBE);
			if (fn)
				((t32_tx_pdev_fn)T32_TX_P(fn))(pdev);
			t32_tx_st(ent, 4, drv);
			break;
		}
	}
	t32_tx_st(isp, T32_ISP_NPLAT, n);
	return 0;
}

/* tx_isp_probe(pdev) */
static inline int t32_tx_isp_probe(struct t32_tx_isp_env *e, u32 pdev)
{
	u32 isp = t32_tx_os_kmalloc(T32_ISP_BYTES, T32_ISP_GFP), pd, proc;
	int r, i;

	if (!isp) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to allocate camera device\n", "tx_isp_probe", 3294);
		return -12;
	}
	t32_tx_os_memset(isp, 0, T32_ISP_BYTES);
	pd = t32_tx_ld(pdev, T32_PDEV_DATA);
	if (!pd) {
		t32_tx_os_kfree(isp);
		return -(int)T32_EINVAL;
	}
	if ((u32)t32_tx_b(pd + 4) >= T32_ISP_MAX_PLAT) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] The number of platform device is too much!\n",
				     "tx_isp_match_and_register_platforms", 2851);
		t32_tx_os_kfree(isp);
		return -(int)T32_EINVAL;
	}
	r = t32_tx_isp_match_platforms(e, isp, pd);
	if (r)
		goto unreg;
	t32_tx_os_spin_lock_init(isp + T32_ISP_LOCK);
	if (t32_tx_isp_module_init(pdev, isp)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to init isp module(%d.%d)\n", "tx_isp_probe", 3312,
				     t32_tx_b(pd + 2), t32_tx_b(pd + 3));
		r = -12;
		goto unreg;
	}
	t32_tx_st(isp, T32_SD_MISC_FOPS, e->fops);
	t32_tx_st(isp, T32_SD_MISC, e->fast_start ? T32_ISP_FAST_MINOR : 255U);
	t32_tx_st(isp, T32_SD_MISC + 8, e->fops);
	t32_tx_st(isp, T32_SD_MISC + 4, T32_TX_A("tx-isp"));
	r = t32_tx_os_misc_register(isp + T32_SD_MISC);
	if (r < 0) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to register tx-isp miscdev!\n", "tx_isp_probe", 3330);
		r = -2;
		goto deinit;
	}
	proc = t32_tx_os_jz_proc_mkdir("isp");
	t32_tx_st(isp, T32_ISP_PROCDIR, proc);
	if (!proc) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to create debug directory of tx-isp!\n", "tx_isp_probe", 3337);
		goto misc;	/* vendor: returns the (non-negative) misc_register result */
	}
	t32_tx_os_set_drvdata(pdev, isp);
	e->isp = isp;
	r = t32_tx_create_graph_and_nodes(isp);
	if (r) {
		t32_tx_os_proc_remove(t32_tx_ld(isp, T32_ISP_PROCDIR));
		goto misc;
	}
	t32_tx_os_isp_mem_init();
	t32_tx_st(isp, T32_ISP_VERSION, T32_TX_A("H20260618a"));
	t32_tx_os_isp_printf(1, "@@@@ tx-isp-probe ok (version %s), compiler date: %s %s @@@@@\n", "H20260618a",
			     "Jun 23 2026", "14:41:04");
	if (e->fast_start == 1) {
		e->fast_isp = isp;
		for (i = 0; i < (int)e->nsensor; i++) {
			/* vendor: the sensor name is a stale stack word */
			t32_tx_os_isp_printf(1, "LIB[%s.%d]sensor id:%d sensor name:%s\n", "tx_isp_probe", 3360, i,
					     (const char *)T32_TX_P(0));
			r = ((t32_tx_void_fn)T32_TX_P(t32_tx_ld(e->sensors + (u32)i * T32_SNS_INIT_BYTES, T32_SNS_INIT_FN)))();
			if (r < 0) {
				t32_tx_os_isp_printf(1, "init_sensor %d failed\n", i);
				return r;
			}
		}
	} else if (e->fast_start == 2) {
		t32_tx_os_isp_printf(1, "LIB:[%s.%d]use_num_sensor = %d \n", "tx_isp_probe", 3368, e->sensor_one);
		r = t32_tx_os_add_sensor_chose();
		if (r < 0) {
			t32_tx_os_isp_printf(1, "init_sensor_one failed\n");
			return r;
		}
	}
	if (e->fast_start && e->start_hook) {
		r = ((t32_tx_void_fn)T32_TX_P(e->start_hook))();
		if (r < 0) {
			t32_tx_os_isp_printf(1, "start_tx_isp_device failed\n");
			return r;
		}
	}
	t32_tx_os_riscv_probe();
	t32_tx_os_riscv_run();
	return 0;
misc:
	t32_tx_os_misc_deregister(isp + T32_SD_MISC);
deinit:
	t32_tx_isp_module_deinit(isp);
unreg:
	t32_tx_isp_unregister_platforms(isp + T32_ISP_PLAT(0));
	t32_tx_os_kfree(isp);
	return r;
}

#endif
