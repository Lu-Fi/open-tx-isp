// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA parameter array get/set and OSD attribute setter
 * (tx_isp_t32_msca_arr.h).  Runs the unmodified vendor functions (Matt
 * Davis' recovered C, PR #15 @ 2e2e06bc) and our port from the same random
 * state; compares the output buffer (whole 512 bytes), the parameter block,
 * the set/channel block, the coefficients, the register writes of
 * params_update (address, value, order), the pending flag, the length the
 * get stores, the return value and the isp_printf count.  Other channels'
 * blocks are decoys that must stay unchanged.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_arr.h"

#define PB	512
#define SB	(4 * 872 + 64)
#define OB	512

extern u64 tisp_msca_param_array_get(u32, u32, u32, u32);
extern u64 tisp_msca_param_array_set(u32, u32, u32, u32);
extern u64 tisp_msca_api_set_osd_out_attribute_cfg(u32, u32, u32, u32);
u32 t32_data_e2740[8] __attribute__((weak));
u32 t32_data_e2758[2] __attribute__((weak));
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x5d2c1a7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a3;
	return system_reg_write(a1, a2, 0, 0);
}

struct st {
	u8 par[PB], sets[SB], buf[OB], dst[OB], sb[2636];
	u16 coef[T32_MSCA_COEFS];
	u8 pend;
};

struct res {
	struct st s;
	struct t32_front_write w[64];
	unsigned int n;
	u32 len, ret, printfs;
};

static void gen(struct st *s)
{
	unsigned int i;

	for (i = 0; i < PB; i++) s->par[i] = (u8)rnd();
	for (i = 0; i < SB; i++) s->sets[i] = (u8)rnd();
	for (i = 0; i < OB; i++) s->buf[i] = s->dst[i] = (u8)rnd();
	for (i = 0; i < 2636; i++) s->sb[i] = (u8)rnd();
	for (i = 0; i < T32_MSCA_COEFS; i++) s->coef[i] = (u16)rnd();
	for (i = 172; i <= 174; i++) s->par[i] = (u8)(rnd() & 1 ? 1 : rnd() % 3);
	for (i = 181; i <= 186; i++) s->par[i] = (u8)(rnd() & 1 ? 1 : rnd() % 3);
	for (i = 172; i <= 174; i++) s->buf[i] = (u8)(rnd() & 1 ? 1 : rnd() % 3);
	for (i = 181; i <= 186; i++) s->buf[i] = (u8)(rnd() & 1 ? 1 : rnd() % 3);
	s->buf[29] = (u8)(rnd() % 4);
	s->pend = (u8)(rnd() % 3);
}

