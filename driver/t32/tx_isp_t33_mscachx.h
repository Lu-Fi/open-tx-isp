/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA channel config load / enable (vendor tisp_msca_t33_chx_cfg_load and
 * tisp_msca_t33_chx_en, hand port, binary oracle block "mscachx"; openimp #99, #643).
 *
 *  - chx_cfg_load (id, ch): byte 202 + ch of the MSCA parameter block (msca_info[id] word 0)
 *    = 1, then tisp_msca_t33_scaling_algorithm (id, ch unmasked as the vendor's a1),
 *    tisp_msca_t33_curve_calc (id, ch), tisp_msca_t33_ch_curve_write (id, ch) and
 *    tisp_msca_t33_ctrl_reg_write (id, ch) (tail call);
 *  - chx_en (id, ch, en): the enable word (mscaCfgMode: list 23 slot 3, else 0xf0040) read,
 *    the FIFO clear words (slot 30 * ch + 42 / + 50, else 0xf0144 / 0xf0164 + ch * 0x100) = 0,
 *    then the enable word with bit ch set when en == 1 and stMscaAttr[id] byte 6032 + ch == 1
 *    (the stream-on flag of tisp_msca_t33_api_chx_stream_on), else cleared.
 * The callees and the module state are passed in struct t33_mscachx.
 */
#ifndef TX_ISP_T33_MSCACHX_H
#define TX_ISP_T33_MSCACHX_H

#include "tx_isp_t33_blk.h"

struct t33_mscachx {
	u8 *const *msca_info;	/* msca_info[]: word 0 = the MSCA parameter block */
	u8 *const *attr;	/* stMscaAttr[] */
	const u8 *cfg_mode;	/* mscaCfgMode */
	void (*scaling)(u32 id, u32 ch);		/* tisp_msca_t33_scaling_algorithm */
	void (*curve_calc)(u32 id, u32 ch);		/* tisp_msca_t33_curve_calc */
	void (*curve_write)(u32 id, u32 ch);		/* tisp_msca_t33_ch_curve_write */
	int (*ctrl_write)(u32 id, u32 ch);		/* tisp_msca_t33_ctrl_reg_write */
	u32 (*reg_read)(u32 id, u32 blk, u32 reg);	/* tisp_reg_read_t33 */
	void (*reg_write)(u32 id, u32 blk, u32 reg, u32 val);	/* tisp_reg_write_t33 */
};

/* tisp_msca_t33_chx_cfg_load */
static inline int t33_msca_chx_cfg_load_v(const struct t33_mscachx *m, u32 id, u32 ch)
{
	u32 i = id & 0xff, c = ch & 0xff;
	u8 *p = *(u8 **)m->msca_info[i];

	p[c + 202] = 1;
	m->scaling(i, ch);
	m->curve_calc(i, c);
	m->curve_write(i, c);
	return m->ctrl_write(i, c);
}

/* tisp_msca_t33_chx_en */
static inline void t33_msca_chx_en_v(const struct t33_mscachx *m, u32 id, u32 ch, u32 en)
{
	u32 i = id & 0xff, c = ch & 0xff, e = en & 0xff, cur;

	cur = m->reg_read(i, 23, *m->cfg_mode ? 3 : 0xf0040);
	m->reg_write(i, 23, *m->cfg_mode ? 30 * c + 42 : (c << 8) + 0xf0144, 0);
	m->reg_write(i, 23, *m->cfg_mode ? 30 * c + 50 : (c << 8) + 0xf0164, 0);
	if (e == 1 && m->attr[i][6032 + c] == e)
		cur |= 1U << c;
	else
		cur &= ~(1U << c);
	m->reg_write(i, 23, *m->cfg_mode ? 3 : 0xf0040, cur);
}

#endif /* TX_ISP_T33_MSCACHX_H */
