// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA OSD output tisp_api_msca_set_osd (tx_isp_t32_msca_osd.h).  Runs the
 * unmodified vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our port from the
 * same state in both register modes (t32_data_d03a0 0 / 1) and compares every register write
 * (address / shadow word, value, order), the whole parameter block of the channel (the other
 * channels' blocks are decoys that must stay unchanged) and, in direct mode, the u16 counter and t32_data_e28bc.
 * tisp_reg_write is replaced as in t32_mscad_oracle.  The vendor's return value is a leftover
 * register and not compared.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_osd.h"

extern u64 tisp_api_msca_set_osd(u32, u32, u32, u32);
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);

#define BLK	2636

u32 t32_data_e28bc[1];

static u32 rs = 0x7c2e91bu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	if (t32_data_d03a0[0] != 1)
		return system_reg_write(a1, a2, 0, 0);
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = T32_MS_SHADOW_IDX(a0, a1);
		t32o_log.w[t32o_log.n].val = a2;
		t32o_log.n++;
	}
	return 0;
}

struct res {
	unsigned int n;
	struct t32_front_write w[160];
	u8 sb[BLK];
	u16 cnt;
	u32 e28bc;
};

static unsigned int compared, seen[2], blocks, fixes;

static void run(u32 mode, u32 ch, const u8 *sbin, u16 cin, u32 ein, int ref, struct res *r)
{
	unsigned int i;

	memset(r, 0, sizeof(*r));
	if (ref) {
		u8 *sbp[4], *dsb[4];
		u16 *cp;

		t32o_reset();
		cp = t32o_alloc(4);
		t32_data_d03a0[0] = (u8)mode;
		*cp = cin;
		t32_data_e28bc[0] = ein;
		for (i = 0; i < 4; i++) {
			sbp[i] = t32o_alloc(BLK); dsb[i] = t32o_alloc(BLK);
			memcpy(sbp[i], sbin, BLK); memcpy(dsb[i], sbin, BLK);
			t32_data_e2768[i] = T32O_PTR(i == ch ? sbp[i] : dsb[i]);
		}
		t32o_call((t32o_fn)(void (*)(void))tisp_api_msca_set_osd, ch, T32O_PTR(cp), 0, 0, 0);
		r->n = t32o_log.n;
		for (i = 0; i < r->n; i++) {
			r->w[i].reg = t32o_log.w[i].reg;
			r->w[i].val = t32o_log.w[i].val;
		}
		memcpy(r->sb, sbp[ch], BLK);
		for (i = 0; i < 4; i++)
			if (i != ch && memcmp(dsb[i], sbin, BLK)) {
				printf("MISMATCH: vendor changed another channel's block\n");
				exit(1);
			}
		r->cnt = *cp;
		r->e28bc = t32_data_e28bc[0];
	} else {
		static struct t32_front_write w[160];
		struct t32_ms_io io;
		u16 cnt = cin;
		u32 e = ein;

		io.shadow = (u8)mode;
		io.w = w;
		io.n = 0;
		io.rd = NULL;
		memcpy(r->sb, sbin, BLK);
		t32_msca_api_set_osd(&io, r->sb, ch, &cnt, &e);
		r->n = io.n;
		memcpy(r->w, w, io.n * sizeof(w[0]));
		r->cnt = cnt;
		r->e28bc = e;
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int c, i, s;

	for (c = 0; c < n; c++) {
		static u8 sb[BLK];
		struct res r, o;
		u32 mode = rnd() & 1, ch = rnd() % 4;
		u16 cin = (u16)(rnd() % 4 ? rnd() % 100 : rnd());
		u32 ein = rnd() % 4 ? rnd() % 100 : rnd();

		for (i = 0; i < BLK; i++)
			sb[i] = (u8)rnd();
		{
			/* windows: x y w h mostly small, odd and even; enable 0 / 1; changed bits sparse / dense / none / all */
			u32 f = rnd() % 5 == 0 ? 0 : rnd() % 5 == 0 ? 0xff : rnd() & rnd();

			memcpy(sb + 880, &f, 4);
			for (s = 0; s < 8; s++) {
				for (i = 682; i <= 688; i += 2) {
					u32 v = rnd() % 3 ? rnd() % 64 : rnd();

					sb[20 * s + i] = (u8)v; sb[20 * s + i + 1] = (u8)(v >> 8);
				}
				sb[20 * s + 680] = (u8)(rnd() % 3 ? rnd() % 2 : rnd());
			}
			sb[844] = (u8)(rnd() % 2 ? 2 : rnd() % 4);
			sb[845] = (u8)(rnd() % 2 ? 2 : rnd() % 4);
		}
		run(mode, ch, sb, cin, ein, 1, &r);
		run(mode, ch, sb, cin, ein, 0, &o);
		compared++;
		seen[mode]++;
		if (r.n != o.n || memcmp(r.sb, o.sb, BLK) || r.cnt != o.cnt || r.e28bc != o.e28bc) {
			printf("MISMATCH mode %u ch %u: writes %u/%u cnt %04x/%04x or block\n", mode, ch, r.n, o.n,
			       r.cnt, o.cnt);
			return 1;
		}
		for (i = 0; i < r.n; i++)
			if (r.w[i].reg != o.w[i].reg || r.w[i].val != o.w[i].val) {
				printf("MISMATCH mode %u: write #%u vendor %08x=%08x ours %08x=%08x\n", mode, i,
				       r.w[i].reg, r.w[i].val, o.w[i].reg, o.w[i].val);
				return 1;
			}
		blocks += r.n;
		fixes += r.e28bc != ein;
	}
	if (!seen[0] || !seen[1] || blocks < 1000 || !fixes) {
		printf("t32_mscan_oracle: coverage too low\n");
		return 1;
	}
	printf("t32_mscan_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
