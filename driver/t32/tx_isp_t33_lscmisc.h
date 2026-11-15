/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 lens shading helpers working on the vendor's lsc info block (a word
 * array, lsc_para[ch]): tisp_lsc_bin2user, func_lsc_para_update,
 * tisp_lsc_lut_valid_check and tisp_lsc_write_reg.
 *
 * Info block words: 0 = IQ section pointer, 2 = pointer to the user block
 * (byte 16, bytes 4..11), 7..23 filled from the section, 24..48 the
 * derived copies / limits written to the registers.
 */
#ifndef TX_ISP_T33_LSCMISC_H
#define TX_ISP_T33_LSCMISC_H

#include "tx_isp_t33_blk.h"

/* tisp_lsc_bin2user: section bytes into the info words */
static inline void t33_lsc_bin_to_user(u32 *v)
{
	const u8 *s = (const u8 *)(unsigned long)v[0];
	const u8 *p = (const u8 *)(unsigned long)v[2];

	v[9] = s[145];
	v[10] = s[84];
	v[15] = s[90];
	v[16] = t33_le32(s + 8);
	v[17] = s[89];
	v[18] = s[88];
	v[19] = s[91];
	v[20] = s[92];
	v[21] = s[93];
	v[22] = t33_le32(s + 16);
	v[23] = t33_le32(s + 20);
	v[7] = p[16];
	v[8] = p[16];
	v[11] = s[146];
	v[12] = s[147];
	v[13] = s[85];
	v[14] = s[86];
}

/* func_lsc_para_update (after tisp_lsc_bin2user) */
static inline void t33_lsc_para_update_info(u32 *v)
{
	const u8 *p;

	t33_lsc_bin_to_user(v);
	p = (const u8 *)(unsigned long)v[2];
	v[34] = v[15];
	v[24] = v[7];
	v[35] = v[16];
	v[25] = v[8];
	v[38] = v[17];
	v[26] = v[9];
	v[39] = v[18];
	v[27] = v[10];
	v[42] = v[19];
	v[28] = v[11];
	v[43] = v[20];
	v[29] = v[12];
	v[44] = v[21];
	v[30] = v[13];
	v[31] = v[14];
	v[46] = v[22];
	v[47] = v[23];
	v[37] = p[8];
	v[36] = p[9];
	v[41] = p[10];
	v[40] = p[11];
	v[48] = t33_le32(p + 4);
	v[32] = (int)v[11] + 1 < (int)v[12] ? 1024 / ((int)v[12] - (int)v[11]) : 1023;
	v[33] = (int)v[13] + 1 < (int)v[14] ? 1024 / ((int)v[14] - (int)v[13]) : 1023;
	v[45] = v[23] * v[23] + v[22] * v[22];
}

/* tisp_lsc_lut_valid_check: mesh of cells mh x mw over a h x w sensor */
static inline void t33_lsc_mesh_check(int h, int w, int mh, int mw, u8 *rem_h, u8 *rem_w, u8 *rows,
				      u8 *cols, u32 *cells, u8 *valid, u32 *stride)
{
	int a = (h >> 1) + (mh >> 1), b = (w >> 1) + (mw >> 1), ra, rb;

	ra = a % mh;
	rb = b % mw;
	*rem_h = ra > 0 ? (u8)(mh - ra) : 0;
	*rem_w = rb > 0 ? (u8)(mw - rb) : 0;
	*cols = (u8)(((*rem_w != 0) + b / mw) * 2);
	*rows = (u8)(((*rem_h != 0) + a / mh) * 2);
	*cells = (u32)*rows * *cols;
	*valid = *cols < 93;
	*stride = (((u32)*cols * 36 + 63) & ~63U) >> 3;
	if (!*cols)
		*cols = 1;
	if ((int)*cells > 2304) {
		*cells = 2304;
		*rows = (u8)(2304 / *cols);
	}
}

typedef void (*t33_lsc_wr_fn)(u32 ch, u32 blk, u32 sel, u32 val);
typedef int (*t33_lsc_dma_fn)(u32 blk);

#define T33_LSC_W(sc, sh, val)	wr(ch, 6, cfg ? (sc) : (sh), (val))

/*
 * tisp_lsc_write_reg: register writes (slot when cfg, else register address);
 * returns the refresh state (1, or 2 while the DMA is busy).
 */
