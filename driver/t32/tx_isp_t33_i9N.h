/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) demosaic refresh: tisp_dmsc_par_refresh(ch, gain, threshold).
 *
 * The vendor skips the work when a previous gain is stored (state word 671, -1 = none)
 * and the new gain is closer than the threshold.  Otherwise: bin copy, gain
 * interpolation, tisp_ratio on five words, func_dmsc_para_update, and, unless
 * tisp_rcfg_get_dma_state(9) reports busy, the register writes of list 9
 * (slot index, or the register address when dmscCfgMode is 0), the 0x8010 trigger
 * and tispDmscRefresh = 1 (2 when busy).  The new gain is stored in both cases.
 */
#ifndef TX_ISP_T33_I9N_H
#define TX_ISP_T33_I9N_H

#include "tx_isp_t33_dmsc.h"

#ifndef T33_I9N_GLUE
extern u32 *dmscInfo[4];
extern u8 dmscCfgMode;
extern u8 tispDmscRefresh;
extern u32 tisp_ratio(u32 r, u32 v, u32 limit);
extern int tisp_rcfg_get_dma_state(u32 id);
extern int tisp_reg_write_t33(u32 ch, u32 blk, u32 reg, u32 val);
#endif

struct t33_dmsc_wr {
	u16 slot;
	u16 addr;
};

/* first pass of the vendor's writes */
static const struct t33_dmsc_wr t33_dmsc_wr_a[] = {
	{ 4, 0x8044 }, { 5, 0x8048 }, { 6, 0x804c }, { 7, 0x8050 }, { 8, 0x8054 },
	{ 9, 0x8058 }, { 10, 0x805c }, { 11, 0x8060 }, { 12, 0x8064 }, { 13, 0x8068 },
	{ 14, 0x806c }, { 15, 0x8070 }, { 16, 0x8074 }, { 17, 0x8078 }, { 18, 0x807c },
	{ 19, 0x8080 }, { 21, 0x8094 }, { 22, 0x8098 }, { 24, 0x80ac }, { 25, 0x80b0 },
	{ 26, 0x80b4 }, { 27, 0x80b8 }, { 28, 0x80bc }, { 29, 0x80c0 }, { 30, 0x80c4 },
	{ 32, 0x80cc }, { 33, 0x80d0 }, { 34, 0x80d4 }, { 37, 0x80e0 }, { 38, 0x80e4 },
	{ 121, 0x80e8 }, { 122, 0x80e8 }, { 123, 0x80e8 }, { 124, 0x80e8 },
	{ 126, 0x8108 }, { 127, 0x8108 }, { 128, 0x8108 }, { 129, 0x8108 }, { 130, 0x8108 },
	{ 131, 0x8108 }, { 132, 0x8108 },
	{ 134, 0x8124 }, { 135, 0x8124 }, { 136, 0x8124 }, { 137, 0x8124 }, { 138, 0x8124 },
	{ 139, 0x8124 }, { 140, 0x8124 },
	{ 40, 0x817c }, { 43, 0x818c }, { 44, 0x8190 }, { 45, 0x8194 }, { 46, 0x8198 },
	{ 49, 0x81c8 }, { 52, 0x81dc }, { 54, 0x81e4 }, { 55, 0x81e8 }, { 58, 0x8200 },
	{ 60, 0x8208 }, { 61, 0x820c }, { 62, 0x8210 }, { 64, 0x8218 }, { 67, 0x8224 },
	{ 68, 0x8228 }, { 142, 0x822c }, { 143, 0x822c }, { 70, 0x8254 }, { 71, 0x8258 },
	{ 75, 0x8284 }, { 76, 0x8288 }, { 77, 0x828c }, { 78, 0x8290 }, { 79, 0x8294 },
	{ 80, 0x8298 }, { 81, 0x829c }, { 82, 0x82a0 }, { 83, 0x82a4 }, { 84, 0x82a8 },
	{ 85, 0x82ac }, { 86, 0x82b0 }, { 87, 0x82b4 }, { 88, 0x82b8 }, { 89, 0x82bc },
	{ 90, 0x82c0 }, { 92, 0x82d4 }, { 93, 0x82d8 }, { 95, 0x82e0 }, { 96, 0x82e4 },
	{ 97, 0x82e8 }, { 98, 0x82ec }, { 99, 0x82f0 }, { 100, 0x82f4 }, { 102, 0x8304 },
	{ 104, 0x8324 }, { 105, 0x8328 }, { 106, 0x832c }, { 107, 0x8330 }, { 108, 0x8334 },
	{ 109, 0x8338 }, { 110, 0x833c }, { 111, 0x8340 }, { 112, 0x8344 }, { 113, 0x8348 },
	{ 114, 0x834c }, { 115, 0x8350 }, { 116, 0x8354 }, { 117, 0x8358 }, { 118, 0x835c },
	{ 119, 0x8360 },
};

