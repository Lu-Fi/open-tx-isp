// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA channel attribute setter (tx_isp_t32_msca_attr.h).
 * Runs the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc) and our port from the same random state; compares the whole
 * channel block, the autozoom flag, the isp_printf count, the osd return
 * value (the crop height), the saved autozoom area (e2650), and that the
 * defaults (dc284), the attribute block and all other channels' blocks stay
 * unchanged.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_attr.h"

#define BLK	2636

extern u64 lib_tisp_msca_channel_attr_set(u32, u32, u32, u32);
u8 t32_data_e2650[240] __attribute__((weak, aligned(4)));
u8 t32_data_e2760[8] __attribute__((weak));

static u32 rs = 0x7c3e9a1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct st {
	u8 sb[BLK], attr[64], saved[240], def[1808], flag[4];
};

static void gen(struct st *s)
{
	unsigned int i;

	for (i = 0; i < BLK; i++) s->sb[i] = (u8)rnd();
	for (i = 0; i < 64; i++) s->attr[i] = (u8)rnd();
	for (i = 0; i < 240; i++) s->saved[i] = (u8)rnd();
	for (i = 0; i < 1808; i++) s->def[i] = (u8)rnd();
	for (i = 0; i < 4; i++) s->flag[i] = (u8)(rnd() % 3 ? 0 : rnd());
	/* enable words: mostly 0 / 1 */
	t32_msa_w32(s->attr, rnd() % 3 ? 1 : rnd() % 3);
	t32_msa_w32(s->attr + 12, rnd() % 3 ? 1 : rnd() % 3);
	t32_msa_w32(s->attr + 32, rnd() % 3 ? 1 : rnd() % 3);
}

static u32 ref_run(u32 n, const struct st *in, struct st *r)
{
	u8 *sbp[4], *dsb[4], *bp, *chk;
	u32 i, ret, chan = n / 3;

	t32o_reset();
	for (i = 0; i < 4; i++) {
		sbp[i] = t32o_alloc(BLK);
		dsb[i] = t32o_alloc(BLK);
		memcpy(sbp[i], in->sb, BLK);
		memcpy(dsb[i], in->sb, BLK);
		t32_data_e2768[i] = T32O_PTR(i == chan ? sbp[i] : dsb[i]);
	}
	bp = t32o_alloc(64); memcpy(bp, in->attr, 64);
	memcpy(t32_data_e2650, in->saved, 240);
	memcpy(t32_data_dc284, in->def, 1808);
	memcpy(t32_data_e2760, in->flag, 4);
	chk = t32o_alloc(1808); memcpy(chk, t32_data_dc284, 1808);
	ret = (u32)t32o_call((t32o_fn)(void (*)(void))lib_tisp_msca_channel_attr_set, n, T32O_PTR(bp), 0, 0, 0);
	if (t32o_log.n || t32o_log.printfs) {
		printf("MISMATCH: unexpected vendor writes / messages\n");
		exit(1);
	}
	memcpy(r->sb, sbp[chan], BLK);
	memcpy(r->saved, t32_data_e2650, 240);
	for (i = 0; i < 4; i++)
		if (i != chan && memcmp(dsb[i], in->sb, BLK)) {
			printf("MISMATCH: vendor changed another channel\n");
			exit(1);
		}
	if (memcmp(chk, t32_data_dc284, 1808) || memcmp(bp, in->attr, 64) || memcmp(t32_data_e2760, in->flag, 4)) {
		printf("MISMATCH: vendor changed default / attribute / flag\n");
		exit(1);
	}
	return ret;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 20000;
	unsigned int c, compared = 0;

	for (c = 0; c < n; c++) {
		static struct st in, o, r;
		u32 idx = rnd() % 12, chan = idx / 3, ret, oret;

		gen(&in);
		if (chan >= 2)	/* dc284 holds two channels: the defaults are only read with the flag clear */
			in.flag[chan] = 1;
		ret = ref_run(idx, &in, &r);
		o = in;
		oret = t32_msca_channel_attr_set(o.sb, o.saved + chan * 60, o.def + chan * 904, o.flag[chan], idx % 3,
						 o.attr);
		compared++;
		if (memcmp(r.sb, o.sb, BLK) || memcmp(r.saved, o.saved, 240) || ret != oret) {
			printf("MISMATCH: block / saved / ret %u/%u (n %u)\n", ret, oret, idx);
			return 1;
		}
	}
	printf("t32_mscai_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
