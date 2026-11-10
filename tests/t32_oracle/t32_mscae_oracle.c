// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA API get/set accessors (tx_isp_t32_msca_api.h).
 * Runs the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc) and our port from the same random channel block and user buffer
 * and compares the channel block (memcmp of all 2636 bytes), the user buffer
 * (2640 bytes), the flags the set functions raise (e275c, f3258) and that the
 * other channels' blocks (decoy copies behind e2768[]) stay unchanged.
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_api.h"

extern u8 t32_data_f3258[512], t32_data_e275c[8];

#define BUF	2640
#define NF	12

extern u64 tisp_msca_api_get_par(u32, u32, u32, u32);
extern u64 tisp_msca_api_get_mask_out_cfg(u32, u32, u32, u32);
extern u64 tisp_msca_api_set_mask_out_cfg(u32, u32, u32, u32);
extern u64 tisp_msca_api_get_line_out_cfg(u32, u32, u32, u32);
extern u64 tisp_msca_api_get_osd_out_cfg(u32, u32, u32, u32);
extern u64 tisp_msca_api_get_osd_out_attribute_cfg(u32, u32, u32, u32);
extern u64 tisp_msca_api_get_fcrop(u32, u32, u32, u32);
extern u64 tisp_msca_api_get_autozoom(u32, u32, u32, u32);
extern u64 tisp_msca_get_mirr_flip(u32, u32, u32, u32);
extern u64 tisp_msca_set_mirr_flip(u32, u32, u32, u32);
extern u64 tisp_s_mscaler_mask_block_attr(u32, u32, u32, u32);
extern u64 tisp_g_mscaler_mask_block_attr(u32, u32, u32, u32);

static const char *const names[NF] = {
	"get_par", "get_mask", "set_mask", "get_line", "get_osd", "get_osd_attr", "get_fcrop",
	"get_autozoom", "get_mirr_flip", "set_mirr_flip", "s_mask_block_attr", "g_mask_block_attr"
};

static u32 rs = 0x7e1c4a9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct st {
	u8 sb[T32_MSCA_BLK_SZ];
	u8 buf[BUF];
	u8 flag, mark;
};

