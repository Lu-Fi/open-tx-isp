/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 wide dynamic range fusion (WDR, "tisp_wdr_*") from the vendor module
 * (Matt Davis' recovered C, PR #15).  The block is ported in phases, each
 * proven bit-exact against the vendor functions by a host oracle
 * (tests/t32_oracle/t32_wdr<n>_oracle):
 *
 *  phase 1 (t32_wdr1_oracle): absfun, interplate, exptime_update, minFun,
 *    maxFun, log2, long_tgain_update, short_tgain_update, short_ev_update,
 *    ev_interp, long_ev_update, api_get_output_mode, degweight_ev.
 *  phase 2 (t32_wdr2_oracle): getVar, get_lum_s, quicksort, paixu.
 *  phase 3 (t32_wdr3_oracle): params_hard_refresh, param_array_get, frame_out,
 *    sleep, malloc_cfg.
 *
 * The vendor reaches the per channel WDR parameter block (the tuning values
 * of the IQ bin plus the working state) as t32_data_e3b40[ch] -> record,
 * record + 12 -> block; the port keeps the record (struct t32_wdr_rec, the
 * vendor's 28 byte layout as named pointers) per channel in struct
 * t32_wdr_st and addresses the block members by the vendor offsets below.  The block is little endian kernel memory: the accessors
 * read and write it in place.
 *
 * Vendor UB that the port does not reproduce (the oracle never feeds it):
 * tisp_wdr_interplate with x1 == x0 divides by zero (the MIPS div result is
 * undefined).  Channels of the 64 bit per channel EV words (e3ab0 / e3ac0,
 * 2 entries each in the vendor .bss) are 0 and 1 only; the vendor writes
 * channel 2 and 3 over the next object.  Return values that are only
 * leftover registers in the vendor code (the high word of every return,
 * the low word of the void functions) are not reproduced.
 */
#ifndef TX_ISP_T32_WDR_H
#define TX_ISP_T32_WDR_H

#include "tx_isp_t32_tp.h"

/* ---- parameter block members (vendor offsets in the block of record + 12) ---- */
#define T32_WDR_P_EVY_LO	32	/* u32[4]: EV curve values 0..3 */
#define T32_WDR_P_EVY_HI	80	/* u32[6]: EV curve values 4..9 */
#define T32_WDR_P_EV_OUT	260	/* u16: value of the EV curve at the current exposure */
#define T32_WDR_P_OUT_MODE	278	/* u16: output mode (api get / set) */
#define T32_WDR_P_EVX		1608	/* u16[10]: EV curve knots */

static inline u32 t32_wdr_r16(const u8 *p, u32 off)
{
	return p[off] | (u32)p[off + 1] << 8;
}
static inline u32 t32_wdr_r32(const u8 *p, u32 off)
{
	return p[off] | (u32)p[off + 1] << 8 | (u32)p[off + 2] << 16 | (u32)p[off + 3] << 24;
}
static inline void t32_wdr_w16(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
}
static inline void t32_wdr_w32(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
	p[off + 2] = (u8)(v >> 16);
	p[off + 3] = (u8)(v >> 24);
}

/* per channel record (vendor: 28 bytes from kmalloc, t32_data_e3b40[ch]) */
struct t32_wdr_rec {
	u32 nbuf;		/* +0: number of statistics buffers (4) */
	u8 *buf;		/* +4: 4 x 6144 byte statistics buffer (kmalloc) */
	u32 buf_phys;		/* +8: its bus address */
	u8 *par;		/* +12: parameter block = tuning base + 10132 */
	u8 *ext;		/* +16: 112 byte extension of the parameter array */
	u8 *hw;			/* +20: 1036 byte hardware parameter image */
	u8 *work;		/* +24: 10200 byte work area */
};

/* OS / register hooks */
struct t32_wdr_ops {
	void (*reg_write)(u32 reg, u32 val);	/* system_reg_write */
	u32 (*reg_read)(u32 reg);		/* system_reg_read */
	u8 *(*kmalloc)(u32 size);		/* private_kmalloc(size, GFP 0x2400c0) */
	u32 (*virt_to_phys)(const u8 *p);	/* vendor: p + 0x80000000 (KSEG0) */
};

/* vendor .bss of the block */
struct t32_wdr_st {
	const struct t32_wdr_ops *ops;
	struct t32_wdr_rec *rec[4];	/* t32_data_e3b40 */
	u32 dma_save[5];	/* t32_data_e3a84: 0x28010 .. 0x2801c, 0x28030 kept over sleep */
	u64 short_tgain;	/* t32_data_e3aa0 (low word) / e3aa4 (high word) */
	u64 short_ev[2];	/* t32_data_e3ab0 */
	u64 long_ev[2];		/* t32_data_e3ac0 */
	u8 ev_changed[4];	/* t32_data_e3ad0 */
};

/* signed 32 bit division, INT_MIN / -1 = INT_MIN (the vendor's emulation; never reached by the callers) */
static inline int t32_wdr_sdiv(int n, int d)
{
	if (n == (int)0x80000000U && d == -1)
		return n;
	return n / d;
}

/* tisp_wdr_absfun: |a - b| of two unsigned values */
static inline u32 t32_wdr_absfun(u32 a, u32 b)
{
	return b < a ? a - b : b - a;
}

/* tisp_wdr_interplate (x0, x1, y0, y1, x): y at x on the line (x0,y0)-(x1,y1), rounded; x1 != x0 (see above) */
static inline int t32_wdr_interplate(int x0, int x1, int y0, int y1, int x)
{
	u32 span = (u32)x1 - (u32)x0;
	u32 num = ((u32)y1 - (u32)y0) * ((u32)x - (u32)x0) + (u32)((int)span >> 1);

	return (int)((u32)t32_wdr_sdiv((int)num, (int)span) + (u32)y0);
}

/* tisp_wdr_exptime_update: e[1] is the new exposure, e[2] the previous one; stores the new one, returns |new - old| */
static inline u32 t32_wdr_exptime_update(u32 *e)
{
	u32 cur = e[1], prev = e[2];

	e[2] = cur;
	return prev < cur ? cur - prev : prev - cur;
}

/* tisp_wdr_minFun / tisp_wdr_maxFun: signed min / max */
static inline int t32_wdr_minfun(int a, int b)
{
	return b < a ? b : a;
}
static inline int t32_wdr_maxfun(int a, int b)
{
	return a >= b ? a : b;
}

/* tisp_wdr_log2: the smallest n in 1..14 with 2^n >= v (signed compare), else 15 */
static inline u32 t32_wdr_log2(int v)
{
	u32 n;

	for (n = 1; n < 15; n++)
		if (!((int)(1U << n) < v))
			return n;
	return 15;
}

/* tisp_wdr_long_tgain_update: empty in the vendor, returns 0 */
static inline int t32_wdr_long_tgain_update(void)
{
	return 0;
}

/* tisp_wdr_short_tgain_update (ch, tgain): keeps the total gain of the short frame >> 16 (one value, any channel) */
static inline int t32_wdr_short_tgain_update(struct t32_wdr_st *st, u64 tgain)
{
	st->short_tgain = tgain >> 16;
	return 0;
}

/* tisp_wdr_short_ev_update (ch, ev): keeps the short frame exposure value >> 10 of the channel */
static inline int t32_wdr_short_ev_update(struct t32_wdr_st *st, u32 ch, u64 ev)
{
	st->short_ev[ch] = ev >> 10;
	return 0;
}

/*
 * tisp_wdr_ev_interp (ev, ch): the EV curve (knots EVX[0..9], values EVY 0..9) at ev into
 * EV_OUT: below the first knot the first value, from the last knot on nothing changes,
 * inside segment i (first i with x[i] <= ev < x[i + 1]) the value
 * (y[i] * (x1 - x0) - (y[i] - y[i + 1]) * (ev - x0)) / (x1 - x0), 32 bit unsigned.
 */
static inline void t32_wdr_ev_interp(struct t32_wdr_st *st, u64 ev, u32 ch)
{
	u8 *p = st->rec[ch]->par;
	u32 y[10], i, e;

	for (i = 0; i < 4; i++)
		y[i] = t32_wdr_r32(p, T32_WDR_P_EVY_LO + 4 * i);
	for (i = 0; i < 6; i++)
		y[4 + i] = t32_wdr_r32(p, T32_WDR_P_EVY_HI + 4 * i);
	if (ev < t32_wdr_r16(p, T32_WDR_P_EVX)) {
		t32_wdr_w16(p, T32_WDR_P_EV_OUT, y[0]);
		return;
	}
	if (ev >= t32_wdr_r16(p, T32_WDR_P_EVX + 18))
		return;
	e = (u32)ev;
	for (i = 0; i < 9; i++) {
		u32 x0 = t32_wdr_r16(p, T32_WDR_P_EVX + 2 * i), x1 = t32_wdr_r16(p, T32_WDR_P_EVX + 2 * i + 2);

		if (e < x0 || e >= x1)
			continue;
		t32_wdr_w16(p, T32_WDR_P_EV_OUT, (y[i] * (x1 - x0) - (y[i] - y[i + 1]) * (e - x0)) / (x1 - x0));
		return;
	}
}

/* tisp_wdr_long_ev_update (ch, ev): marks the channel changed, keeps ev >> 10 and runs the EV curve on it */
static inline int t32_wdr_long_ev_update(struct t32_wdr_st *st, u32 ch, u64 ev)
{
	st->ev_changed[ch] = 1;
	st->long_ev[ch] = ev >> 10;
	t32_wdr_ev_interp(st, ev >> 10, ch);
	return 0;
}

/* tisp_wdr_api_get_output_mode (ch, out) */
static inline int t32_wdr_api_get_output_mode(const struct t32_wdr_st *st, u32 ch, u32 *mode)
{
	*mode = t32_wdr_r16(st->rec[ch]->par, T32_WDR_P_OUT_MODE);
	return 0;
}

/*
 * tisp_wdr_degweight_ev (cfg, xs, ys, v, ev): weight of the deghost blend.
 * cfg (u16): [0] EV gate on, [1] value gate on, [2] value knee high, [3] value knee low,
 * [4] gain threshold.  ev (u32): [1] exposure, [3] gain.  base = ys[0], or 0 when
 * cfg[4] < ev[3].  EV gate: the 8 point curve (xs u32, ys u16) at ev[1] (first value
 * up to xs[0], last above xs[7], rounded line inside) capped at base; otherwise base.
 * Value gate: 0 from cfg[2] on, 32 below cfg[3], the rounded ramp 32 .. 0 between; the
 * smaller of the two.
 */
static inline u32 t32_wdr_degweight_ev(const u16 *cfg, const u32 *xs, const u16 *ys, u32 v, const u32 *ev)
{
	u32 base = cfg[4] < ev[3] ? 0 : ys[0];
	u32 w = base;

	v &= 0xffff;
	if (cfg[0] == 1) {
		u32 e = ev[1], i;

		if (!(xs[0] < e)) {
			w = ys[0];
		} else if (xs[7] < e) {
			w = ys[7];
		} else {
			w = 0;
			for (i = 0; i < 7; i++) {
				u32 dx;

				if (!(xs[i] < e) || xs[i + 1] < e)
					continue;
				dx = xs[i + 1] - xs[i];
				w = ((ys[i + 1] - ys[i]) * (e - xs[i]) + (dx >> 1)) / dx + ys[i];
				break;
			}
		}
		if (w >= base)
			w = base;
	}
	if (cfg[1] == 1) {
		u32 knee_hi = cfg[2], knee_lo = cfg[3], g;

		if (v >= knee_hi)
			g = 0;
		else if (v < knee_lo)
			g = 32;
		else
			g = ((knee_hi - v) * 32 + ((knee_hi - knee_lo) >> 1)) / (knee_hi - knee_lo);
		if (g < w)
			w = g;
	}
	return w;
}

/* ---- phase 2: statistics helpers ---- */

/*
 * tisp_wdr_getVar (h): spread of the histogram slope.  h has 257 u16 entries (the vendor
 * reads h[256]).  Mean: over k = 1..255 with h[k] < 200 and h[k + 1] < 200 the halved central
 * difference (h[k] - h[k - 1] + 1) / 2 (signed), times 10 over the count + 1, rounded.  Result:
 * the rounded mean square of (h[j + 2] - h[j] + 1) / 2 * 10 - mean over j = 0 .. count.
 */
static inline u32 t32_wdr_getvar(const u16 *h)
{
	u32 cnt = 1, sum = 0, mean, acc = 0, k;

	for (k = 1; k < 256; k++) {
		if (h[k] >= 200 || h[k + 1] >= 200)
			continue;
		cnt++;
		sum += (u32)t32_wdr_sdiv((int)h[k] - (int)h[k - 1] + 1, 2);
	}
	mean = (sum * 10 + (u32)((int)cnt >> 1)) / cnt;
	for (k = 0; k < cnt; k++) {
		u32 d = (u32)t32_wdr_sdiv((int)h[k + 2] - (int)h[k] + 1, 2) * 10 - mean;

		acc += d * d;
	}
	return (acc + (u32)((int)cnt >> 1)) / cnt;
}

/*
 * tisp_wdr_get_lum_s (hist, thr, out, w, lut): three luminance levels from the top of the
 * 160 bin histogram.  The 16 bit running sum of hist[] from bin 159 down is compared with
 * thr[0], thr[1], thr[2] in turn (each search goes on below the bin found before); a hit
 * stores lut[bin] into out[k], a miss leaves out[k].  out[3] = (w[0] out[0] + w[1] out[1] +
 * w[2] out[2] + 5) / 10 (signed); returns that quotient.
 */
static inline int t32_wdr_get_lum_s(const u32 *hist, const u16 *thr, u16 *out, const u8 *w, const u16 *lut)
{
	u32 sum = 0;
	int i = 159, k;

	for (k = 0; k < 3; k++) {
		for (; i >= 0; i--) {
			sum = (sum + hist[i]) & 0xffff;
			if (sum >= thr[k]) {
				out[k] = lut[i];
				break;
			}
		}
		i--;	/* the next search starts below the bin found (or ends after a miss) */
	}
	k = t32_wdr_sdiv((int)((u32)w[1] * out[1] + (u32)w[0] * out[0] + (u32)w[2] * out[2] + 5), 10);
	out[3] = (u16)k;
	return k;
}

/* tisp_wdr_quicksort (a, lo, hi): ascending in place sort of the u16 entries a[lo..hi] (vendor partition, pivot a[lo]) */
static void t32_wdr_quicksort(u16 *a, int lo, int hi)
{
	while (lo <= hi) {
		u32 pivot = a[lo];
		int i = lo, j = hi;

		while (i != j) {
			while (pivot < a[j] && i < j)
				j--;
			a[i] = a[j];
			while (!(pivot < a[i]) && i < j)
				i++;
			a[j] = a[i];
		}
		a[i] = (u16)pivot;
		t32_wdr_quicksort(a, lo, i - 1);
		lo = i + 1;
	}
}

/*
 * tisp_wdr_paixu (a, n, ascending): exchange sort of the first n positions against all 225
 * entries: for i < n and every j in i + 1 .. 224 a[i] and a[j] swap when out of order
 * (descending for ascending == 0, else ascending).
 */
static inline void t32_wdr_paixu(u16 *a, u32 n, u32 ascending)
{
	u32 i, j;

	n &= 0xff;
	for (i = 0; i < n; i++)
		for (j = i + 1; j < 225; j++) {
			u16 x = a[i], y = a[j];

			if ((ascending & 0xff) ? y < x : x < y) {
				a[i] = y;
				a[j] = x;
			}
		}
}

/* ---- phase 3: parameter image, array get, DMA buffers ---- */

#define T32_WDR_REG_DMA0	0x28010	/* .. 0x2801c: bus addresses of the 4 statistics buffers */
#define T32_WDR_REG_DMA_CTL	0x28030
#define T32_WDR_REG_FRAME	0x28040
#define T32_WDR_REG_OUTSEL	0x2800c
#define T32_WDR_BUF_SZ		6144
#define T32_WDR_PAR_SZ		1840	/* parameter block bytes of the api array */
#define T32_WDR_EXT_SZ		112

static inline void t32_wdr_cpy(u8 *d, const u8 *s, u32 n)
{
	while (n--)
		*d++ = *s++;
}

/*
 * tisp_wdr_params_hard_refresh (ch): copies the parameter block members the hardware
 * refresh uses into the hardware image (hw <- par, offsets of the vendor).
 */
static inline int t32_wdr_params_hard_refresh(struct t32_wdr_st *st, u32 ch)
{
	u8 *hw = st->rec[ch]->hw;
	const u8 *par = st->rec[ch]->par;

	t32_wdr_cpy(hw + 136, par + 168, 24);
	t32_wdr_cpy(hw + 754, par + 246, 10);
	t32_wdr_cpy(hw + 1022, par + 1778, 4);
	t32_wdr_cpy(hw + 1026, par + 1818, 5);
	t32_wdr_cpy(hw + 764, par + 256, 12);
	t32_wdr_w32(hw, 0, t32_wdr_r32(par, 4));
	t32_wdr_w32(hw, 4, t32_wdr_r32(par, 8));
	t32_wdr_cpy(hw + 780, par + 268, 10);
	t32_wdr_cpy(hw + 790, par + T32_WDR_P_OUT_MODE, 12);
	t32_wdr_cpy(hw + 802, par + 290, 20);
	t32_wdr_cpy(hw + 822, par + 310, 8);
	t32_wdr_cpy(hw + 830, par + 318, 50);
	hw[1031] = par[1831];
	hw[1032] = par[1832];
	hw[1033] = par[1825];
	return 0;
}

/* tisp_wdr_param_array_get (ch, out, size): the parameter block and its extension, 1952 bytes */
static inline int t32_wdr_param_array_get(const struct t32_wdr_st *st, u32 ch, u8 *out, u32 *size)
{
	t32_wdr_cpy(out, st->rec[ch]->par, T32_WDR_PAR_SZ);
	t32_wdr_cpy(out + T32_WDR_PAR_SZ, st->rec[ch]->ext, T32_WDR_EXT_SZ);
	*size = T32_WDR_PAR_SZ + T32_WDR_EXT_SZ;
	return 0;
}

/* tisp_wdr_frame_out (ch): frame trigger, then the output select bit (par + 1832 bit 0) */
static inline void t32_wdr_frame_out(const struct t32_wdr_st *st, u32 ch)
{
	st->ops->reg_write(T32_WDR_REG_FRAME, 1);
	st->ops->reg_write(T32_WDR_REG_OUTSEL, (st->rec[ch]->par[1832] & 1U) << 4);
}

/* tisp_wdr_sleep: keeps the 4 buffer addresses and the DMA control word */
static inline void t32_wdr_sleep(struct t32_wdr_st *st)
{
	u32 i;

	for (i = 0; i < 4; i++)
		st->dma_save[i] = st->ops->reg_read(T32_WDR_REG_DMA0 + 4 * i);
	st->dma_save[4] = st->ops->reg_read(T32_WDR_REG_DMA_CTL);
}

/*
 * tisp_wdr_malloc_cfg (ch): one 24576 byte buffer for the 4 statistics buffers, their bus
 * addresses into 0x28010 .. 0x2801c, DMA control 3, recorded in the channel's record.
 * The vendor's 10 debug prints are dropped.  Vendor: no check of the allocation (a failed
 * kmalloc programs the bus addresses of NULL), kept.
 */
static inline int t32_wdr_malloc_cfg(struct t32_wdr_st *st, u32 ch)
{
	struct t32_wdr_rec *r;
	u8 *buf = st->ops->kmalloc(4 * T32_WDR_BUF_SZ);
	u32 phys = st->ops->virt_to_phys(buf), i;

	for (i = 0; i < 4; i++)
		st->ops->reg_write(T32_WDR_REG_DMA0 + 4 * i, phys + i * T32_WDR_BUF_SZ);
	st->ops->reg_write(T32_WDR_REG_DMA_CTL, 3);
	r = st->rec[ch];
	r->nbuf = 4;
	r->buf = buf;
	r->buf_phys = phys;
	return 0;
}

#endif
