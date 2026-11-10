// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA line / osd / autozoom setters (tx_isp_t32_msca_set.h).
 * Runs the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc) and our port from the same random state; compares the whole
 * channel block, the autozoom flag, the isp_printf count, the osd return
 * value and that the saved/default autozoom areas and all other channels'
 * blocks stay unchanged.  (The line setter's return value is the block
 * pointer left in v0, not a status: not compared.)  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_set.h"

#define BLK	2636

extern u64 tisp_msca_api_set_line_out_cfg(u32, u32, u32, u32);
extern u64 tisp_msca_api_set_osd_out_cfg(u32, u32, u32, u32);
extern u64 tisp_msca_api_set_autozoom(u32, u32, u32, u32);
u8 t32_data_e2650[240] __attribute__((weak, aligned(4)));
u8 t32_data_e2760[8] __attribute__((weak));

static u32 rs = 0x7c3e9a1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct st {
	u8 sb[BLK], buf[64], saved[60], def[8], flag;
};

struct res {
	struct st s;
	u32 ret, printfs;
};

static void gen(struct st *s, int k)
{
	unsigned int i;
	u32 lim = rnd() & 1 ? 1200 : 65536;

	for (i = 0; i < BLK; i++) s->sb[i] = (u8)rnd();
	for (i = 0; i < 64; i++) s->buf[i] = (u8)rnd();
	for (i = 0; i < 60; i++) s->saved[i] = (u8)rnd();
	for (i = 0; i < 8; i++) s->def[i] = (u8)rnd();
	s->flag = (u8)rnd();
	s->buf[0] = (u8)(k == 0 ? rnd() % 6 : k == 1 ? rnd() % 4 : rnd() % 3);
	if (k == 0) {
		memset(s->buf + 4, 0, 4);
		s->buf[4] = (u8)(rnd() % 5);
		if (!(rnd() % 8)) s->buf[5] = (u8)rnd();
		return;
	}
	if (k == 1) {
		s->buf[1] = (u8)(rnd() % 3 ? 1 : rnd() % 3);
		for (i = 2; i < 10; i += 2) t32_msa_w16(s->buf + i, rnd() % lim);
		t32_msa_w16(s->sb + 44, rnd() % lim);
		t32_msa_w16(s->sb + 46, rnd() % lim);
		for (i = 0; i < 4; i++) {
			t32_msa_w16(s->sb + s->buf[0] * 20 + 686 + 0, rnd() % 1200);
			t32_msa_w16(s->sb + s->buf[0] * 20 + 688, rnd() % 1200);
		}
		return;
	}
	for (i = 0; i < 15; i++)
		t32_msa_w32(s->buf + i * 4, i < 3 ? rnd() % 4 : rnd());
	for (i = 0; i < 3; i++)
		t32_msa_w32(s->saved + i * 4, rnd() % 3 ? rnd() : 0);
}

static void ref_run(int k, u32 chn, const struct st *in, struct res *r)
{
	u8 *sbp[4], *dsb[4], *bp, *sv, *df, *chk, *chk2;
	u32 i;

	t32o_reset();
	for (i = 0; i < 4; i++) {
		sbp[i] = t32o_alloc(BLK);
		dsb[i] = t32o_alloc(BLK);
		memcpy(sbp[i], in->sb, BLK);
		memcpy(dsb[i], in->sb, BLK);
		t32_data_e2768[i] = T32O_PTR(i == chn ? sbp[i] : dsb[i]);
	}
	bp = t32o_alloc(64); memcpy(bp, in->buf, 64);
	sv = t32_data_e2650; df = t32_data_dc284;
	memset(t32_data_e2650, 0xa5, sizeof(t32_data_e2650));
	memset(t32_data_e2760, 0x5a, sizeof(t32_data_e2760));
	memset(t32_data_dc284, 0x3c, sizeof(t32_data_dc284));
	memcpy(sv + chn * 60, in->saved, 60);
	memcpy(df + chn * 904, in->def, 8);
	t32_data_e2760[chn] = in->flag;
	chk = t32o_alloc(240); memcpy(chk, sv, 240);
	chk2 = t32o_alloc(1808); memcpy(chk2, df, 1808);
	if (k == 0)
		t32o_call((t32o_fn)(void (*)(void))tisp_msca_api_set_line_out_cfg, chn, T32O_PTR(bp), 0, 0, 0);
	else if (k == 1)
		r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_api_set_osd_out_cfg, chn, T32O_PTR(bp), 0, 0, 0);
	else
		r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_api_set_autozoom, chn, T32O_PTR(bp), 0, 0, 0);
	if (t32o_log.n) {
		printf("MISMATCH: unexpected vendor register writes\n");
		exit(1);
	}
	r->printfs = t32o_log.printfs;
	memcpy(r->s.sb, sbp[chn], BLK);
	r->s.flag = t32_data_e2760[chn];
	for (i = 0; i < 4; i++) {
		if (i != chn && (memcmp(dsb[i], in->sb, BLK) || t32_data_e2760[i] != (i == chn ? in->flag : 0x5a))) {
			printf("MISMATCH: vendor changed another channel\n");
			exit(1);
		}
	}
	if (memcmp(chk, sv, 240) || memcmp(chk2, df, 1808) || memcmp(bp, in->buf, 64)) {
		printf("MISMATCH: vendor changed saved/default/user data\n");
		exit(1);
	}
}

int main(void)
{
	static const char *const names[3] = { "set_line", "set_osd", "set_autozoom" };
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int c, k, compared = 0;

	for (c = 0; c < n; c++)
		for (k = 0; k < 3; k++) {
			static struct st in, o;
			static struct res r;
			u32 chn = rnd() & (k == 2 ? 1 : 3), oret = 0;
			unsigned int msgs = 0;

			gen(&in, (int)k);
			ref_run((int)k, chn, &in, &r);
			o = in;
			if (k == 0)
				t32_msca_api_set_line_out_cfg(o.sb, o.buf);
			else if (k == 1)
				oret = (u32)t32_msca_api_set_osd_out_cfg(o.sb, o.buf, &msgs);
			else
				oret = t32_msca_api_set_autozoom(o.sb, o.buf, in.saved, in.def, &o.flag);
			compared++;
			if (memcmp(r.s.sb, o.sb, BLK) || r.s.flag != o.flag) {
				printf("MISMATCH %s: block / flag %u/%u\n", names[k], r.s.flag, o.flag);
				return 1;
			}
			if (k >= 1 && r.ret != oret) {
				printf("MISMATCH %s: ret %d/%d\n", names[k], (int)r.ret, (int)oret);
				return 1;
			}
			if (r.printfs != msgs) {
				printf("MISMATCH %s: printfs %u/%u\n", names[k], r.printfs, msgs);
				return 1;
			}
		}
	printf("t32_mscag_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
