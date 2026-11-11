/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core platform probe (vendor tx_isp_core_probe with the inlined
 * isp_core_output_channel_init): the 4292 byte core device (subdevice;
 * +288 lock, +300..+308 states, +740 platform data, +1132 / +1136 output
 * channels (284 bytes) / count, +1196 sensors, +1704 tuning, +4024..+4040
 * switch state and the wqSwitchNormal wait queue (+4028, PM only)), the PM
 * callbacks and event callbacks when d9004 is set, the output channels
 * (+116 index, +120 index % 3, +128 enabled, +132 pad, +136 core, +140 /
 * +144 maximum size, +148 / +152 minimum 128, +156 / +157 flags, +168
 * lock), the tuning init, the globals f3310 / f331c.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txcprobe_oracle).
 */
#ifndef TX_ISP_T32_TXCPROBE_H
#define TX_ISP_T32_TXCPROBE_H

#include "tx_isp_t32_txos.h"

#define T32_CORE_BYTES		4292U
#define T32_CORE_GFP		0x24000c0U
#define T32_CCH_BYTES		284U
#define T32_CCH_PAD_BYTES	36U

struct t32_tx_core_env {
	u32 pm_on;		/* d9004 */
	u32 lock_key;		/* t32_small_bss */
	u32 ops, cfg;		/* d8c28, d8bbc */
	u32 done;		/* f331c: completion */
	u32 cache;		/* kmalloc_caches[7] */
	u32 core;		/* f3310 (out) */
};

/* provided by the driver glue (kernel) or by the oracle (host) */
u32 t32_tx_os_kmalloc(u32 n, u32 flags);
void t32_tx_os_kfree(u32 p);
void t32_tx_os_kfree_plain(u32 p);
void t32_tx_os_memset(u32 a, u32 c, u32 n);
void t32_tx_os_init_completion(u32 c);
void t32_tx_os_core_event_init(u32 a0);
void t32_tx_os_core_event_set_cb(u32 a0, u32 ev, u32 fn);
u32 t32_tx_os_kmem_cache_alloc(u32 cache, u32 flags);
void t32_tx_os_init_waitqueue_head_named(u32 q, const char *name, u32 key);
int t32_tx_os_subdev_init(u32 pdev, u32 sd, u32 ops);
void t32_tx_os_subdev_deinit(u32 sd);
void t32_tx_os_spin_lock_init(u32 lock);
void t32_tx_os_raw_mutex_init(u32 lock, const char *name, u32 key);
u32 t32_tx_os_core_tuning_init(u32 core);
void t32_tx_os_ispcore_slake_module(u32 core);
void t32_tx_os_set_drvdata(u32 pdev, u32 data);
void t32_tx_os_sensor_early_init(u32 core);
int t32_tx_ispcore_prepare_module(u32 sd);
int t32_tx_ispcore_suspend_module(u32 sd);
int t32_tx_ispcore_resume_early_module(u32 sd);
int t32_tx_ispcore_resume_module(u32 sd);
int t32_tx_ispcore_complete_module(u32 sd);
int t32_tx_isp_event_sleep_process(u32 a);
int t32_tx_sensor_event_prepare_process(u32 a);
int t32_tx_sensor_event_suspend_process(u32 a);
int t32_tx_sensor_event_resume_process(u32 a);
int t32_tx_sensor_event_complete_process(u32 a);
int t32_tx_sensor_event_streamoff_process(u32 a);
int t32_tx_ispcore_pad_event_handle(u32 pad, u32 ev, u32 arg);

/* the frame size limits of output channel i (vendor isp_core_output_channel_init) */
static inline void t32_tx_core_chan_limits(u32 ch, u32 i)
{
	if (i == 1 || i == 4) {
		t32_tx_st(ch, 140, 1920);
		t32_tx_st(ch, 144, 1080);
		t32_tx_wb(ch + 156, 1);
		t32_tx_wb(ch + 157, 1);
	} else if (i == 0 || i == 3) {
		t32_tx_st(ch, 140, 2624);
		t32_tx_st(ch, 144, 2048);
		t32_tx_wb(ch + 156, 1);
		t32_tx_wb(ch + 157, 0);
	}
	t32_tx_st(ch, 148, 128);
	t32_tx_st(ch, 152, 128);
}

/* PM-only cleanup and the core free */
static inline int t32_tx_core_probe_free(struct t32_tx_core_env *e, u32 core, int r)
{
	if (e->pm_on && t32_tx_ld(core, 4028)) {
		t32_tx_os_kfree_plain(t32_tx_ld(core, 4028));
		t32_tx_st(core, 4028, 0);
	}
	t32_tx_os_kfree(core);
	return r;
}

