/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 frame-source platform probe (vendor tx_isp_fs_probe): the 304 byte
 * frame source (subdevice; +268 u16 output pad count, +272 pads (36 bytes:
 * +4 channel index, +5 enabled, +28 event handler), +288 channels, +292
 * count, +296 lock, +300 state), one 816 byte frame channel per pad
 * (+44 vb2 queue: +44 type 1, +48 mutex, +60 / +64 / +68 192 / 8192 / 2,
 * +536 / +548 lists, +556 lock, +560 wait queue; +716 pad, +720 index,
 * +724 mutex, +736 active, +740 completion or (fast start without PM)
 * +752 / +756 list), PM callbacks into the ops table cf558 (d9004), the
 * fast-start channel table f3370 (d9008) and the global d9950.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txfs_oracle).
 */
#ifndef TX_ISP_T32_TXFS_H
#define TX_ISP_T32_TXFS_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txcore.h"

#define T32_FS_BYTES		304U
#define T32_FS_NPAD		268U	/* u16 */
#define T32_FS_PADS		272U
#define T32_FS_CHANS		288U
#define T32_FS_NCHAN		292U
#define T32_FS_LOCK		296U
#define T32_FS_STATE		300U
#define T32_FS_PAD_BYTES	36U
#define T32_FS_CHAN_BYTES	816U
#define T32_FC_Q		44U
#define T32_FC_Q_BYTES		540U
#define T32_FC_PAD		716U
#define T32_FC_IDX		720U
#define T32_FC_MLOCK		724U
#define T32_FC_ACTIVE		736U
#define T32_FC_DONE		740U
#define T32_FS_GFP		0x24000c0U

struct t32_tx_fs_env {
	u32 pm_on, fast_start;			/* d9004, d9008 */
	u32 lock_key, ops, cfg;			/* t32_small_bss, cf558, cf4ec */
	u32 fast_chans;				/* f3370: u32[256] */
	u32 fs;					/* d9950 (out) */
};

/* provided by the driver glue (kernel) or by the oracle (host) */
u32 t32_tx_os_kmalloc(u32 n, u32 flags);
void t32_tx_os_kfree(u32 p);
void t32_tx_os_memset(u32 a, u32 c, u32 n);
void t32_tx_os_set_drvdata(u32 pdev, u32 data);
int t32_tx_os_subdev_init(u32 pdev, u32 sd, u32 ops);
void t32_tx_os_spin_lock_init(u32 lock);
void t32_tx_os_raw_mutex_init(u32 lock, const char *name, u32 key);
void t32_tx_os_init_completion(u32 c);
void t32_tx_os_init_waitqueue_head(u32 q);
int t32_tx_fs_suspend_module(u32 sd);
int t32_tx_fs_resume_module(u32 sd);
int t32_tx_fs_prepare_module(u32 sd);
int t32_tx_fs_complete_module(u32 sd);
int t32_tx_frame_chan_event(u32 pad, u32 ev, u32 arg);

/* one frame channel of pad i */
static inline void t32_tx_fs_chan_init(struct t32_tx_fs_env *e, u32 ch, u32 pad)
{
	u32 q = ch + T32_FC_Q;

	t32_tx_st(ch, T32_FC_PAD, pad);
	t32_tx_st(ch, T32_FC_IDX, *(volatile u8 *)T32_TX_P(pad + 4));
	if (!*(volatile u8 *)T32_TX_P(pad + 5)) {
		t32_tx_st(ch, T32_FC_ACTIVE, 0);
		return;
	}
	t32_tx_os_memset(q, 0, T32_FC_Q_BYTES);
	t32_tx_st(ch, 44, 1);
	t32_tx_st(ch, 68, 2);
	t32_tx_st(ch, 60, 192);
	t32_tx_st(ch, 64, 8192);
	t32_tx_st(ch, 536, ch + 536);
	t32_tx_st(ch, 540, ch + 536);
	t32_tx_st(ch, 548, ch + 548);
	t32_tx_st(ch, 552, ch + 548);
	t32_tx_os_spin_lock_init(ch + 556);
	t32_tx_os_raw_mutex_init(ch + 48, "&q->mlock", e->lock_key);
	t32_tx_st(ch, 544, 0);
	t32_tx_st(ch, 556, 0);
	t32_tx_os_init_waitqueue_head(ch + 560);
	t32_tx_os_spin_lock_init(ch + T32_FC_MLOCK);
	t32_tx_os_raw_mutex_init(ch + T32_FC_MLOCK, "&chan->mlock", e->lock_key);
	if (e->fast_start && !e->pm_on) {
		t32_tx_st(ch, 752, 0);
		t32_tx_st(ch, 756, ch + 756);
		t32_tx_st(ch, 760, ch + 756);
	} else {
		t32_tx_os_init_completion(ch + T32_FC_DONE);
	}
	t32_tx_st(pad, 28, T32_TX_A(t32_tx_frame_chan_event));
	t32_tx_st(ch, T32_FC_ACTIVE, 1);
	if (e->fast_start)
		t32_tx_st(e->fast_chans, 4 * t32_tx_ld(ch, T32_FC_IDX), ch);
}

