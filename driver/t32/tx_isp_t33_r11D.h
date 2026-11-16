/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 block r11D: AF statistics accessors / buffer setup, LCE buffer setup and the LCE Gaussian low pass.
 * Vendor functions: tisp_af_api_get_metric, tisp_af_api_get_statis_info, tisp_g_af_metric_attr,
 * tisp_g_af_statis_attr, tisp_af_malloc_cfg, tisp_lce_malloc_cfg, tisp_lce_gauss_lpf.
 * Checked against the vendor module by tests/t33_binoracle (block r11D).
 */
#ifndef TX_ISP_T33_R11D_H
#define TX_ISP_T33_R11D_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t32_lcetg.h"

/* kernel services the buffer setup uses (hooks, so the header needs no kernel declarations) */
struct t33_mem_ops {
	void *(*vmalloc)(u32 n);
	void *(*kmalloc)(u32 n, u32 flags);
	void (*oom)(void);		/* isp_printf on a failed allocation (execution continues) */
	void (*unaligned)(void);	/* printk for a statistics buffer that is not 8 byte aligned */
	int (*reg_write)(u32 ch, u32 blk, u32 reg, u32 val);
};

/* tisp_af_api_get_metric: info[ch] holds a pointer to the parameter block at +1256; attr = af_par_attr */
static inline int t33_af_get_metric(u8 *const *info, const u8 *attr, u32 ch, u32 *m1, u32 *m2)
{
	const u8 *blk = info[ch];
	const u8 *par = (const u8 *)(unsigned long)t33_le32(blk + 1256);

	*m1 = t33_le32(par + 68) >> (attr[ch * 24 + 9] & 31);
	*m2 = t33_le32(par + 64) >> (attr[ch * 24 + 9] & 31);
	return 0;
}

/* tisp_g_af_metric_attr: the two metrics into the first two words of the user buffer */
static inline int t33_g_af_metric_attr(u8 *const *info, const u8 *attr, u32 ch, u8 *buf)
{
	t33_af_get_metric(info, attr, ch, (u32 *)buf, (u32 *)(buf + 4));
	return 0;
}

/* tisp_af_api_get_statis_info: 5400 bytes from the buffer at word 1 of the channel's buffer info, +8 */
static inline void t33_af_get_statis(u32 *const *info, u32 ch, u8 *out)
{
	memcpy(out, (const u8 *)(unsigned long)info[ch][1] + 8, 5400);
}

/* tisp_g_af_statis_attr */
static inline int t33_g_af_statis_attr(u32 *const *info, u32 ch, u8 *buf)
{
	t33_af_get_statis(info, ch, buf);
	return 0;
}

/* tisp_af_malloc_cfg: info[ch] = { 4, virt, phys }, the four DMA parts and the control word */
static inline int t33_af_malloc_cfg(u32 **info, u32 ch, u32 mode, const struct t33_mem_ops *o)
{
	u32 *blk = (u32 *)o->vmalloc(12);
	u32 virt, phys;

	info[ch] = blk;
	if (!blk)
		o->oom();
	virt = (u32)(unsigned long)o->kmalloc(24576, 208);
	if (!virt)
		o->oom();
	phys = virt + 0x80000000U;
	if (phys & 7)
		o->unaligned();
	o->reg_write(ch, 31, mode ? 43 : 0x18108, phys);
	o->reg_write(ch, 31, mode ? 44 : 0x1810c, phys + 6144);
	o->reg_write(ch, 31, mode ? 45 : 0x18110, phys + 12288);
	o->reg_write(ch, 31, mode ? 46 : 0x18114, phys + 18432);
	blk[0] = 4;
	blk[1] = virt;
	blk[2] = phys;
	return o->reg_write(ch, 31, mode ? 47 : 0x18118, 0x100df);
}

static inline u32 t33_lce_kalloc(const struct t33_mem_ops *o, u32 size)
{
	u32 v = (u32)(unsigned long)o->kmalloc(size, 208);

	if (!v)
		o->oom();
	return v;
}

/* tisp_lce_malloc_cfg: lceBuffInfo (48 bytes per channel: sizes, virtual and physical addresses of four buffers) */
static inline int t33_lce_malloc_cfg(u32 *info, u32 ch, u32 lce_mode, u32 lsf_mode, const struct t33_mem_ops *o)
{
	u32 *e = info + ch * 12;
	u32 v;

	if (!e[4]) {
		v = t33_lce_kalloc(o, 11520);
		e[0] = 2;
		e[4] = v;
		e[8] = v + 0x80000000U;
	}
	if (!e[5]) {
		v = t33_lce_kalloc(o, 78880);
		e[1] = 4;
		e[5] = v;
		e[9] = v + 0x80000000U;
	}
	o->reg_write(ch, 16, lce_mode ? 20 : 0xc090, e[9]);
	o->reg_write(ch, 16, lce_mode ? 21 : 0xc094, e[9] + 19720);
	o->reg_write(ch, 16, lce_mode ? 22 : 0xc098, e[9] + 0x9a10);
	o->reg_write(ch, 16, lce_mode ? 23 : 0xc09c, e[9] + 0xe718);
	o->reg_write(ch, 16, lce_mode ? 17 : 0xc080, 3);
	if (!e[6]) {
		v = t33_lce_kalloc(o, 18496);
		e[2] = 1;
		e[6] = v;
		e[10] = v + 0x80000000U;
	}
	if (!e[7]) {
		v = t33_lce_kalloc(o, 9248);
		e[3] = 1;
		e[7] = v;
		e[11] = v + 0x80000000U;
	}
	v = e[11];
	o->reg_write(ch, 37, lsf_mode ? 7 : 0x25068, v);
	return o->reg_write(ch, 37, lsf_mode ? 8 : 0x2506c, v);
}

/*
 * tisp_lce_gauss_lpf: 7 x 7 Gaussian weights (strength byte, at least 9) normalised to 4096; the
 * quadrant r <= c <= 3 goes to out[9 - k] (10 words), the centre takes the rounding rest.
 */
static inline void t33_lce_gauss_lpf(const u8 *sigma, u32 *out)
{
	int w[49], dy, dx, r, c, k = 0;
	u16 tbl[49];
	u32 sg = *sigma, s0, tot = 0, sum = 0, i;

	if (sg < 9)
		sg = 9;
	s0 = sg * sg;
	for (dy = -3; dy <= 3; dy++) {
		for (dx = -3; dx <= 3; dx++) {
			u32 r2 = (u32)(dy * dy + dx * dx);
			u32 x = ((r2 << 19) + ((s0 >> 1) & 0xffff)) / s0;
			int e = (int)t32_calc_exp(x, 15, 10, 8);
			int v = (int)t32_lce_sdiv((u32)((e >> 1) + 0x100000), (u32)e);

			w[(dy + 3) * 7 + dx + 3] = v;
			tot += (u32)v;
		}
	}
	for (i = 0; i < 49; i++) {
		unsigned long long num = (unsigned long long)(long long)w[i] << 12;

		tbl[i] = (u16)t32_tp_div64(num, (unsigned long long)(long long)(int)tot);
		sum = (sum + tbl[i]) & 0xffff;
	}
	if (sum != 4096)
		tbl[24] = (u16)(tbl[24] + 4096 - sum);
	for (r = 0; r < 4; r++) {
		for (c = r; c < 4; c++) {
			out[9 - k] = tbl[7 * r + c];
			k++;
		}
	}
}

#endif /* TX_ISP_T33_R11D_H */