/* second pass (before the trigger) */
static const struct t33_dmsc_wr t33_dmsc_wr_b[] = {
	{ 3, 0x8040 }, { 4, 0x8044 }, { 9, 0x8058 }, { 11, 0x8060 }, { 13, 0x8068 },
	{ 14, 0x806c }, { 17, 0x8078 }, { 18, 0x807c }, { 21, 0x8094 }, { 24, 0x80ac },
	{ 26, 0x80b4 }, { 31, 0x80c8 }, { 33, 0x80d0 }, { 34, 0x80d4 }, { 35, 0x80d8 },
	{ 36, 0x80dc }, { 37, 0x80e0 }, { 38, 0x80e4 }, { 42, 0x8188 }, { 48, 0x81c4 },
	{ 50, 0x81cc }, { 52, 0x81dc }, { 55, 0x81e8 }, { 57, 0x81fc }, { 65, 0x821c },
	{ 70, 0x8254 }, { 71, 0x8258 }, { 73, 0x827c }, { 76, 0x8288 }, { 77, 0x828c },
	{ 81, 0x829c }, { 98, 0x82ec }, { 104, 0x8324 }, { 105, 0x8328 }, { 107, 0x8330 },
	{ 109, 0x8338 }, { 110, 0x833c }, { 114, 0x834c }, { 115, 0x8350 }, { 117, 0x8358 },
	{ 118, 0x835c }, { 119, 0x8360 },
};

static const u16 t33_dmsc_ratio_w[5] = { 42, 43, 64, 65, 179 };

static inline void t33_dmsc_emit(u32 ch, const u32 *l, const struct t33_dmsc_wr *t, unsigned int n)
{
	unsigned int k;

	for (k = 0; k < n; k++)
		tisp_reg_write_t33(ch, 9, dmscCfgMode ? t[k].slot : t[k].addr, l[t[k].slot + 1]);
}

/* tisp_dmsc_par_refresh */
static inline void t33_dmsc_par_step(u32 ch, u32 gain, u32 thr)
{
	u32 *v = dmscInfo[ch];
	u32 last = v[671], ch8 = ch & 0xff, diff, i, f, sharp, l[146];
	const u8 *sec;
	unsigned int k;

	if (last != 0xffffffffU) {
		diff = gain < last ? last - gain : gain - last;
		if (diff < thr)
			return;
	}
	v = dmscInfo[ch8];
	sec = (const u8 *)(unsigned long)v[670];
	t33_vb_bin(v, sec, t33_dmsc_bin, ARRAY_SIZE(t33_dmsc_bin));
	i = (u32)((int)gain >> 16);
	f = gain & 0xffff;
	for (k = 0; k < ARRAY_SIZE(t33_dmsc_intp); k++)
		v[t33_dmsc_intp[k].word] = t33_dmsc_intp[k].bits == 8 ?
			t32_tp_intp_u8(i, f, sec + t33_dmsc_intp[k].off) :
			t32_tp_intp_u16(i, f, sec + t33_dmsc_intp[k].off);
	for (k = 0; k < 5; k++) {
		sharp = ((const u8 *)v)[2688];
		v[t33_dmsc_ratio_w[k]] = tisp_ratio(sharp, v[t33_dmsc_ratio_w[k]], 600);
	}
	t33_vb_copy(v, t33_dmsc_copy, ARRAY_SIZE(t33_dmsc_copy));
	t33_dmsc_para(v);
	if (tisp_rcfg_get_dma_state(9)) {
		tispDmscRefresh = 2;
	} else {
		for (k = 0; k < 146; k++)
			l[k] = 0;
		t33_dmsc_store(v, l);
		t33_dmsc_emit(ch8, l, t33_dmsc_wr_a, ARRAY_SIZE(t33_dmsc_wr_a));
		t33_dmsc_emit(ch8, l, t33_dmsc_wr_b, ARRAY_SIZE(t33_dmsc_wr_b));
		tisp_reg_write_t33(ch8, 9, dmscCfgMode ? 1 : 0x8010, 1);
		tispDmscRefresh = 1;
	}
	v = dmscInfo[ch];
	v[671] = gain;
}

/*
 * tisp_dmsc_sharpness_set (vendor 0x66fd8, from tisp_set_sharpness, openimp #707): the level byte goes into
 * state word 672 (the tisp_ratio byte of the five sharpness words), then the refresh runs at the stored gain
 * (word 671) without the gain gate: threshold 0, the gain word keeps its value.
 */
static inline void t33_dmsc_sharpness_set(u32 ch, u32 level)
{
	u32 *v = dmscInfo[ch];
	u32 gain = v[671];

	v[672] = level & 0xff;
	t33_dmsc_par_step(ch, gain, 0);
}

#endif /* TX_ISP_T33_I9N_H */
