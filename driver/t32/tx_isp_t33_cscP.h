/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 CSC parameter plumbing (openimp #99), binary oracle block "cscP":
 *
 * - tisp_csc_para_refresh_by_mode: refresh byte 1 copies one of the six
 *   built-in CSC tables (BT601/709/2020, full/limited) into the working
 *   block; otherwise the API table of the mode is copied when its flag is 1.
 * - tisp_csc_update_para: the working block (csc_para[ch]->src) into the
 *   register block (->dst): bytes 88..90, the s16 at 76/78/80, the nine
 *   coefficients at 4..36 (rounded, some negated) and the offsets.
 * - tisp_csc_sync_api_para: copy between the working block and the API block
 *   (->api) in either direction, gated by the API mode bytes 100 / 101.
 */
#ifndef TX_ISP_T33_CSCP_H
#define TX_ISP_T33_CSCP_H

#include "tx_isp_t33_blk.h"

extern long long tisp_round_int64(long long x, int a, int b);

#define T33_CSCP_TABLE_BYTES	92

/* csc_para[ch]: three block pointers */
struct t33_cscp {
	u8 *src;
	u8 *dst;
	u8 *api;
};

static inline void t33_cscp_store16(u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static inline void t33_cscp_store32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* tisp_csc_para_refresh_by_mode: returns the written block, or the mode when nothing is copied */
static inline u8 *t33_csc_para_refresh_by_mode(u32 mode, u32 refresh, u8 *dst,
					       const u8 *const *mode1_tab,
					       const u8 *flags,
					       const u8 *const *mode2_tab)
{
	const u8 *src;
	unsigned int k;

	refresh &= 0xff;
	if (refresh == 1) {
		if (mode >= 6)
			return (u8 *)(unsigned long)mode;
		src = mode1_tab[mode];
		for (k = 0; k < T33_CSCP_TABLE_BYTES; k++)
			dst[k] = src[k];
		return dst;
	}
	if (mode >= 7)
		return (u8 *)(unsigned long)mode;
	if (flags[mode] != 1)
		return (u8 *)(unsigned long)mode;
	src = mode2_tab[mode];
	dst[91] = src[0];
	for (k = 0; k < 6; k++)
		dst[82 + k] = src[40 + k];
	for (k = 0; k < 6; k++)
		dst[92 + k] = src[84 + k];
	for (k = 0; k < 36; k++)
		dst[4 + k] = src[4 + k];
	for (k = 0; k < 36; k++)
		dst[40 + k] = src[48 + k];
	return dst + 40;
}

/* tisp_csc_update_para: returns the word stored at dst + 36 */
static inline u32 t33_csc_update_para(u32 ch, struct t33_cscp *const *csc_para)
{
	const struct t33_cscp *c = csc_para[ch & 0xff];
	const u8 *s = c->src;
	const u8 *api = c->api;
	u8 *dst = c->dst;
	const u8 *coef;
	u32 use_api, v;
	unsigned int k;

	use_api = (s[98] == 1) || (s[98] == 0 && s[99] < 2);

	dst[0] = s[88];
	dst[1] = s[89];
	dst[2] = s[90];
	t33_cscp_store16(dst + 4, t32_tp_le16(s + 76));
	t33_cscp_store16(dst + 6, t32_tp_le16(s + 78));
	t33_cscp_store16(dst + 8, t32_tp_le16(s + 80));

	coef = use_api ? s : api;
	for (k = 0; k < 9; k++) {
		int rv = (int)tisp_round_int64((long long)t33_s32(coef + 4 + 4 * k), 6, 1);

		if (k == 3 || k == 4 || k == 7 || k == 8)
			rv = -rv;
		t33_cscp_store16(dst + 10 + 2 * k, (u16)rv);
	}

	if (use_api) {
		dst[28] = 0;
		for (k = 0; k < 5; k++)
			dst[29 + k] = s[83 + k];
		v = s[91];
	} else {
		for (k = 0; k < 6; k++)
			dst[28 + k] = api[40 + k];
		v = t33_le32(api);
	}
	t33_cscp_store32(dst + 36, v);
	return v;
}

/* tisp_csc_sync_api_para: dir 1 = working block to API block, else back; returns 0 */
static inline int t33_csc_sync_api_para(u32 ch, u32 dir, struct t33_cscp *const *csc_para)
{
	const struct t33_cscp *c = csc_para[ch & 0xff];
	u8 *s = c->src;
	u8 *d = c->api;
	unsigned int k;

	if (dir == 1) {
		if (s[100] != 1)
			return 0;
		t33_cscp_store32(d, (u32)s[91]);
		for (k = 0; k < 6; k++)
			d[40 + k] = s[82 + k];
		for (k = 0; k < 6; k++)
			d[84 + k] = s[92 + k];
		for (k = 0; k < 36; k++)
			d[4 + k] = s[4 + k];
		for (k = 0; k < 36; k++)
			d[48 + k] = s[40 + k];
	} else {
		if (s[101] != 1)
			return 0;
		s[91] = d[0];
		for (k = 0; k < 6; k++)
			s[82 + k] = d[40 + k];
		for (k = 0; k < 6; k++)
			s[92 + k] = d[84 + k];
		for (k = 0; k < 36; k++)
			s[4 + k] = d[4 + k];
		for (k = 0; k < 36; k++)
			s[40 + k] = d[48 + k];
	}
	return 0;
}

#endif /* TX_ISP_T33_CSCP_H */