/* tx_isp_core_probe(pdev) */
static inline int t32_tx_core_probe(struct t32_tx_core_env *e, u32 pdev)
{
	u32 core = t32_tx_os_kmalloc(T32_CORE_BYTES, T32_CORE_GFP), pd, ops, n, chans, i, t;

	if (!core) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to allocate sensor device\n", "tx_isp_core_probe", 4703);
		return -12;
	}
	t32_tx_os_memset(core, 0, T32_CORE_BYTES);
	t32_tx_os_init_completion(e->done);
	pd = t32_tx_ld(pdev, 88);
	if (e->pm_on) {
		ops = t32_tx_ld(e->ops, 16);
		t32_tx_st(ops, 8, T32_TX_A(t32_tx_ispcore_prepare_module));
		t32_tx_st(ops, 12, T32_TX_A(t32_tx_ispcore_suspend_module));
		t32_tx_st(ops, 16, T32_TX_A(t32_tx_ispcore_resume_early_module));
		t32_tx_st(ops, 20, T32_TX_A(t32_tx_ispcore_resume_module));
		t32_tx_st(ops, 24, T32_TX_A(t32_tx_ispcore_complete_module));
		t32_tx_os_core_event_init(0);
		t32_tx_os_core_event_set_cb(0, 1, T32_TX_A(t32_tx_isp_event_sleep_process));
		t32_tx_os_core_event_set_cb(0, 5, T32_TX_A(t32_tx_sensor_event_prepare_process));
		t32_tx_os_core_event_set_cb(0, 6, T32_TX_A(t32_tx_sensor_event_suspend_process));
		t32_tx_os_core_event_set_cb(0, 7, T32_TX_A(t32_tx_sensor_event_resume_process));
		t32_tx_os_core_event_set_cb(0, 8, T32_TX_A(t32_tx_sensor_event_complete_process));
		t32_tx_os_core_event_set_cb(0, 2, T32_TX_A(t32_tx_sensor_event_streamoff_process));
		t32_tx_st(core, 4024, 0);
		t32_tx_st(core, 4028, t32_tx_os_kmem_cache_alloc(e->cache, T32_CORE_GFP));
		if (!t32_tx_ld(core, 4028)) {
			t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to init isp\n", "tx_isp_core_probe", 4732);
			return t32_tx_core_probe_free(e, core, -12);
		}
		t32_tx_os_init_waitqueue_head_named(t32_tx_ld(core, 4028), "core_dev->wqSwitchNormal", e->lock_key);
	}
	if (t32_tx_os_subdev_init(pdev, core, e->ops)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to init isp module(%d.%d)\n", "tx_isp_core_probe", 4741,
				     t32_tx_b(pd + 2), t32_tx_b(pd + 3));
		return t32_tx_core_probe_free(e, core, -12);
	}
	t32_tx_os_spin_lock_init(core + 288);
	t32_tx_os_raw_mutex_init(core + 288, "&core_dev->mlock", e->lock_key);
	n = t32_tx_h(core + 268);
	t32_tx_st(core, 1136, n);
	t32_tx_st(core, 740, t32_tx_ld(pdev, 88));
	chans = t32_tx_os_kmalloc(n * T32_CCH_BYTES, T32_CORE_GFP);
	if (!chans) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to allocate sensor device\n", "isp_core_output_channel_init", 4445);
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to init output channels!\n", "tx_isp_core_probe", 4754);
		goto deinit;
	}
	t32_tx_os_memset(chans, 0, t32_tx_h(core + 268) * T32_CCH_BYTES);
	for (i = 0; i < t32_tx_ld(core, 1136); i++) {
		u32 ch = chans + i * T32_CCH_BYTES, pad = t32_tx_ld(core, 272) + i * T32_CCH_PAD_BYTES;

		t32_tx_st(ch, 116, i);
		t32_tx_st(ch, 120, (u32)((int)i % 3));
		t32_tx_st(ch, 132, pad);
		if (!t32_tx_b(pad + 5)) {
			t32_tx_st(ch, 128, 0);
			continue;
		}
		t32_tx_core_chan_limits(ch, i);
		t32_tx_st(ch, 128, 1);
		t32_tx_os_spin_lock_init(ch + 168);
		t32_tx_st(ch, 136, core);
		t32_tx_st(pad, 28, T32_TX_A(t32_tx_ispcore_pad_event_handle));
		t32_tx_st(pad, 32, ch);
	}
	t32_tx_st(core, 1132, chans);
	t = t32_tx_os_core_tuning_init(core);
	t32_tx_st(core, 1704, t);
	if (!t) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to init tuning module!\n", "tx_isp_core_probe", 4761);
		if ((int)t32_tx_ld(core, 300) >= 2)
			t32_tx_os_ispcore_slake_module(core);
		t32_tx_os_kfree(t32_tx_ld(core, 1132));
		t32_tx_st(core, 1140, 1);
		t32_tx_st(core, 1132, 0);
		goto deinit;
	}
	t32_tx_st(core, 300, 1);
	t32_tx_st(core, 304, 1);
	t32_tx_st(core, 308, 1);
	t32_tx_st(core, 1196, 1);
	t32_tx_st(core, 4036, core + 4036);
	t32_tx_st(core, 4040, core + 4036);
	t32_tx_st(core, 1200, 0);
	t32_tx_st(core, 4020, 0);
	t32_tx_st(core, 4032, 0);
	t32_tx_os_set_drvdata(pdev, core);
	t32_tx_st(core, 280, core);
	t32_tx_st(core, 52, t32_tx_ld(t, 148));
	t32_tx_st(core, 56, e->cfg);
	e->core = core;
	t32_tx_os_sensor_early_init(core);
	return 0;
deinit:
	t32_tx_os_subdev_deinit(core);
	return t32_tx_core_probe_free(e, core, -22);
}

#endif
