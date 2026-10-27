/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE and AWB statistics engines: the vendor's initialisation from the IQ
 * bin (S4), as register writes in the vendor's order.
 *
 * Reverse engineered flow (PRJ007 driver): tisp_ae_malloc_cfg,
 * tisp_ae_set_hardware_param, tisp_ae_roi_point (from tisp_ae_init), and
 * tisp_awb_malloc_cfg, tisp_awb_set_hardware_param,
 * tisp_awb_set_regional_threshold, tisp_awb_set_lum_th_freq (from
 * tisp_awb_init); tisp_simple_intp_uint16 for the EV dependent thresholds.
 * Checked against the vendor's writes in the QEMU trace (run v1, cv8001
 * PRJ007 bin, 3840x2160).
 *
 * Parameter blocks: IQ bin sections 1 (AE, bin + 0x104) and 2 (AWB,
 * bin + 0xddc), found by the vendor's section walk (tx_isp_t32_iq.h).  The
 * vendor copies them to tparamsP + 0x68 / + 0xd40; the 4 bytes between the
 * bin and tparamsP offsets come from its per-section copy, not from a
 * different pointer (the former open question).
 */
#ifndef TX_ISP_T32_STATS_H
#define TX_ISP_T32_STATS_H

#ifndef __KERNEL__
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

/* IQ bin sections 1 and 2 (tx_isp_t32_iq.h; the vendor's tparamsP + 0x68 / + 0xd40) */
#define T32_AE_PARAM		0x104U
#define T32_AWB_PARAM		0xddcU
#define T32_AE_BUF_BYTES	0x6000U		/* 4 zone buffers of 4 KiB + 4 histograms of 2 KiB */
#define T32_AWB_BUF_BYTES	0x1c000U	/* 4 buffers of 0x7000 */
#define T32_STATS_ID		0x0134da11U
#define T32_AWB_INIT_EV		1024U		/* tisp_awb_init */

#define T32_AE_TRIG		0x17010
#define T32_AE_ID		0x17004
#define T32_AWB_TRIG		0x16010
#define T32_STATS_MAX_WRITES	48

struct t32_stats_write {
	u32 reg, val;
};

static inline u32 t32_st16(const u8 *p, u32 o) { return p[o] | (u32)p[o + 1] << 8; }

static inline u16 t32_intp_u16(u32 i, u32 frac, const u8 *t)
{
	u32 a, b, d, v;

	if (i >= 10)
		return (u16)t32_st16(t, 20);
	a = t32_st16(t, 2 * i);
	b = t32_st16(t, 2 * i + 2);
	if (a == b)
		return (u16)a;
	d = (a < b ? b - a : a - b) * frac;
	v = (d >> 16) + ((d >> 15) & 1);
	return (u16)(a < b ? a + v : a - v);
}

static inline unsigned int t32_stats_add(struct t32_stats_write *w, unsigned int n, u32 reg, u32 val)
{
	if (n < T32_STATS_MAX_WRITES) {
		w[n].reg = reg;
		w[n].val = val;
	}
	return n + 1;
}

/* the 4 bytes p16[o + 6], p16[o + 4], p16[o + 2], p16[o] as one word */
static inline u32 t32_st_bytes4(const u8 *p, u32 o)
{
	return t32_st16(p, o + 6) << 24 | t32_st16(p, o + 4) << 16 | t32_st16(p, o + 2) << 8 | t32_st16(p, o);
}

static inline u32 t32_bytes4(const u16 *z, unsigned int i)
{
	return (u32)z[i + 3] << 24 | (u32)z[i + 2] << 16 | (u32)z[i + 1] << 8 | z[i];
}

/* AE: buffers at buf_pa (T32_AE_BUF_BYTES), sensor width x height */
static inline unsigned int t32_ae_stats_init(const u8 *bin, u32 width, u32 height, u32 buf_pa,
					     struct t32_stats_write *w)
{
	const u8 *p = bin + T32_AE_PARAM;
	u32 x0 = t32_st16(p, 2768), nx_v = t32_st16(p, 2770), y0 = t32_st16(p, 2772), nx = t32_st16(p, 2774);
	u32 lum = t32_st16(p, 2778), hw, hh, cx, cy;
	u16 zh[16] = { 0 }, zv[16] = { 0 };
	unsigned int n = 0, i;

