/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) defog per frame (openimp #878): the vendor's tisp_defog_init
 * registers system irq 10 (tisp_defog_interrupt_static), event 3
 * (tisp_defog_par) and irq 47 (tisp_defog_t33_frame_done_interrupt): vendor
 * T33 ko tisp_defog_init 0x6d770 (li a1,10), 0x6d78c (li a1,3, event_set_cb),
 * 0x6d7ac (li a1,47).  Before #878 the open T33 path ran the init only.
 *
 * The compute parts are the T32 ports (tx_isp_t32_defog.h / _defogs.h): the
 * vendor T33 Tiziano_defog_soft, img_filter9 / 25, max_filter are
 * instruction-equal to the T32 ones; tisp_defog_get_data differs only in its
 * data address; interp_trans_k, update_trans_k, update_reg_para2, par and
 * ev_update read the IQ section 4 bytes further on from T32 byte 190 on (the
 * T33 section has 16 instead of 15 + 1 words at +128; T32 +191 -> T33 +195,
 * +251 / +256 -> +255 / +260, +590 -> +594, +610..+646 -> +614..+650,
 * +195 -> +199, update_reg_para2 +190..+226 -> +194..+230; objdump of both
 * vendor kos).  The parameter block (3948 B, defog_info word 4) has the same
 * layout on both (LUT bytes +2458 / +2638 / +2818 / +2998, irq strength
 * byte +2420, cache flags +3178).
 *
 * defog_info[0] (vendor 56 B, t33_swg_defog_blk): word 1 the DMA buffer, 2 its
 * physical address, 3 the section, 4 the parameter block, 6 the EV
 * (hi << 22 | lo >> 10), 7 the second EV value of ev_update, 8 / 9 height /
 * width, 11 the interrupt count.
 */
#ifndef TX_ISP_T33_DEFOGPF_H
#define TX_ISP_T33_DEFOGPF_H

#include "tx_isp_t32_defogs.h"

#define T33_DPF_IRQ_BIT		10U	/* 0x30028 bit 10: tisp_defog_interrupt_static */
#define T33_DPF_FD_BIT		15U	/* msca bit 15 = system irq 47: tisp_defog_t33_frame_done_interrupt */
#define T33_DPF_EVENT		3U
#define T33_DPF_BANK_REG	0xa230U
#define T33_DPF_GRID		180U

enum { T33_DPF_I_BUF = 1, T33_DPF_I_PHYS = 2, T33_DPF_I_EV = 6, T33_DPF_I_EV2 = 7, T33_DPF_I_IRQ = 11 };

static inline u32 t33_dpf_le32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/* tisp_defog_interp_trans_k (T33: descending flag at +195; thresholds +8, descending +44 with the EV2 word) */
static inline void t33_dpf_interp_trans_k(const u8 *s, u32 ev, u32 ev2, const u8 *thr, const u8 *ta,
					  const u8 *tb, const u8 *tc, const u8 *td, const u8 *te, u8 *out)
{
	const u8 *tab[5] = { ta, tb, tc, td, te };
	int desc = s[195] != 0, found = 0;
	u32 k, i;

	if (desc) {
		thr = s + 44;
		ev = ev2;
	}
	for (k = 0; k < 9; k++) {
		u32 th = t33_dpf_le32(thr + 4 * k);

		if (desc ? th < ev : ev < th) {
			found = 1;
			break;
		}
	}
	if (!found || k == 0) {
		for (i = 0; i < 5; i++)
			out[i] = tab[i][found ? 0 : 8];
		return;
	}
	{
		u32 thp = t33_dpf_le32(thr + 4 * (k - 1)), thc = t33_dpf_le32(thr + 4 * k);
		u32 wp = desc ? ev - thc : thc - ev, wc = desc ? thp - ev : ev - thp, span = wp + wc;

		for (i = 0; i < 5; i++)
			out[i] = (u8)t32_defog_div((u32)tab[i][k - 1] * wp + (u32)tab[i][k] * wc + (span >> 1), span);
	}
}

/* tisp_defog_update_trans_k (vendor T33 0x79d54): EV word +4, flag +594, tables +614..+650, result +260 */
static inline void t33_dpf_update_trans_k(u8 *s, const u32 *info, struct t32_defog_par *p)
{
	u8 old[5];
	u32 i, ev = info[T33_DPF_I_EV];

	memcpy(old, s + 260, 5);
	if (s[594] || t33_dpf_le32(s + 4) == ev)
		return;
	s[4] = (u8)ev;
	s[5] = (u8)(ev >> 8);
	s[6] = (u8)(ev >> 16);
	s[7] = (u8)(ev >> 24);
	t33_dpf_interp_trans_k(s, ev, info[T33_DPF_I_EV2], s + 8, s + 614, s + 623, s + 632, s + 641, s + 650,
			       s + 260);
	if (!memcmp(old, s + 260, 5))
		return;
	for (i = 0; i < T33_DPF_GRID; i++)	/* par + 3178 .. + 3357: the curve cache flags */
		p->b[3178 + i] &= 0xfU;
}

/* tisp_defog_update_reg_para2's parameter block part (the list words are t33_defog_para2) */
static inline void t33_dpf_update_reg_para2(const u8 *s, struct t32_defog_par *p)
{
	p->b[2415] = s[194];
	p->b[2416] = s[196];
	p->b[2417] = s[197];
	p->b[2418] = s[198];
	p->b[2420] = s[199];
	p->b[2421] = s[200];
	p->b[2422] = s[201];
	p->b[2423] = s[202];
	p->b[2424] = s[203];
	memcpy(p->b + 2381, s + 221, 9);
	memcpy(p->b + 2390, s + 230, 25);
	memcpy(p->b + 2364, s + 204, 17);
	p->tbl_a = s + 255;	/* vendor par + 2176 */
	p->tbl_b = s + 260;	/* vendor par + 2180 */
}

/*
 * tisp_defog_interrupt_static (vendor T33 0x7cf64): the statistics of the DMA bank the core reports at
 * 0xa230 into the parameter block, the interrupt count (255 once it passed 2^30); the caller pushes event 3.
 */
static inline void t33_dpf_interrupt_static(u32 *info, struct t32_defog_par *p, u32 bank, const u8 *buf,
					    void (*sync)(const u8 *b, u32 len))
{
	u32 k, n = info[T33_DPF_I_IRQ];

	for (k = 0; k < 4; k++)
		if (bank == info[T33_DPF_I_PHYS] + 0x800U * k) {
			if (sync)
				sync(buf + 0x800U * k, 0x800U);
			t32_defog_get_data(p, buf + 0x800U * k);
		}
	info[T33_DPF_I_IRQ] = (n >> 30) == 1 ? 255U : n + 1;
}

/* tisp_defog_par (vendor T33 0x7ce58) up to write_reg_para3: trans-k, the strength byte, Tiziano_defog_soft */
static inline void t33_dpf_par(u8 *s, const u32 *info, struct t32_defog_par *p)
{
	t33_dpf_update_trans_k(s, info, p);
	p->b[2420] = info[T33_DPF_I_IRQ] < 2U ? 0 : s[199];
	t32_defog_soft(p);
}

/*
 * tisp_defog_write_reg_para3 (vendor T33 0x7c050), cfg mode (slot indexes): 0x8e = 0, 0x90 = 0x101, 180 LUT
 * words 0x92.., 0x147 = 0xb30102, 0x149 = 1; the caller sets tispDefogRefresh
 */
static inline void t33_dpf_write_reg_para3(const struct t32_defog_par *p, void (*wr)(u32 slot, u32 val))
{
	u32 j;

	wr(0x8e, 0);
	wr(0x90, 0x101);
	for (j = 0; j < T33_DPF_GRID; j++)
		wr(0x92 + j, t32_defog_lut_word(p->b, j));
	wr(0x147, 0xb30102);
	wr(0x149, 1);
}

/*
 * tisp_defog_ev_update (vendor T33 0x7da40, from tisp_long_ev_update with its x / y words): as the vendor,
 * update_trans_k runs before the new EV word is stored (the T32 port does the same); the caller sets
 * tispDefogRefresh.
 */
static inline void t33_dpf_ev_update(u8 *s, u32 *info, struct t32_defog_par *p, u32 lo, u32 hi, u32 x, u32 y)
{
	u32 ev = hi << 22 | lo >> 10;

	if (ev != info[T33_DPF_I_EV] && s[195] == 0)
		t33_dpf_update_trans_k(s, info, p);
	info[T33_DPF_I_EV] = ev;
	if ((info[T33_DPF_I_EV2] != x || y != 0) && s[195] == 1) {
		info[T33_DPF_I_EV2] = x;
		t33_dpf_update_trans_k(s, info, p);
	}
}

#endif /* TX_ISP_T33_DEFOGPF_H */