static void ref_run(int k, u32 chn, const struct st *in, struct res *r)
{
	u8 *pb, *sbp, *cx, *dp, *lp, *dpb, *dcx, *dsb, *sbb, *dsbb, *bp;
	u32 pv, len = 0xdeadbeef, i;

	t32o_reset();
	pb = t32o_alloc(PB); sbp = t32o_alloc(SB); cx = t32o_alloc(320);
	dp = t32o_alloc(OB); lp = t32o_alloc(4);
	dpb = t32o_alloc(PB); dsb = t32o_alloc(SB); dcx = t32o_alloc(320);
	sbb = t32o_alloc(2636); dsbb = t32o_alloc(2636); bp = t32o_alloc(OB);
	memcpy(pb, in->par, PB); memcpy(sbp, in->sets, SB);
	pv = T32O_PTR(pb);
	memcpy(cx, &pv, 4); memcpy(cx + 4, in->coef, 300);
	memcpy(dpb, in->par, PB); memcpy(dsb, in->sets, SB); memcpy(dcx, cx, 320);
	pv = T32O_PTR(dpb);
	memcpy(dcx, &pv, 4);
	memcpy(lp, &len, 4);
	memcpy(dp, in->dst, OB);
	memcpy(sbb, in->sb, 2636); memcpy(dsbb, in->sb, 2636); memcpy(bp, in->buf, OB);
	for (i = 0; i < 4; i++) {
		t32_data_e28ac[i] = T32O_PTR(i == chn ? cx : dcx);
		t32_data_e2768[i] = T32O_PTR(k == 2 ? (i == chn ? sbb : dsbb) : (i == chn ? sbp : dsb));
		t32_data_e2898[i] = 0x5a;
	}
	t32_data_e2898[chn] = in->pend;
	t32_data_d03a0[0] = 0;
	if (k == 0)
		r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_param_array_get, chn, T32O_PTR(dp), T32O_PTR(lp), 0, 0);
	else if (k == 1)
		r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_param_array_set, chn, T32O_PTR(bp), 0, 0, 0);
	else
		r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_api_set_osd_out_attribute_cfg, chn, T32O_PTR(bp), 0, 0, 0);
	r->n = t32o_log.n;
	if (r->n > 64) {
		fprintf(stderr, "too many writes\n");
		exit(2);
	}
	for (i = 0; i < r->n; i++) {
		r->w[i].reg = t32o_log.w[i].reg;
		r->w[i].val = t32o_log.w[i].val;
	}
	r->printfs = t32o_log.printfs;
	memcpy(&r->len, lp, 4);
	memcpy(r->s.par, pb, PB); memcpy(r->s.sets, sbp, SB); memcpy(r->s.sb, sbb, 2636);
	memcpy(r->s.dst, dp, OB); memcpy(r->s.buf, bp, OB);
	memcpy(r->s.coef, cx + 4, 300);
	r->s.pend = t32_data_e2898[chn];
	for (i = 0; i < 4; i++) {
		if (i != chn && t32_data_e2898[i] != 0x5a) {
			printf("MISMATCH: vendor changed the pending flag of channel %u\n", i);
			exit(1);
		}
	}
	if (memcmp(dpb, in->par, PB) || memcmp(dsb, in->sets, SB) || memcmp(dcx + 4, in->coef, 300) ||
	    memcmp(dsbb, in->sb, 2636)) {
		printf("MISMATCH: vendor changed another channel's block\n");
		exit(1);
	}
}

int main(void)
{
	static const char *const names[3] = { "param_array_get", "param_array_set", "set_osd_attr" };
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int c, k, i, compared = 0;

	for (c = 0; c < n; c++)
		for (k = 0; k < 3; k++) {
			static struct st in;
			static struct res r;
			static struct st o;
			struct t32_front_write ow[64];
			struct t32_msca_ch oc;
			u32 chn = rnd() & 3, olen = 0xdeadbeef;
			int on = 0, oret = 0;

			gen(&in);
			ref_run((int)k, chn, &in, &r);
			o = in;
			memset(&oc, 0, sizeof(oc));
			oc.par = o.par;
			memcpy(oc.coef, o.coef, 300);
			oc.pending = o.pend;
			if (k == 0)
				olen = t32_msca_param_array_get(&oc, o.dst);
			else if (k == 1)
				on = t32_msca_param_array_set(&oc, o.sets, o.buf, ow);
			else
				oret = t32_msca_api_set_osd_out_attr_cfg(o.sb, o.buf);
			compared++;
			if (k == 1 && ((unsigned int)on != r.n || memcmp(ow, r.w, sizeof(ow[0]) * r.n))) {
				printf("MISMATCH %s: register writes ref %u ours %d\n", names[k], r.n, on);
				return 1;
			}
			if (k != 1 && r.n) {
				printf("MISMATCH %s: unexpected vendor writes\n", names[k]);
				return 1;
			}
			if (memcmp(r.s.par, o.par, PB) || memcmp(r.s.sets, o.sets, SB) || memcmp(r.s.sb, o.sb, 2636) ||
			    memcmp(r.s.dst, o.dst, OB) || memcmp(r.s.buf, o.buf, OB) || memcmp(r.s.coef, oc.coef, 300)) {
				printf("MISMATCH %s: memory\n", names[k]);
				return 1;
			}
			if (r.s.pend != oc.pending || (k == 0 && (r.len != olen || r.len != 488 || r.ret != 0))) {
				printf("MISMATCH %s: pending %u/%u len %u/%u ret %u\n", names[k], r.s.pend, oc.pending, r.len, olen, r.ret);
				return 1;
			}
			if (k == 2 && (r.ret != (u32)oret || r.printfs != (oret ? 2u : 0u))) {
				printf("MISMATCH %s: ret %d/%d printfs %u\n", names[k], (int)r.ret, oret, r.printfs);
				return 1;
			}
			(void)i;
		}
	printf("t32_mscaf_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