static void gen(struct st *s, int k)
{
	unsigned int i, st, d;

	for (i = 0; i < sizeof(s->sb); i++)
		s->sb[i] = (u8)rnd();
	for (i = 0; i < BUF; i++)
		s->buf[i] = (u8)rnd();
	for (st = 0; st < 3; st++)
		for (d = 0; d < 4; d++)
			s->sb[st * T32_MSCA_SET_SZ + (d + 2) * 16 + 20] = (u8)(rnd() % 3 == 0 ? rnd() % 3 : 1);
	for (st = 0; st < 4; st++) {
		s->sb[st * 28 + 116] = (u8)(rnd() % 4);
		s->sb[st * 20 + 680] = (u8)(rnd() % 3);
	}
	if (rnd() % 3 == 0)
		s->sb[20] = s->sb[21] = 0;
	if (rnd() % 2) {
		/* small values like the real blocks, so the 16 bit paths are not all-random */
		for (i = 0; i < 40; i++)
			s->sb[20 + 2 * i] &= 0x0f;
	}
	if (rnd() % 4 == 0) {
		s->sb[20] = 1;
		s->sb[21] = 0;
	}
	s->buf[0] = (u8)(rnd() % ((k == 1 || k == 2 || k >= 10) ? 3 : 4));
	s->buf[1] = (u8)(rnd() % ((k == 1 || k == 2 || k >= 10) ? 24 : 4));
	s->buf[2] = (u8)(rnd() % 3);
	if (k == 5)
		s->buf[29] = (u8)(rnd() % 8);
	else if (rnd() % 2)
		s->buf[29] = (u8)(rnd() % 8);
	if (k == 8 || k == 9)
		s->buf[0] = (u8)rnd();
	if (k == 8 || k == 9)
		if ((s->buf[0] & 3) == 3)
			s->buf[0] ^= 1;
	s->flag = (u8)rnd();
	s->mark = (u8)rnd();
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int c, k, i, compared = 0;

	for (c = 0; c < n; c++)
		for (k = 0; k < NF; k++) {
			static struct st in, o, r;
			u8 *sb[4], *dsb[4], *bp;
			u32 chn = rnd() % 3, ch2 = chn | (rnd() & ~0xffu);
			int mask_chn = k < 8 || k == 11;

			gen(&in, (int)k);
			o = in;
			r = in;
			t32o_reset();
			for (i = 0; i < 4; i++) {
				sb[i] = t32o_alloc(T32_MSCA_BLK_SZ);
				dsb[i] = t32o_alloc(T32_MSCA_BLK_SZ);
				memcpy(sb[i], in.sb, T32_MSCA_BLK_SZ);
				memcpy(dsb[i], in.sb, T32_MSCA_BLK_SZ);
				t32_data_e2768[i] = T32O_PTR(i == chn ? sb[i] : dsb[i]);
			}
			bp = t32o_alloc(BUF);
			memcpy(bp, in.buf, BUF);
			t32_data_e275c[chn] = in.flag;
			t32_data_f3258[chn * 32 + 14] = in.mark;
			t32o_call((t32o_fn)(void (*)(void))(k == 0 ? tisp_msca_api_get_par :
				k == 1 ? tisp_msca_api_get_mask_out_cfg : k == 2 ? tisp_msca_api_set_mask_out_cfg :
				k == 3 ? tisp_msca_api_get_line_out_cfg : k == 4 ? tisp_msca_api_get_osd_out_cfg :
				k == 5 ? tisp_msca_api_get_osd_out_attribute_cfg : k == 6 ? tisp_msca_api_get_fcrop :
				k == 7 ? tisp_msca_api_get_autozoom : k == 8 ? tisp_msca_get_mirr_flip :
				k == 9 ? tisp_msca_set_mirr_flip : k == 10 ? tisp_s_mscaler_mask_block_attr :
				tisp_g_mscaler_mask_block_attr),
				  mask_chn ? ch2 : chn, T32O_PTR(bp), 0, 0, 0);
			memcpy(r.sb, sb[chn], T32_MSCA_BLK_SZ);
			memcpy(r.buf, bp, BUF);
			r.flag = t32_data_e275c[chn];
			r.mark = t32_data_f3258[chn * 32 + 14];
			for (i = 0; i < 4; i++)
				if (i != chn && memcmp(dsb[i], in.sb, T32_MSCA_BLK_SZ)) {
					printf("MISMATCH %s: vendor changed another channel's block\n", names[k]);
					return 1;
				}
			switch (k) {
			case 0: t32_msca_api_get_par(o.sb, o.buf); break;
			case 1: case 11: t32_msca_api_get_mask_out_cfg(o.sb, o.buf); break;
			case 2: t32_msca_api_set_mask_out_cfg(o.sb, o.buf); break;
			case 3: t32_msca_api_get_line_out_cfg(o.sb, o.buf); break;
			case 4: t32_msca_api_get_osd_out_cfg(o.sb, o.buf); break;
			case 5: t32_msca_api_get_osd_out_attr_cfg(o.sb, o.buf); break;
			case 6: t32_msca_api_get_fcrop(o.sb, o.buf); break;
			case 7: t32_msca_api_get_autozoom(o.sb, o.buf); break;
			case 8: t32_msca_get_mirr_flip(o.sb, o.buf); break;
			case 9: t32_msca_set_mirr_flip(o.sb, o.buf, &o.flag); break;
			default: t32_s_mscaler_mask_block_attr(o.sb, o.buf, &o.mark); break;
			}
			compared++;
			if (memcmp(r.sb, o.sb, T32_MSCA_BLK_SZ) || memcmp(r.buf, o.buf, BUF) ||
			    r.flag != o.flag || r.mark != o.mark) {
				for (i = 0; i < T32_MSCA_BLK_SZ; i++)
					if (r.sb[i] != o.sb[i] && getenv("T32O_DBG")) printf("sb[%u] %02x/%02x in %02x bufhdr %02x %02x %02x\n", i, r.sb[i], o.sb[i], in.sb[i], in.buf[0], in.buf[1], in.buf[2]);
				for (i = 0; i < BUF; i++)
					if (r.buf[i] != o.buf[i] && getenv("T32O_DBG")) printf("buf[%u] %02x/%02x in %02x mode %u\n", i, r.buf[i], o.buf[i], in.buf[i], in.sb[in.buf[0] * 28 + 116]);
				printf("MISMATCH %s: block %d buffer %d flag %u/%u mark %u/%u\n", names[k],
				       memcmp(r.sb, o.sb, T32_MSCA_BLK_SZ) != 0, memcmp(r.buf, o.buf, BUF) != 0,
				       r.flag, o.flag, r.mark, o.mark);
				return 1;
			}
		}
	printf("t32_mscae_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
