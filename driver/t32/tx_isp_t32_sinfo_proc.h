/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 sensor-info proc files of the vendor module (Matt Davis' recovered C,
 * PR #15): sinfo_open (0xc30), sinfo_count_open (0xc74), sinfo_count_show
 * (0xc90) and sinfo_slot_publish (0xe5c).
 *
 * Four sensor slots of 160 bytes (t32_data_d9024, byte 0 = slot in use) under
 * one mutex (t32_data_ce270); the count file prints the number of used slots.
 * A slot publishes /proc/<root>/sensor<idx>/ (root t32_data_d9020) with the
 * 15 files of t32_sinfo_names (mode 0444, the vendor's sinfo fops), each with
 * its { slot, index } entry at slot +40 as the proc data.  The slot keeps the
 * directory at +20 and its name at +24 (16 bytes).
 * The OS calls are hooks; the vendor's return value of slot_publish is a
 * leftover register (void here).  The show function itself (sinfo_show) is
 * not part of this block (os->sinfo_show).
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_sinfo_oracle).
 */
#ifndef TX_ISP_T32_SINFO_PROC_H
#define TX_ISP_T32_SINFO_PROC_H

#include "tx_isp_t32_glue.h"
#ifndef __KERNEL__
#include <stdio.h>
#endif

#define T32_SINFO_SLOTS		4U
#define T32_SINFO_SLOT_BYTES	160U
#define T32_SINFO_FILES		15U
#define T32_SINFO_MODE		0444U
#define T32_SLOT_DIR		20U
#define T32_SLOT_NAME		24U
#define T32_SLOT_ENTRY(i)	(40U + (i) * 8U)

static const char *const t32_sinfo_names[T32_SINFO_FILES] = {
	"name", "chip_id", "i2c_addr", "i2c_adapter", "width", "height", "fps", "status",
	"min_fps", "max_fps", "mclk", "boot", "video_interface", "rst_gpio", "pwdn_gpio",
};

struct t32_sinfo_os {
	u8 *slots;				/* t32_data_d9024 */
	u8 *mutex;				/* t32_data_ce270 */
	u32 *root;				/* t32_data_d9020: proc root dir */
	const void *fops;			/* t32_data_bd5f8 */
	u32 sinfo_show, count_show;		/* show callbacks handed to single_open */
	void (*lock)(u8 *m);			/* mutex_lock */
	void (*unlock)(u8 *m);
	void *(*pde_data)(void *inode);		/* PDE_DATA */
	int (*single_open)(void *file, u32 show, void *data);
	int (*seq_printf)(void *seq, const char *fmt, u32 v);
	u32 (*proc_mkdir)(const char *name, u32 parent);
	u32 (*proc_create_data)(const char *name, u32 mode, u32 parent, const void *fops, void *data);
};

static inline int t32_sinfo_open(const struct t32_sinfo_os *os, void *inode, void *file)
{
	return os->single_open(file, os->sinfo_show, os->pde_data(inode));
}

static inline int t32_sinfo_count_open(const struct t32_sinfo_os *os, void *inode, void *file)
{
	(void)inode;
	return os->single_open(file, os->count_show, 0);
}

static inline int t32_sinfo_count_show(const struct t32_sinfo_os *os, void *seq, void *v)
{
	u32 n = 0, i;

	(void)v;
	os->lock(os->mutex);
	for (i = 0; i < T32_SINFO_SLOTS; i++)
		n += os->slots[i * T32_SINFO_SLOT_BYTES];
	os->unlock(os->mutex);
	os->seq_printf(seq, "%d\n", n);
	return 0;
}

static inline void t32_sinfo_slot_publish(const struct t32_sinfo_os *os, u8 *slot, u32 idx)
{
	u32 dir, i;

	snprintf((char *)slot + T32_SLOT_NAME, 16, "sensor%d", (int)idx);
	dir = os->proc_mkdir((char *)slot + T32_SLOT_NAME, *os->root);
	t32_g_wr32(slot, T32_SLOT_DIR, dir);
	if (!dir)
		return;
	for (i = 0; i < T32_SINFO_FILES; i++) {
		t32_g_wr32(slot, T32_SLOT_ENTRY(i) + 4, i);
		t32_g_wr32(slot, T32_SLOT_ENTRY(i), T32_WORD(slot));
		os->proc_create_data(t32_sinfo_names[i], T32_SINFO_MODE, t32_g_rd32(slot, T32_SLOT_DIR),
				     os->fops, slot + T32_SLOT_ENTRY(i));
	}
}

#endif
