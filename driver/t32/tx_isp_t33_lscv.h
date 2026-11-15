/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) LSC on the vendor's lsc_para layout (openimp #99 group 13b): tisp_lsc_init's block
 * (0xc4 bytes) and the CT step of tisp_lsc_interp_by_ct on it.
 *
 * Block words: 0 the IQ section (the vendor: tparamsP + 17488), 1 the 0x20-byte parameters (0 / 1 sensor
 * height / width, 2 CT, 3 the last CT zone, 5 gain), 2 the 0x34-byte mesh info (0 cells, 4 stride, 8..11
 * remainders / rows / cols, 0x10 / 0x12 / 0x13 CT / gain / last strength, 0x14 the shift, 0x15 valid,
 * 0x18 the table CTs, 0x1c..0x30 the six planes), 3 a 0xc-byte block, 4 / 5 the mesh table (virtual /
 * physical, tisp_lsc_malloc_info), byte 0x18 the frame done's pending-rewrite flag; words 7..48 the
 * register values (func_lsc_para_update, tx_isp_t33_lscmisc.h).
 *
 * The CT step is t33_lsc_ct of tx_isp_t33_lsc.h (the init builder's blend / strength / distance /
 * packing, QEMU-equal to the vendor, openimp #388) on the mesh info's fields; the planes and CTs of the
 * mesh info are those of its struct t33_lsc.
 */
#ifndef TX_ISP_T33_LSCV_H
#define TX_ISP_T33_LSCV_H

#include "tx_isp_t33_lsc.h"
#include "tx_isp_t33_lscmisc.h"

struct t33_lsc_vblk {
	u32 blk[0xc4 / 4], par[8], mi[0x34 / 4], w3[3];
};

/* tisp_lsc_interp_by_ct(ch, ct, 1) on block v: m holds the planes / CTs, tab the bin's CT tables */
static inline void t33_lsc_ct_info(u32 *v, struct t33_lsc *m, const u8 *tab, u32 ct)
{
	u32 *pa = (u32 *)(unsigned long)v[1];
	const u8 *s = (const u8 *)(unsigned long)v[0];
	u8 *mi = (u8 *)(unsigned long)v[2];

	ct &= 0xffff;
	mi[0x14] = s[0x5b];
	m->cells = t33_le32(mi);
	m->stride = t33_le32(mi + 4);
	m->rem_h = mi[8];
	m->rem_w = mi[9];
	m->rows = mi[10];
	m->cols = mi[11];
	m->str_ct = mi[0x10];
	m->str = mi[0x12];
	m->shift = mi[0x14];
	m->valid = mi[0x15];
	pa[2] = ct;
	pa[3] = t33_lsc_ct_part(ct, s[0x94], m->ct, s + 0x28);
	t33_lsc_ct(m, s, tab, ct, (u8 *)(unsigned long)v[4]);
	mi[0x13] = m->str_last;
}

/*
 * tisp_lsc_init up to its write_reg (channel 0): sec the section copy the block points to (its cell
 * size falls back to 80 x 80 like the vendor's), tab / tabbytes the CT tables, h x w the sensor, gain
 * 16.16, ct in K, lut / lut_phys the mesh table.  Returns 0, -1 when the tables do not fit (block unset).
 */
static inline int t33_lsc_vinit(struct t33_lsc_vblk *b, struct t33_lsc *m, u8 *sec, const u8 *tab, u32 tabbytes,
				u32 h, u32 w, u32 gain, u32 ct, u8 *lut, u32 lut_phys)
{
	u32 *v = b->blk, i, n = sec[0x94];
	u8 *mi = (u8 *)b->mi;

	memset(b, 0, sizeof(*b));
	if (!n || n > 8 || tabbytes < n * T33_LSC_TABLE_BYTES)
		return -1;
	v[0] = (u32)(unsigned long)sec;
	v[1] = (u32)(unsigned long)b->par;
	v[2] = (u32)(unsigned long)b->mi;
	v[3] = (u32)(unsigned long)b->w3;
	b->par[0] = h;
	b->par[1] = w;
	b->par[2] = ct;
	b->par[3] = 0xff;
	b->par[5] = gain;
	mi[0x13] = 0xff;
	t33_lsc_mesh_check((int)h, (int)w, sec[0x58], sec[0x59], mi + 8, mi + 9, mi + 10, mi + 11, (u32 *)mi,
			   mi + 0x15, (u32 *)(mi + 4));
	if (!mi[0x15]) {
		sec[0x58] = 0x50;
		sec[0x59] = 0x50;
		t33_lsc_mesh_check((int)h, (int)w, 0x50, 0x50, mi + 8, mi + 9, mi + 10, mi + 11, (u32 *)mi, mi + 0x15,
				   (u32 *)(mi + 4));
	}
	b->mi[6] = (u32)(unsigned long)m->ct;
	for (i = 0; i < 6; i++)
		b->mi[7 + i] = (u32)(unsigned long)m->pl[i];
	for (i = 0; i < n; i++)
		m->ct[i] = (u16)t32_tp_le16(tab + i * T33_LSC_TABLE_BYTES);
	v[4] = (u32)(unsigned long)lut;	/* tisp_lsc_malloc_info */
	v[5] = lut_phys;
	t33_lsc_interp_by_tgain_v(v, gain);
	t33_lsc_ct_info(v, m, tab, ct);
	t33_lsc_para_update_info(v);
	return 0;
}

#endif /* TX_ISP_T33_LSCV_H */
