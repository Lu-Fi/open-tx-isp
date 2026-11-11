/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IQ bin loading of the vendor tisp core: tisp_load_parameters (0x278b8).
 *
 * Arguments: ch (low byte), info = the sensor/bin request (vendor layout):
 *   +20 sensor name, +48 mode word, +52 + 4 * i a word per bin, +124 bin
 *   count, +125 selected bin, +126 + i "bin i loaded" flags (i = 0..3),
 *   +132 + 128 * i the path of bin i.
 * Vendor globals (struct t32_loadpar_glb): the common block da850 (336 B per
 * channel; +0 first loaded path, +224 channel count, +232 the 14 size bytes of
 * the LUT info, +296 selected bin, +300 its info word), the channel state
 * dc284 (904 B per channel, +126 + i flags), the tuning base dd0a4[ch], the
 * per mode bin images dd0b4[ch * 6 + mode], the LUT info dc274[ch] and the
 * table sizes dc1a4[ch * 7 + i].
 *
 * Mode 0: the four bins day / night / day-wdr / night-wdr are tried in that
 * order (path "/etc/sensor/<name>-PRJ007-<kind>.bin"); an existing file that
 * reads well counts, sets its flags and its path, and becomes the common
 * block's path if that is still empty.  Mode 1: the count paths already in
 * info are tried again (a read error ends the loop).  Other modes load
 * nothing.  Then the selected bin must be loaded: its image is copied into
 * the tuning base (0xf238 bytes), the LUT info (44 B, allocated once) gets
 * seven freshly allocated tables, tisp_lut_params_copy runs and the common
 * block takes the sizes, the bin index and the bin's info word.
 * File, allocator, LUT copy and print calls go through struct t32_loadpar_ops.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK): when the
 * night bin does not exist the channel state flag +127 is set to 1 (the info
 * flag to 0; every other missing bin clears both); the LUT info allocation
 * and the last five table allocations are not checked (NULL is stored and
 * used later); the table buffers of an earlier call are not freed (leak per
 * call); the selected bin index is not range checked (flags and info words
 * past the 4 bins are read).  The 128 byte path copies take the whole
 * snprintf buffer: behind the text are the tails of the earlier (longer)
 * paths of the same call and, in the vendor, uninitialised stack bytes; ours
 * starts from a zeroed buffer (the reference model's zeroed stack).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_loadpar_oracle).
 */
#ifndef TX_ISP_T32_LOADPAR_H
#define TX_ISP_T32_LOADPAR_H

#include "tx_isp_t32_param.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#include <stdio.h>
#endif

#define T32_LP_COM_SIZE		336U	/* da850[ch] */
#define T32_LP_CHS_SIZE		904U	/* dc284[ch] */
#define T32_LP_BIN_SIZE		0xf238U	/* bin image copied into the tuning base */
#define T32_LP_PATH		128U

/* info (the request) */
#define T32_LP_NAME		20
#define T32_LP_MODE		48
#define T32_LP_WORD		52
#define T32_LP_COUNT		124
#define T32_LP_SEL		125
#define T32_LP_FLAG		126
#define T32_LP_PATHS		132

/* common block */
#define T32_LP_COM_PATH		0
#define T32_LP_COM_NCH		224
#define T32_LP_COM_SIZES	232
#define T32_LP_COM_SEL		296
#define T32_LP_COM_WORD		300

struct t32_loadpar_glb {
	u8 *com;		/* da850 */
	u8 *chs;		/* dc284 */
	u32 *tuning;		/* dd0a4[ch] */
	const u32 *bins;	/* dd0b4[ch * 6 + mode] */
	u32 *lut;		/* dc274[ch] -> struct t32_lut_info */
	const u32 *lut_size;	/* dc1a4[ch * 7 + i] */
};

struct t32_loadpar_ops {
	int (*file_exist)(const char *path);			/* lib_tisp_is_file_exist */
	int (*read_bin)(u32 ch, const char *path, u32 idx);	/* tisp_read_bin_file, 0 = ok */
	void (*lut_copy)(u32 ch, u32 sel);			/* tisp_lut_params_copy */
	void *(*vmalloc)(u32 size);
	void (*printk)(const char *fmt, const char *func, u32 line);
	void (*isp_printf)(u32 level, const char *fmt, const char *func, u32 line);
};

static const char *const t32_lp_fmt[4] = {
	"/etc/sensor/%s-PRJ007-day.bin",
	"/etc/sensor/%s-PRJ007-night.bin",
	"/etc/sensor/%s-PRJ007-day-wdr.bin",
	"/etc/sensor/%s-PRJ007-night-wdr.bin",
};

static inline void t32_lp_put32(u8 *p, u32 v)
{
	memcpy(p, &v, 4);
}

static inline u32 t32_lp_get32(const u8 *p)
{
	u32 v;

	memcpy(&v, p, 4);
	return v;
}

/* a bin that read well: flags, and the path becomes the channel's path when it has none yet */
static inline void t32_lp_mark(const struct t32_loadpar_glb *g, u8 *info, u32 ch, u32 i, const char *path)
{
	info[T32_LP_FLAG + i] = 1;
	g->chs[ch * T32_LP_CHS_SIZE + T32_LP_FLAG + i] = 1;
	if (!g->com[ch * T32_LP_COM_SIZE + T32_LP_COM_PATH])
		memcpy(g->com + ch * T32_LP_COM_SIZE + T32_LP_COM_PATH, path, T32_LP_PATH);
}

static inline int t32_load_parameters(const struct t32_loadpar_glb *g, const struct t32_loadpar_ops *o, u32 ch,
				      u8 *info)
{
	static const u8 tab_of[7] = { 0, 1, 2, 4, 3, 5, 6 };	/* dc1a4 entry i -> LUT table */
	struct t32_lut_info *lut;
	u32 mode = t32_lp_get32(info + T32_LP_MODE), i, sel, idx;
	char path[T32_LP_PATH] = { 0 };	/* vendor: uninitialised stack, see the header */

	ch &= 0xff;
	if (!(ch < g->com[T32_LP_COM_NCH]))
		g->com[T32_LP_COM_NCH] = (u8)(ch + 1);
	if (mode == 0) {
		for (i = 0; i < 4; i++) {
			snprintf(path, sizeof(path), t32_lp_fmt[i], (const char *)info + T32_LP_NAME);
			if (!o->file_exist(path)) {
				info[T32_LP_FLAG + i] = 0;
				g->chs[ch * T32_LP_CHS_SIZE + T32_LP_FLAG + i] = i == 1;
				continue;
			}
			if (o->read_bin(ch, path, i))
				continue;
			info[T32_LP_COUNT]++;
			memcpy(info + T32_LP_PATHS + T32_LP_PATH * i, path, T32_LP_PATH);
			t32_lp_mark(g, info, ch, i, path);
		}
	} else if (mode == 1) {
		for (i = 0; i < info[T32_LP_COUNT]; i = (i + 1) & 0xff) {
			const char *p = (const char *)info + T32_LP_PATHS + T32_LP_PATH * i;

			if (!o->file_exist(p))
				continue;
			if (o->read_bin(ch, p, i))
				break;
			t32_lp_mark(g, info, ch, i, p);
		}
	}

	sel = info[T32_LP_SEL];
	if (info[T32_LP_FLAG + sel] != 1) {
		o->printk("[ISP][%s, %d]: The BIN file used did not load successfully \n", "tisp_load_parameters",
			  1436);
		return -1;
	}
	idx = ch * 6 + sel;
	memcpy(t32_lut_ptr(g->tuning[ch]), t32_lut_ptr(g->bins[idx]), T32_LP_BIN_SIZE);
	if (!g->lut[ch])
		g->lut[ch] = (u32)(uintptr_t)o->vmalloc(sizeof(struct t32_lut_info));
	lut = t32_lut_ptr(g->lut[ch]);
	for (i = 0; i < 7; i++) {
		lut->tab[tab_of[i]] = (u32)(uintptr_t)o->vmalloc(g->lut_size[ch * 7 + i]);
		if (i < 2 && !lut->tab[tab_of[i]]) {
			o->isp_printf(2, "[%s %d]: vmalloc error !!!\n", "tisp_load_parameters", i ? 1423 : 1418);
			return -1;
		}
	}
	o->lut_copy(ch, info[T32_LP_SEL]);
	memcpy(g->com + ch * T32_LP_COM_SIZE + T32_LP_COM_SIZES, lut, 14);
	t32_lp_put32(g->com + ch * T32_LP_COM_SIZE + T32_LP_COM_SEL, info[T32_LP_SEL]);
	t32_lp_put32(g->com + ch * T32_LP_COM_SIZE + T32_LP_COM_WORD,
		     t32_lp_get32(info + T32_LP_WORD + 4 * info[T32_LP_SEL]));
	return 0;
}

#endif /* TX_ISP_T32_LOADPAR_H */