static inline int t33_lsc_write_regs(const u32 *v, u32 ch, u8 cfg, t33_lsc_wr_fn wr, t33_lsc_dma_fn dma)
{
	u32 c0 = (v[28] & 0xff) | v[26] << 31 | ((v[29] << 8) & 0xffff) | (v[32] & 0x3ff) << 16;
	u32 c1 = (v[30] & 0xff) | v[27] << 31 | ((v[31] << 8) & 0xffff) | (v[33] & 0x3ff) << 16;
	u32 c2 = (v[43] & 3) << 4 | (v[44] & 3) << 8 | (v[42] & 3);

	T33_LSC_W(5, 20552, c0);
	T33_LSC_W(7, 20560, (v[24] & 0xff) | (v[25] & 0xff) << 16);
	T33_LSC_W(14, 20592, c1);
	T33_LSC_W(4, 20548, c2);
	T33_LSC_W(3, 20544, (v[36] & 0x7f) << 8 | (v[39] & 0x7f) << 16 | (v[38] & 0x7f) | (v[37] & 0x7f) << 24);
	T33_LSC_W(4, 20548, c2);
	T33_LSC_W(5, 20552, c0);
	T33_LSC_W(8, 20564, v[46] << 16 | (v[47] & 0xffff));
	T33_LSC_W(9, 20568, v[45] & 0x7fffff);
	T33_LSC_W(11, 20576, (v[34] & 1) << 16);
	T33_LSC_W(12, 20580, v[35] & 0x7fffff);
	T33_LSC_W(14, 20592, c1);
	T33_LSC_W(15, 20596, v[48] & 0x1ff);
	T33_LSC_W(16, 20600, v[5]);
	T33_LSC_W(1, 20496, 1);
	return dma(6) ? 2 : 1;
}

#undef T33_LSC_W

/*
 * tisp_lsc_interp_by_tgain(ch, gain): v = lsc_para[ch] (word 0 the section, 1 the 0x20-byte parameters,
 * 2 the 0x34-byte mesh info): parameter word 5 = gain; section word 1 (byte 4) selects the CT / gain
 * strengths of the mesh info bytes 0x10 / 0x12 (0: CT by gain, 1: CT fixed, 2: gain by gain, else fixed).
 */
static inline void t33_lsc_interp_by_tgain_v(u32 *v, u32 gain)
{
	const u8 *s = (const u8 *)(unsigned long)v[0];
	u8 *mi = (u8 *)(unsigned long)v[2];
	u32 mode;

	((u32 *)(unsigned long)v[1])[5] = gain;
	mode = t33_le32(s + 4);
	if (mode == 0) {
		mi[16] = t32_tp_intp_u8(gain >> 16, gain & 0xffff, s + 0x85);
		mi[18] = 0xff;
	} else if (mode == 1) {
		mi[16] = s[0x90];
		mi[18] = 0xff;
	} else if (mode == 2) {
		mi[16] = 0xff;
		mi[18] = t32_tp_intp_u8(gain >> 16, gain & 0xffff, s + 0x48);
	} else {
		mi[16] = 0xff;
		mi[18] = s[0x90];
	}
}

/*
 * tisp_lsc_irq_frame_done (system irq 36): count[ch] the frame counter (16 bit); info byte 0x18 set
 * (a pending rewrite) -> clear + write_reg(ch, 2); with stIspInitPar word 226 == 1: refresh 1 starts
 * list 6, refresh 2 writes the slots again (write_reg sets the refresh state).
 */
static inline int t33_lsc_frame_done_v(u32 ch, u32 *const *lsc_para, u32 *count, const u8 *initpar, u8 *refresh,
				       int (*trig)(u32, u32), void (*write_reg)(u32, u32))
{
	u8 *v = (u8 *)lsc_para[ch];

	count[ch] = (count[ch] + 1) & 0xffff;
	if (v[0x18]) {
		v[0x18] = 0;
		write_reg(ch, 2);
	}
	if (t33_le32(initpar + 904) == 1) {
		if (*refresh == 1) {
			trig(ch & 0xff, 6);
			*refresh = 0;
		} else if (*refresh == 2) {
			write_reg(ch, 2);
		}
	}
	return 1;
}

#endif /* TX_ISP_T33_LSCMISC_H */
