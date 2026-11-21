/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) LSC mirror / flip (openimp #727): tisp_lsc_hvflip (vendor tx-isp-t33.ko 0xf9c18, tuning id 0x73 after
 * the sensor mirror / flip) -> tisp_lsc_mirror_flip (0x3feb0) -> do_mirror_flip (0x3cc14) on the vendor-layout
 * lsc_para[ch] (tx_isp_t33_bst.h: word 0 the section, 1 the 0x20-byte parameters, 2 the mesh info, 3 the 0xc-byte
 * block) and the CT tables of stLutInfo[ch] + 0x14.  Binary oracle block t33-lscmf (tests/t33_binoracle).
 *
 * The T33 code differs from the T32 one (tx_isp_t32_lsc.h t32_lsc_mesh_flip): no channel lock, the table count is
 * section byte 0x94, the tables are T33_LSCMF_TABLE (0x3602) bytes apart from + 2 with three planes 0x1200 apart, the
 * grid is mesh info byte 10 (lines) x byte 11 (u16 elements per line); the counters in word 3 + 8 / + 9 grow by the
 * flip / mirror argument (not by one), parameter word 3 gets 0xff, then interp_by_ct(ch, CT u16 at + 8, 1),
 * func_lsc_para_update(ch), tisp_lsc_write_reg(ch, 2) and the printk of the two counters.
 */
#ifndef TX_ISP_T33_LSCMF_H
#define TX_ISP_T33_LSCMF_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
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

#define T33_LSCMF_TABLE		0x3602U	/* one CT table: u16 CT + three planes */
#define T33_LSCMF_PLANE		0x1200U

struct t33_lscmf_ops {
	int (*interp_by_ct)(u32 ch, u32 ct, u32 force);	/* tisp_lsc_interp_by_ct */
	int (*para_update)(u32 ch);			/* func_lsc_para_update */
	int (*write_reg)(u32 ch, u32 mode);		/* tisp_lsc_write_reg */
};

static inline u32 t33_lscmf_ld32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

/*
 * do_mirror_flip(p, lines, cols, flip, mirror): flip == 1 exchanges line i and lines - 1 - i (i < lines / 2, the
 * vendor through a vmalloc'ed line buffer), mirror == 1 element j and cols - 1 - j of every line (j < cols / 2);
 * u16 elements, signed halves as the vendor's
 */
static inline void t33_lsc_do_mirror_flip(u8 *p, int lines, int cols, u32 flip, u32 mirror)
{
	int i, j;
	u8 t;

	if (flip == 1) {
		for (i = 0; i < lines / 2; i++) {
			u8 *a = p + 2 * i * cols, *b = p + 2 * (lines - 1 - i) * cols;

			for (j = 0; j < cols * 2; j++) {
				t = a[j];
				a[j] = b[j];
				b[j] = t;
			}
		}
	}
	if (mirror == 1) {
		for (i = 0; i < lines; i++) {
			u8 *l = p + 2 * i * cols;

			for (j = 0; j < cols / 2; j++) {
				u8 *a = l + 2 * j, *b = l + 2 * (cols - 1 - j);

				t = a[0]; a[0] = b[0]; b[0] = t;
				t = a[1]; a[1] = b[1]; b[1] = t;
			}
		}
	}
}

/*
 * tisp_lsc_mirror_flip(ch, a1, a2, flip, mirror) on blk = lsc_para[ch] (four u32 pointer words) and tab =
 * stLutInfo[ch] + 0x14 (the CT tables)
 */
static inline void t33_lsc_mirror_flip(u32 ch, const u32 *blk, u8 *tab, u32 flip, u32 mirror,
				       const struct t33_lscmf_ops *ops)
{
	const u8 *sec = (const u8 *)(unsigned long)blk[0], *info = (const u8 *)(unsigned long)blk[2];
	u8 *par = (u8 *)(unsigned long)blk[1], *cnt = (u8 *)(unsigned long)blk[3];
	u32 n = sec[0x94], t, ct;
	int lines = info[10], cols = info[11];

	for (t = 0; t < n; t++) {
		u8 *p = tab + 2 + t * T33_LSCMF_TABLE;

		t33_lsc_do_mirror_flip(p, lines, cols, flip, mirror);
		t33_lsc_do_mirror_flip(p + T33_LSCMF_PLANE, lines, cols, flip, mirror);
		t33_lsc_do_mirror_flip(p + 2 * T33_LSCMF_PLANE, lines, cols, flip, mirror);
	}
	cnt[8] = (u8)(cnt[8] + flip);
	cnt[9] = (u8)(cnt[9] + mirror);
	par[12] = 0xff;
	par[13] = 0;
	par[14] = 0;
	par[15] = 0;
	ct = t33_lscmf_ld32(par + 8) & 0xffff;
	ops->interp_by_ct(ch & 0xff, ct, 1);
	ops->para_update(ch);
	ops->write_reg(ch, 2);
}

#endif /* TX_ISP_T33_LSCMF_H */
