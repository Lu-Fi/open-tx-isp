/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) MSCA line / rectangle draw block: readable C of the vendor module
 * tisp_msca_t33_api_set_line_out_cfg and tisp_msca_t33_set_line_reg.
 *
 * The channel attribute block is handled by byte offset (the vendor's stMscaAttr[ch]):
 * window n (36 bytes each) from +912: type @0, colour bytes @4..6, raw end points @8..14,
 * x @16, y @18, w @20, h @22, enable @24, colour index @25, @26, width @27, k @28,
 * @29, @30, @31; byte +944 of the block is the msk size.
 * mode != 0 (mscaCfgMode): every register is a slot of MSCA list 23, mode 0: direct address.
 */
#ifndef TX_ISP_T33_MSCALINE_H
#define TX_ISP_T33_MSCALINE_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_osd.h"

#define T33_ML_WIN	32

#define T33_ML_NOTE_FULL	0	/* colour list full */
#define T33_ML_NOTE_MSK		1	/* msk size error */
#define T33_ML_NOTE_SAME	2	/* line end points equal */
#define T33_ML_NOTE_TYPE	3	/* unknown line type */

struct t33_line_ops {
	void (*wr)(u32 ch, u32 reg, u32 v);	/* tisp_reg_write_t33(ch, 23, reg, v) */
	void (*refresh)(u32 ch);		/* tispMscaRefresh = 1 */
	void (*note)(u32 what);			/* diagnostic message */
};

static inline void t33_ml_w(const struct t33_line_ops *op, u32 mode, u32 ch, u32 slot, u32 addr, u32 v)
{
	op->wr(ch, mode ? slot : addr, v);
}

/* tisp_msca_t33_set_line_reg; cl = the colour list (4 words) */
static inline void t33_msca_line_set_reg(const u8 *a, const u32 *cl, u32 mode, const struct t33_line_ops *op, u32 ch)
{
	u32 base = mode ? 0x131 : 0xf2004, i, cnt = 0, s, type;
	const u8 *b;

	t33_ml_w(op, mode, ch, 0xa4, 0xf04c4, 1);
	for (i = 0; i < 4; i++)
		t33_ml_w(op, mode, ch, 0xc7 + i, 0xf04c0, cl[i]);
	t33_ml_w(op, mode, ch, 9, 0xf0058, a[944]);
	t33_ml_w(op, mode, ch, 0x12f, 0xf2000, 0x101);
	for (i = 0; i < T33_ML_WIN; i++) {
		b = a + 36 * i;
		s = base + 3 * i;
		if (b[936] != 1)
			continue;
		type = t33_osd_u32(b + 912);
		op->wr(ch, s, (u32)t33_osd_u16(b + 930) << 12 | t33_osd_u16(b + 928) | (type + 1) << 24 | 0x4000000U);
		op->wr(ch, s + 1, (u32)t33_osd_u16(b + 934) << 12 | (u32)b[937] << 24 | t33_osd_u16(b + 932) | (u32)b[938] << 26);
		op->wr(ch, s + 2, (u32)b[940] << 8 | (u32)b[941] << 16 | b[939] | (u32)b[942] << 18 | (u32)b[943] << 23);
		cnt++;
	}
	for (; cnt < T33_ML_WIN; cnt++) {
		s = base + 3 * cnt;
		op->wr(ch, s, 0);
		op->wr(ch, s + 1, 0);
		op->wr(ch, s + 1, 0);
	}
	t33_ml_w(op, mode, ch, 402, 0xf2000, 0x10102);
	op->refresh(ch);
}

/* colour search: cl[0..3] the list, cl[4] its fill count */
static inline void t33_ml_color(u8 *b, u32 *cl, const struct t33_line_ops *op, u32 c)
{
	u32 i;

	for (i = 0; i < 4; i++) {
		if (cl[i] == c) {
			b[937] = (u8)i;
			return;
		}
	}
	if (cl[4] < 4) {
		cl[cl[4]] = c;
		b[937] = (u8)cl[4];
		cl[4]++;
	} else {
		op->note(T33_ML_NOTE_FULL);
	}
}

/* tisp_msca_t33_api_set_line_out_cfg: a = stMscaAttr[ch], p = the 36 byte request block (window number < 32) */
static inline void t33_msca_line_set_out(u8 *a, u32 *cl, u32 mode, const struct t33_line_ops *op, u32 ch, u8 *p)
{
	u8 *b = a + 36 * p[0];
	u32 type = t33_osd_u32(p + 4), x1, y1, x2, y2, dx = 0, dy = 0, q;
	u8 kn = 0;

	t33_osd_p32(b + 912, type);
	b[916] = p[25];
	b[917] = p[26];
	b[918] = p[27];
	x1 = t33_osd_u16(p + 14);
	y1 = t33_osd_u16(p + 16);
	x2 = t33_osd_u16(p + 18);
	y2 = t33_osd_u16(p + 20);
	switch (type) {
	case 0:
	case 1:
	case 3:
		t33_ml_color(b, cl, op, (u32)p[25] << 16 | (u32)p[26] << 8 | p[27]);
		if (type == 3) {
			if ((u8)(p[1] - 8) < 0x39) {
				a[944] = p[1];
			} else {
				op->note(T33_ML_NOTE_MSK);
				p[1] = 8;
				a[944] = 8;
			}
		}
		t33_osd_p16(b + 928, (u16)x1);
		t33_osd_p16(b + 930, (u16)y1);
		t33_osd_p16(b + 932, (u16)x2);
		t33_osd_p16(b + 934, (u16)y2);
		b[936] = p[12];
		b[939] = p[28];
		b[940] = type == 1 ? (u8)t33_osd_u16(p + 30) : 5;
		b[941] = 0;
		b[942] = 1;
		b[943] = type == 3 ? 0 : p[29];
		b[938] = type == 1 ? p[32] : p[30];
		break;
	case 2:
		t33_ml_color(b, cl, op, (u32)p[25] << 16 | (u32)p[26] << 8 | p[27]);
		t33_osd_p16(b + 920, (u16)x1);
		t33_osd_p16(b + 922, (u16)y1);
		t33_osd_p16(b + 924, (u16)x2);
		t33_osd_p16(b + 926, (u16)y2);
		b[936] = p[12];
		b[938] = 0;
		b[939] = p[28];
		b[940] = 5;
		b[941] = 0;
		b[942] = 1;
		b[943] = p[29];
		if (p[12] == 1 && x1 == x2 && y1 == y2) {
			op->note(T33_ML_NOTE_SAME);
			return;
		}
		if (x1 < x2) {
			t33_osd_p16(b + 928, (u16)x1);
			dx = x2 - x1;
		} else {
			t33_osd_p16(b + 928, (u16)x2);
			dx = x1 - x2;
		}
		if (y1 < y2) {
			t33_osd_p16(b + 930, (u16)y1);
			dy = y2 - y1;
		} else {
			t33_osd_p16(b + 930, (u16)y2);
			dy = y1 - y2;
		}
		if (x1 == x2) {
			b[940] = 0;
			dx = b[939];
			b[941] = 0;
			b[942] = 0;
		} else if (y1 == y2) {
			b[940] = 0xff;
			dy = b[939];
			b[941] = 2;
			b[942] = 0x1f;
		} else {
			q = dx / dy;
			if ((x2 < x1 && y2 < y1) || (x1 < x2 && y1 < y2))
				b[941] = q ? 2 : 0;
			else
				b[941] = q ? 3 : 1;
			t33_find_k_n(dx * 1000000 / dy, &b[940], &kn);
			b[942] = (signed char)kn < 0 ? (u8)-(signed char)kn : (u8)(kn | 0x10);
		}
		t33_osd_p16(b + 932, (u16)dx);
		t33_osd_p16(b + 934, (u16)dy);
		break;
	default:
		op->note(T33_ML_NOTE_TYPE);
		break;
	}
	t33_msca_line_set_reg(a, cl, mode, op, ch);
}

#endif /* TX_ISP_T33_MSCALINE_H */
