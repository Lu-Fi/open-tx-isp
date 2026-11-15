/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 statistics interrupts: tisp_ae_interrupt_hist, tisp_ae_interrupt_static, tisp_awb_interrupt_static.
 * Hardware / event / list-trigger callees are reached through struct t33_irqa_ops (same code in driver and oracle).
 */
#ifndef TX_ISP_T33_IRQA_H
#define TX_ISP_T33_IRQA_H

#include "tx_isp_t33_blk.h"
#include "tx_isp_t33_stats.h"

struct t33_irqa_ops {
	u32 (*reg_read)(u32 reg);
	int (*event_push)(u32 ch, void *ev);
	int (*trig)(u32 ch, u32 id);
	int (*set_hw)(u32 ch);
};

/* tisp_ae_interrupt_hist: runs tisp_normal_ae_interrupt_static (the vendor names are crossed), returns 1 */
static inline int t33_irqa_ae_hist(u32 ch, int (*fn_hist)(u32 ch))
{
	(void)fn_hist(ch);
	return 1;
}

/* tisp_ae_interrupt_static: runs tisp_normal_ae_interrupt_hist, returns 1 */
static inline int t33_irqa_ae_static(u32 ch, int (*fn_stat)(u32 ch))
{
	(void)fn_stat(ch);
	return 1;
}

static inline void t33_irqa_put16(u8 *p, u32 off, u32 v)
{
	p[off] = (u8)v;
	p[off + 1] = (u8)(v >> 8);
}

static inline void t33_irqa_put32(u8 *p, u32 off, u32 v)
{
	t33_irqa_put16(p, off, v);
	t33_irqa_put16(p, off + 2, v >> 16);
}

/* AWB parameter bytes (block 3 of awbInfo[ch]) that take the low byte of the 30 zone halfwords */
static const u8 t33_irqa_awb_out[30] = {
	104, 121, 112, 110, 148, 91, 141, 129, 134, 89, 88, 122, 109, 125, 147,
	128, 145, 94, 113, 144, 105, 98, 118, 130, 149, 142, 133, 99, 119, 93,
};

/* tisp_awb_interrupt_static */
static inline int t33_awb_int_static(u32 ch, u32 *num_stat, const u32 *awb_info, const u8 *init_par,
				     u8 *awb_refresh, u8 *awbg_refresh, const struct t33_irqa_ops *o)
{
	u32 ev[3];

	ch &= 0xff;
	num_stat[ch] = o->reg_read(T33_AWB_IDX_REG) & 3;
	ev[0] = 0;
	ev[1] = 0;
	ev[2] = 12;
	o->event_push(ch, ev);
	if (t33_le32(init_par + 904) == 1) {
		u32 refresh = *awb_refresh;

		if (refresh == 1) {
			o->trig(ch, 29);
			*awb_refresh = 0;
		} else if (refresh == 2) {
			u8 *a = (u8 *)(unsigned long)awb_info[2 * 0 + ch];
			u8 *t = (u8 *)(unsigned long)((const u32 *)(unsigned long)0 ? 0 : 0);
			u8 *b;
			u32 t3, t2, a2, a3, w1, w2, t1, a1, k;
			int n1, n2;

			t = (u8 *)(unsigned long)t33_le32(a + 4);
			b = (u8 *)(unsigned long)t33_le32(a + 8);
			a = (u8 *)(unsigned long)t33_le32(a);
			t3 = t33_st_le16(a + 3188);
			t2 = t33_st_le16(a + 3184);
			a2 = t33_st_le16(a + 3186);
			a3 = t33_st_le16(a + 3190);
			w1 = t33_st_le16(t + 28690);
			w2 = t33_st_le16(t + 28692);
			n1 = (int)(w1 - t3 + 1) / 2;
			for (;;) {
				t1 = (u32)(n1 / (int)a3) & 0xffff;
				if (t1 < 127)
					break;
				a3 = (a3 + 1) & 0xffff;
			}
			n2 = (int)(w2 - t2 + 1) / 2;
			for (;;) {
				a1 = (u32)(n2 / (int)a2) & 0xffff;
				if (a1 < 127)
					break;
				a2 = (a2 + 1) & 0xffff;
			}
			t33_irqa_put16(a, 3186, a2);
			t33_irqa_put16(a, 3190, a3);
			if (!t[38057] && !t[0x10438]) {
				t33_irqa_put32(a, 8, 1);
				t33_irqa_put32(a, 12, 0);
			}
			t[38057] = 0;
			for (k = 0; k < 30; k++)
				t33_irqa_put16(a, 3192 + 2 * k, k < 15 ? t1 : a1);
			t33_irqa_put16(a, 3290, t1);
			t33_irqa_put16(a, 3292, a1);
			t33_irqa_put16(b, 24, t3);
			t33_irqa_put16(b, 12, t2);
			for (k = 0; k < 30; k++)
				b[t33_irqa_awb_out[k]] = (u8)t33_st_le16(a + 3192 + 2 * k);
			b[117] = (u8)a3;
			b[140] = (u8)a2;
			o->set_hw(ch);
		}
		if (*awbg_refresh == 1) {
			o->trig(ch, 7);
			*awbg_refresh = 0;
		}
	}
	return 1;
}

#endif /* TX_ISP_T33_IRQA_H */
