// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA channel enable / stream control (tx_isp_t32_msca_chx.h):
 * chx_en, stream_on, stream_off, deinit.  Runs the unmodified vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our port from the same
 * state in both register modes (t32_data_d03a0 0 / 1) and compares every
 * register write (address / shadow word, value, order), every register read
 * (order), the whole parameter block of the channel (other channels' blocks are
 * decoys that must stay unchanged) and, for deinit, the irq / free calls and the
 * pointer arrays.  tisp_reg_write / _read are replaced as in t32_mscad_oracle.
 * The vendor's tail-call return values are not compared.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_chx.h"

extern u64 tisp_msca_chx_en(u32, u32, u32, u32);
extern u64 lib_tisp_msca_chx_stream_on(u32, u32, u32, u32);
extern u64 lib_tisp_msca_chx_stream_off(u32, u32, u32, u32);
extern u64 tisp_msca_deinit(u32, u32, u32, u32);
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);

/* deinit observers: the env.c stubs are weak */
static u32 kf[8], kn, irq[8][3], in_;
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (kn < 8)
		kf[kn] = a0;
	kn++;
	return 0;
}

u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	if (in_ < 8) {
		irq[in_][0] = a0; irq[in_][1] = a1; irq[in_][2] = a2;
	}
	in_++;
	return 0;
}

static void port_kfree(u32 p)
{
	if (kn < 8)
		kf[kn] = p;
	kn++;
}

static void port_irq(u32 ch, u32 i, u32 fn)
{
	if (in_ < 8) {
		irq[in_][0] = ch; irq[in_][1] = i; irq[in_][2] = fn;
	}
	in_++;
}

#define BLK	2636

static u32 rs = 0x2f1a9c3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 rlog[64], rn, salt;

static u32 rdv(u32 reg)
{
	u32 v = (reg ^ salt) * 2654435761u;

	if (rn < 64)
		rlog[rn] = reg;
	rn++;
	return v ^ (v >> 15);
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

u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	return rdv(t32_data_d03a0[0] != 1 ? a1 : T32_MS_SHADOW_IDX(a0, a1));
}


struct res {
	unsigned int n, rn;
	struct t32_front_write w[16];
	u32 rl[64];
	u8 sb[BLK];
	u32 kn, kf[8], in_, irq[8][3], sets[4], ctx[4];
};

static unsigned int compared;
static unsigned int seen[4][2];

static void run(int which, u32 mode, u32 a0, u32 a1, u32 a2, const u8 *sbin, u32 pat, int ref, struct res *r)
{
	unsigned int i;
	u32 ch = which == 0 ? a0 : a0 / 3;

	memset(r, 0, sizeof(*r));
	rn = 0; kn = 0; in_ = 0;
	if (ref) {
		u8 *sbp[4], *dsb[4], *cx[4];

		t32o_reset();
		t32_data_d03a0[0] = (u8)mode;
		t32o_rd_hook = rdv;
		for (i = 0; i < 4; i++) {
			sbp[i] = t32o_alloc(BLK); dsb[i] = t32o_alloc(BLK); cx[i] = t32o_alloc(16);
			memcpy(sbp[i], sbin, BLK); memcpy(dsb[i], sbin, BLK);
			t32_data_e2768[i] = (pat & (1u << i)) ? 0 : T32O_PTR(i == (ch & 3) ? sbp[i] : dsb[i]);
			t32_data_e28ac[i] = (pat & (16u << i)) ? 0 : T32O_PTR(cx[i]);
		}
		switch (which) {
		case 0: t32o_call((t32o_fn)(void (*)(void))tisp_msca_chx_en, a0, a1, a2, 0, 0); break;
		case 1: t32o_call((t32o_fn)(void (*)(void))lib_tisp_msca_chx_stream_on, a0, 0, 0, 0, 0); break;
		case 2: t32o_call((t32o_fn)(void (*)(void))lib_tisp_msca_chx_stream_off, a0, 0, 0, 0, 0); break;
		default: t32o_call((t32o_fn)(void (*)(void))tisp_msca_deinit, a0, 0, 0, 0, 0); break;
		}
		r->n = t32o_log.n;
		for (i = 0; i < r->n; i++) {
			r->w[i].reg = t32o_log.w[i].reg;
			r->w[i].val = t32o_log.w[i].val;
		}
		memcpy(r->sb, sbp[ch & 3], BLK);
		for (i = 0; i < 4; i++) {
			if (i != (ch & 3) && memcmp(dsb[i], sbin, BLK)) {
				printf("MISMATCH: vendor changed another channel's block\n");
				exit(1);
			}
			r->sets[i] = t32_data_e2768[i] ? 1 : 0;
			r->ctx[i] = t32_data_e28ac[i] ? 1 : 0;
		}
		/* free calls are checked by identity, not address */
		for (i = 0; i < kn && i < 8; i++)
			kf[i] = kf[i] == T32O_PTR(sbp[ch & 3]) ? 1 : kf[i] == T32O_PTR(cx[ch & 3]) ? 2 : 3;
	} else {
		static struct t32_front_write w[16];
		static const struct t32_msca_free_ops ops = { port_irq, port_kfree };
		struct t32_ms_io io;
		u32 sets[4], ctx[4];

		io.shadow = (u8)mode;
		io.w = w;
		io.n = 0;
		io.rd = rdv;
		memcpy(r->sb, sbin, BLK);
		for (i = 0; i < 4; i++) {
			sets[i] = (pat & (1u << i)) ? 0 : 0x1000 + i;
			ctx[i] = (pat & (16u << i)) ? 0 : 0x2000 + i;
		}
		switch (which) {
		case 0: t32_msca_chx_en(&io, r->sb, a0, a1, a2); break;
		case 1: t32_msca_chx_stream_on(&io, r->sb, a0); break;
		case 2: t32_msca_chx_stream_off(&io, r->sb, a0); break;
		default: t32_msca_deinit(&ops, sets, ctx, a0); break;
		}
		r->n = io.n;
		memcpy(r->w, w, io.n * sizeof(w[0]));
		for (i = 0; i < 4; i++) {
			r->sets[i] = sets[i] ? 1 : 0;
			r->ctx[i] = ctx[i] ? 1 : 0;
		}
		for (i = 0; i < kn && i < 8; i++)
			kf[i] = kf[i] == 0x1000 + (ch & 3) ? 1 : kf[i] == 0x2000 + (ch & 3) ? 2 : 3;
	}
	r->rn = rn;
	memcpy(r->rl, rlog, sizeof(rlog));
	r->kn = kn; r->in_ = in_;
	memcpy(r->kf, kf, sizeof(kf));
	memcpy(r->irq, irq, sizeof(irq));
}