	for (i = 0; i < 4; i++)
		n = t32_stats_add(w, n, 0x17068 + 4 * i, buf_pa + i * 0x1000);
	for (i = 0; i < 4; i++)
		n = t32_stats_add(w, n, 0x17078 + 4 * i, buf_pa + 0x4000 + i * 0x800);
	n = t32_stats_add(w, n, 0x17088, 0x10010033);
	/* zones: half picture over the zone count (tisp_ae_set_hardware_param) */
	for (i = 0; i < nx && i < 15; i++)
		zh[i] = (u16)(((width - y0 + 1) / 2) / nx);
	for (i = 0; i < nx_v && i < 15; i++)
		zv[i] = (u16)(((height - x0 + 1) / 2) / nx_v);
	n = t32_stats_add(w, n, 0x17040, nx << 28 | y0 << 16 | nx_v << 12 | x0);
	n = t32_stats_add(w, n, 0x17044, t32_bytes4(zh, 0));
	n = t32_stats_add(w, n, 0x17048, t32_bytes4(zh, 4));
	n = t32_stats_add(w, n, 0x1704c, t32_bytes4(zh, 8));
	n = t32_stats_add(w, n, 0x17050, (u32)zh[14] << 16 | (u32)zh[13] << 8 | zh[12]);
	n = t32_stats_add(w, n, 0x17054, t32_bytes4(zv, 0));
	n = t32_stats_add(w, n, 0x17058, t32_bytes4(zv, 4));
	n = t32_stats_add(w, n, 0x1705c, t32_bytes4(zv, 8));
	n = t32_stats_add(w, n, 0x17060, (u32)zv[14] << 16 | (u32)zv[13] << 8 | zv[12]);
	if (lum < 255)		/* the vendor raises values below 255 to 255 */
		lum = 255;
	n = t32_stats_add(w, n, 0x17064, 0x110000 | (lum * 2 / 3) << 8 | t32_st16(p, 2776));
	/* centre ROI: an eighth or a quarter of the size around the centre */
	cx = width >> 1;
	cy = height >> 1;
	if (width * height / 4 >= 0x100000) {
		hw = (width >> 3) & 0xffff;
		hh = (height >> 3) & 0xffff;
	} else {
		hw = (width >> 2) & 0xffff;
		hh = (height >> 2) & 0xffff;
	}
	n = t32_stats_add(w, n, 0x17090, ((cx + hw) & 0xffff) << 16 | ((cx - hw) & 0xffff));
	n = t32_stats_add(w, n, 0x17094, ((cy + hh) & 0xffff) << 16 | ((cy - hh) & 0xffff));
	n = t32_stats_add(w, n, T32_AE_TRIG, 1);
	n = t32_stats_add(w, n, T32_AE_TRIG, 1);
	n = t32_stats_add(w, n, T32_AE_ID, T32_STATS_ID);
	return n;
}

/* AWB: buffers at buf_pa (T32_AWB_BUF_BYTES), thresholds for the EV (log2 Q16) */
static inline unsigned int t32_awb_stats_init(const u8 *bin, u32 buf_pa, u32 ev,
					      struct t32_stats_write *w)
{
	static const u16 thr[8] = { 4588, 4610, 4632, 4654, 4676, 4698, 4720, 4742 };
	const u8 *p = bin + T32_AWB_PARAM;
	u32 i = ev >> 16, f = ev & 0xffff, q[8], l[11], k;
	unsigned int n = 0;

