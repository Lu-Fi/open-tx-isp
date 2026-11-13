// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA line output tisp_api_msca_set_line (tx_isp_t32_msca_line.h).  Runs the
 * unmodified vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our port from the
 * same state in both register modes (t32_data_d03a0 0 / 1) and compares every register write
 * (address / shadow word, value, order), the whole parameter block of the channel (the other
 * channels' blocks are decoys that must stay unchanged) and, in direct mode, the u16 counter and the isp_printf calls.
 * tisp_reg_write is replaced as in t32_mscad_oracle.  The vendor's return value is a leftover
 * register and not compared.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_line.h"

extern u64 tisp_api_msca_set_line(u32, u32, u32, u32);
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);

#define BLK	2636

static u32 pn, pi[32];

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a3;
	if (pn < 32)
		pi[pn] = a2;
	pn++;
	return 0;
}

static u32 rs = 0x3b91c5du;
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
	struct t32_front_write w[512];
	u8 sb[BLK];
	u16 cnt;
	u32 pn, pi[32];
};

static unsigned int compared, seen[2], blocks, fixes;

static void run(u32 mode, u32 ch, const u8 *sbin, u16 cin, int ref, struct res *r)
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
		pn = 0;
		memset(pi, 0, sizeof(pi));
		for (i = 0; i < 4; i++) {
			sbp[i] = t32o_alloc(BLK); dsb[i] = t32o_alloc(BLK);
			memcpy(sbp[i], sbin, BLK); memcpy(dsb[i], sbin, BLK);
			t32_data_e2768[i] = T32O_PTR(i == ch ? sbp[i] : dsb[i]);
		}
		t32o_call((t32o_fn)(void (*)(void))tisp_api_msca_set_line, ch, T32O_PTR(cp), 0, 0, 0);
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
		r->pn = pn;
		memcpy(r->pi, pi, sizeof(pi));
	} else {
		static struct t32_front_write w[512];
		struct t32_ms_io io;
		u16 cnt = cin;
		u32 sk[32] = { 0 };
		unsigned int ns;

		io.shadow = (u8)mode;
		io.w = w;
		io.n = 0;
		io.rd = NULL;
		memcpy(r->sb, sbin, BLK);
		t32_msca_api_set_line(&io, r->sb, ch, &cnt, sk, &ns);
		r->n = io.n;
		memcpy(r->w, w, io.n * sizeof(w[0]));
		r->cnt = cnt;
		r->pn = ns;
		memcpy(r->pi, sk, sizeof(sk));
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

		for (i = 0; i < BLK; i++)
			sb[i] = (u8)rnd();
		{
			/* changed bits sparse / dense / none / all; modes 0..3; coordinates small (also equal, reversed) or any */
			u32 f = rnd() % 5 == 0 ? 0 : rnd() % 5 == 0 ? 0xfffff : rnd() & rnd();

			memcpy(sb + 888, &f, 4);
			for (s = 0; s < 20; s++) {
				u8 *l = sb + 28 * s;

				l[116] = (u8)(rnd() % 4);
				l[120] = (u8)(rnd() % 3 ? rnd() % 2 : rnd());
				for (i = 122; i <= 128; i += 2) {
					u32 v = rnd() % 4 == 0 ? rnd() : rnd() % 3 == 0 ? 100 : rnd() % 300;

					l[i] = (u8)v; l[i + 1] = (u8)(v >> 8);
				}
				if (rnd() % 5 == 0) {	/* horizontal / vertical / point */
					memcpy(l + 126, l + 122, rnd() % 2 ? 2 : 4);
					if (rnd() % 2)
						memcpy(l + 128, l + 124, 2);
				}
			}
		}
		run(mode, ch, sb, cin, 1, &r);
		run(mode, ch, sb, cin, 0, &o);
		compared++;
		seen[mode]++;
		if (r.n != o.n || memcmp(r.sb, o.sb, BLK) || r.cnt != o.cnt || r.pn != o.pn || memcmp(r.pi, o.pi, sizeof(r.pi))) {
			printf("MISMATCH mode %u ch %u: writes %u/%u cnt %04x/%04x, printf %u/%u or block\n", mode, ch, r.n, o.n,
			       r.cnt, o.cnt, r.pn, o.pn);
			return 1;
		}
		for (i = 0; i < r.n; i++)
			if (r.w[i].reg != o.w[i].reg || r.w[i].val != o.w[i].val) {
				printf("MISMATCH mode %u: write #%u vendor %08x=%08x ours %08x=%08x\n", mode, i,
				       r.w[i].reg, r.w[i].val, o.w[i].reg, o.w[i].val);
				return 1;
			}
		blocks += r.n;
		fixes += r.pn;
	}
	if (!seen[0] || !seen[1] || blocks < 1000 || !fixes) {
		printf("t32_mscao_oracle: coverage too low\n");
		return 1;
	}
	printf("t32_mscao_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