int main(void)
{
	static const char *const names[4] = { "chx_en", "stream_on", "stream_off", "deinit" };
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int c, i, k;

	for (c = 0; c < n; c++)
		for (k = 0; k < 4; k++) {
			static u8 sb[BLK];
			struct res r, o;
			u32 mode = rnd() & 1, a0, a1 = rnd() % 4, a2 = rnd() % 3 ? rnd() % 2 : rnd();
			u32 pat = k == 3 ? rnd() & 0xff : 0;

			for (i = 0; i < BLK; i++)
				sb[i] = (u8)rnd();
			for (i = 14; i < 17; i++)
				sb[i] = (u8)(rnd() % 3 ? rnd() % 2 : rnd());
			a0 = k == 0 || k == 3 ? rnd() % 4 : rnd() % 12;
			if (k == 0) {	/* set 0..3 mostly, any byte sometimes; en 0 / 1 mostly, any sometimes */
				a1 = rnd() % 4 == 0 ? (rnd() & 0xff) : rnd() % 3;
				if (rnd() % 4 == 0)
					a1 |= 0x100 * (rnd() % 8);
				a2 = rnd() % 3 ? rnd() % 2 + (rnd() % 8 == 0 ? 0x100 : 0) : rnd();
			}
			salt = rnd();
			run((int)k, mode, a0, a1, a2, sb, pat, 1, &r);
			run((int)k, mode, a0, a1, a2, sb, pat, 0, &o);
			compared++;
			seen[k][mode]++;
			if (r.n != o.n || r.rn != o.rn || memcmp(r.sb, o.sb, BLK) ||
			    memcmp(r.rl, o.rl, sizeof(u32) * (r.rn < 64 ? r.rn : 64))) {
				printf("MISMATCH %s mode %u: writes %u/%u reads %u/%u or block\n", names[k], mode, r.n, o.n,
				       r.rn, o.rn);
				return 1;
			}
			for (i = 0; i < r.n; i++)
				if (r.w[i].reg != o.w[i].reg || r.w[i].val != o.w[i].val) {
					printf("MISMATCH %s mode %u: write #%u vendor %08x=%08x ours %08x=%08x\n", names[k],
					       mode, i, r.w[i].reg, r.w[i].val, o.w[i].reg, o.w[i].val);
					return 1;
				}
			if (r.kn != o.kn || r.in_ != o.in_ || memcmp(r.kf, o.kf, sizeof(r.kf)) ||
			    memcmp(r.irq, o.irq, sizeof(r.irq)) || memcmp(r.sets, o.sets, sizeof(r.sets)) ||
			    memcmp(r.ctx, o.ctx, sizeof(r.ctx))) {
				printf("MISMATCH %s mode %u: free / irq / arrays\n", names[k], mode);
				return 1;
			}
		}
	for (k = 0; k < 4; k++)
		if (!seen[k][0] || !seen[k][1]) {
			printf("t32_mscaj_oracle: %s never ran in both modes\n", names[k]);
			return 1;
		}
	printf("t32_mscaj_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