/*
 * tx_isp_fs_probe(pdev).  Vendor: the channel array allocation is not
 * tested; the rollback for a NULL / ERR_PTR channel or pad never ends for
 * pad 0 and skips channel 0 (not reachable with a valid array, not
 * ported: ours stops with -EINVAL there).
 */
static inline int t32_tx_fs_probe(struct t32_tx_fs_env *e, u32 pdev)
{
	u32 fs = t32_tx_os_kmalloc(T32_FS_BYTES, T32_FS_GFP), pd, ops, n;
	int i;

	if (!fs) {
		t32_tx_os_isp_printf(2, "Failed to allocate csi device\n");
		return -12;
	}
	t32_tx_os_memset(fs, 0, T32_FS_BYTES);
	pd = t32_tx_ld(pdev, 88);
	if (e->pm_on) {
		ops = t32_tx_ld(e->ops, 16);
		t32_tx_st(ops, 12, T32_TX_A(t32_tx_fs_suspend_module));
		t32_tx_st(ops, 20, T32_TX_A(t32_tx_fs_resume_module));
		t32_tx_st(ops, 8, T32_TX_A(t32_tx_fs_prepare_module));
		t32_tx_st(ops, 24, T32_TX_A(t32_tx_fs_complete_module));
	}
	if (t32_tx_os_subdev_init(pdev, fs, e->ops)) {
		t32_tx_os_isp_printf(2, "Failed to init isp module(%d.%d)\n", *(volatile u8 *)T32_TX_P(pd + 2),
				     *(volatile u8 *)T32_TX_P(pd + 3));
		t32_tx_os_kfree(fs);
		return -12;
	}
	n = *(volatile u16 *)T32_TX_P(fs + T32_FS_NPAD);
	t32_tx_st(fs, T32_FS_NCHAN, n);
	if (n) {
		t32_tx_st(fs, T32_FS_CHANS, t32_tx_os_kmalloc(n * T32_FS_CHAN_BYTES, T32_FS_GFP));
		t32_tx_os_memset(t32_tx_ld(fs, T32_FS_CHANS), 0, T32_FS_CHAN_BYTES * t32_tx_ld(fs, T32_FS_NCHAN));
		for (i = 0; i < (int)t32_tx_ld(fs, T32_FS_NCHAN); i++) {
			u32 ch = t32_tx_ld(fs, T32_FS_CHANS) + (u32)i * T32_FS_CHAN_BYTES;
			u32 pad = t32_tx_ld(fs, T32_FS_PADS) + (u32)i * T32_FS_PAD_BYTES;

			if (!t32_tx_sd_ok(ch) || !t32_tx_sd_ok(pad))
				return -22;
			t32_tx_fs_chan_init(e, ch, pad);
		}
	}
	t32_tx_st(fs, T32_FS_STATE, 1);
	t32_tx_os_set_drvdata(pdev, fs);
	t32_tx_st(fs, 56, e->cfg);
	t32_tx_st(fs, 280, fs);
	t32_tx_os_spin_lock_init(fs + T32_FS_LOCK);
	e->fs = fs;
	t32_tx_st(fs, T32_FS_LOCK, 0);
	return 0;
}

#endif