	for (k = 0; k < 4; k++)
		n = t32_stats_add(w, n, 0x16078 + 4 * k, buf_pa + k * 0x7000);
	n = t32_stats_add(w, n, 0x16088, 3);
	n = t32_stats_add(w, n, 0x16040, t32_st16(p, 3188) << 16 | t32_st16(p, 3184));
	n = t32_stats_add(w, n, 0x16044, t32_st_bytes4(p, 3192));
	n = t32_stats_add(w, n, 0x16048, t32_st_bytes4(p, 3200));
	n = t32_stats_add(w, n, 0x1604c, t32_st_bytes4(p, 3208));
	n = t32_stats_add(w, n, 0x16050, t32_st16(p, 3190) << 24 | (t32_st_bytes4(p, 3214) >> 8));
	n = t32_stats_add(w, n, 0x16054, t32_st_bytes4(p, 3222));
	n = t32_stats_add(w, n, 0x16058, t32_st_bytes4(p, 3230));
	n = t32_stats_add(w, n, 0x1605c, t32_st_bytes4(p, 3238));
	n = t32_stats_add(w, n, 0x16060, t32_st16(p, 3186) << 24 | (t32_st_bytes4(p, 3244) >> 8));
	n = t32_stats_add(w, n, 0x16088, 0x01010103 | (u32)p[3276] << 12);
	n = t32_stats_add(w, n, 0x160f4, p[3290] | p[3291] << 8 | p[3292] << 16 | (u32)p[3293] << 24);
	/* tisp_awb_set_regional_threshold */
	for (k = 0; k < 8; k++)
		q[k] = t32_intp_u16(i, f, p + thr[k]);
	n = t32_stats_add(w, n, 0x16064, q[1] << 16 | q[0]);
	n = t32_stats_add(w, n, 0x16068, q[3] << 16 | q[2]);
	n = t32_stats_add(w, n, 0x1606c, q[5] << 16 | q[4]);
	n = t32_stats_add(w, n, 0x16070, q[7] << 16 | q[6]);
	n = t32_stats_add(w, n, T32_AWB_TRIG, 1);
	/* tisp_awb_set_lum_th_freq (no luma override set) */
	for (k = 0; k < 11; k++)
		l[k] = t32_intp_u16(i, f, p + 4764 + 22 * k);
	n = t32_stats_add(w, n, 0x160f0, (l[3] & 15) | (l[4] & 15) << 8 | (l[5] & 15) << 16 | (l[6] & 15) << 24);
	n = t32_stats_add(w, n, 0x16074, t32_st16(p, 3274) << 24 | l[2] << 16 | l[1] << 8 | l[0]);
	n = t32_stats_add(w, n, 0x160ec, (t32_st16(p, 3280) & 3) << 16 | (t32_st16(p, 3278) & 1));
	n = t32_stats_add(w, n, 0x160f8, (l[7] & 15) | (l[8] & 15) << 8 | (l[9] & 15) << 16 | (l[10] & 15) << 24);
	n = t32_stats_add(w, n, T32_AWB_TRIG, 1);
	return n;
}

/*
 * Bring-up controller helpers (NOT the vendor AE): the AE histogram as the
 * statistics engine writes it (8 bytes after the buffer start, 256 bins of
 * 24 bits, two per 64-bit word) and one proportional exposure step.
 */
#ifdef __KERNEL__
#include <linux/math64.h>
#else
static inline unsigned long long div_u64(unsigned long long a, u32 b) { return a / b; }
#endif

#define T32_AE_HIST_HEAD	8
#define T32_AE_HIST_BINS	256
#define T32_AE_CUR		0x1708c	/* bits 1:0 histogram buffer written last */

static inline u32 t32_ae_hist_bin(const u8 *h, unsigned int i)
{
	unsigned int bit = i * 24 + i / 2 * 16, k;
	u32 v = 0;

	for (k = 0; k < 24; k++, bit++)
		v |= (u32)((h[bit / 8] >> (bit % 8)) & 1) << k;
	return v;
}

/* mean bin (0..255) times 16, 0 when the histogram is empty */
static inline u32 t32_ae_hist_mean16(const u8 *h)
{
	unsigned long long sum = 0;
	u32 n = 0;
	unsigned int i;

	for (i = 0; i < T32_AE_HIST_BINS; i++) {
		u32 c = t32_ae_hist_bin(h, i);

		sum += (unsigned long long)c * i;
		n += c;
	}
	if (!n)
		return 0;
	return (u32)div_u64(sum * 16 + n / 2, n);
}

/* next integration time: proportional to target / mean, limited to a
 * quarter / four times per step and to 1 .. max lines */
static inline u32 t32_bringup_ae_step(u32 it, u32 mean16, u32 target16, u32 max)
{
	u32 next;

	if (!mean16 || !target16 || !it)
		return it;
	next = (u32)div_u64((unsigned long long)it * target16, mean16);
	if (next > it * 4)
		next = it * 4;
	if (next < it / 4)
		next = it / 4;
	if (next < 1)
		next = 1;
	if (next > max)
		next = max;
	return next;
}

#endif /* TX_ISP_T32_STATS_H */
