// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA channel init (tx_isp_t32_msca_init.h).  Runs the
 * unmodified vendor tisp_msca_init (Matt Davis' recovered C, PR #15 @ 2e2e06bc)
 * and our port from the same random init block in both register modes
 * (t32_data_d03a0 0 / 1) and compares every register write (address / shadow
 * word, value, order), every register read, the parameter block and context
 * contents, the size / OSD globals, the pointer slots, the allocation sizes and
 * flags, the lock init, the irq hook and the isp_printf count; other channels
 * are decoys that must stay unchanged.  tisp_reg_write / _read are replaced as in
 * t32_mscad_oracle.  The allocation-failure paths differ on purpose (the vendor
 * crashes in memset(NULL)); they are checked on the port alone.
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_init.h"

extern u64 tisp_msca_init(u32, u32, u32, u32);
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_msca_irq_frame_done(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

#define IRQ_TOKEN	1

u16 t32_data_e28a4[4] __attribute__((weak));
u16 t32_data_e289c[4] __attribute__((weak));
u32 t32_data_e28bc[1] __attribute__((weak));
u32 t32_data_e2740[8] __attribute__((weak));

static u32 ksz[4], kfl[4], kn, locks, irqn, irqv[3][3];
static int kfail;	/* port only: fail the n-th allocation (1 based) */

u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (kn < 4) {
		ksz[kn] = a0; kfl[kn] = a1;
	}
	kn++;
	return (u32)(uintptr_t)t32o_alloc(a0);
}

u64 private_spin_lock_init(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	locks++;
	return 0;
}

u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	if (irqn < 3) {
		irqv[irqn][0] = a0; irqv[irqn][1] = a1;
		irqv[irqn][2] = a2 == T32O_PTR(tisp_msca_irq_frame_done) ? IRQ_TOKEN : a2;
	}
	irqn++;
	return 0;
}

static u8 *p_kmalloc(u32 size)
{
	if (kn < 4) {
		ksz[kn] = size; kfl[kn] = 0x24000c0;
	}
	kn++;
	if (kfail && (int)kn == kfail)
		return NULL;
	return t32o_alloc(size);
}

static void p_lock(void) { locks++; }
static void p_err(u32 line)
{
	t32o_log.printfs += line == 1285 ? 1 : 100;
}

static void p_irq(u32 ch, u32 irq)
{
	if (irqn < 3) {
		irqv[irqn][0] = ch; irqv[irqn][1] = irq; irqv[irqn][2] = IRQ_TOKEN;
	}
	irqn++;
}

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



#define BLK	2636
#define CTX	304
#define MAXW	96

struct res {
	unsigned int n, rn, printfs;
	struct t32_front_write w[MAXW];
	u32 rl[64];
	u8 sb[BLK], cx[CTX], dsb[3][BLK], dcx[3][CTX];
	u32 par, osd, ksz[4], kfl[4], kn, locks, irqn, irqv[3][3], ret;
	u16 h[4], wd[4];
	u32 sets[4], ctx[4];
};

static unsigned int compared;
static unsigned int seen[2];

static void run(u32 mode, u32 ch, const u8 *p, const u32 *pbase, const u8 *noise, int ref, struct res *r)
{
	unsigned int i, j;
	struct t32_msca_init_st st;
	static struct t32_front_write w[MAXW];
	struct t32_ms_io io;
	static const struct t32_msca_init_ops ops = { p_kmalloc, p_lock, p_err, p_irq };
	u8 *pp;

	memset(r, 0, sizeof(*r));
	rn = 0; kn = 0; locks = 0; irqn = 0;
	t32o_reset();
	t32_data_d03a0[0] = (u8)mode;
	t32o_rd_hook = rdv;
	pp = t32o_alloc(1024);
	memcpy(pp, p, 1024);
	for (i = 0; i < 4; i++)
		t32_data_dd0a4[i] = pbase[i];
	if (ref) {
		u8 *dsb[4], *dcx[4];

		for (i = 0; i < 4; i++) {
			dsb[i] = t32o_alloc(BLK); dcx[i] = t32o_alloc(CTX);
			memset(dsb[i], noise[i], BLK); memset(dcx[i], noise[i], CTX);
			t32_data_e2768[i] = T32O_PTR(dsb[i]);
			t32_data_e28ac[i] = T32O_PTR(dcx[i]);
			t32_data_e28a4[i] = 0x1111 * (i + 1); t32_data_e289c[i] = 0x2222 * (i + 1);
		}
		t32_data_e28bc[0] = 0x77;
		kn = 0;
		r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_init, ch, T32O_PTR(pp), 0, 0, 0);
		r->n = t32o_log.n;
		for (i = 0; i < r->n && i < MAXW; i++) {
			r->w[i].reg = t32o_log.w[i].reg;
			r->w[i].val = t32o_log.w[i].val;
		}
		r->printfs = t32o_log.printfs;
		memcpy(r->sb, (void *)(uintptr_t)t32_data_e2768[ch], BLK);
		memcpy(r->cx, (void *)(uintptr_t)t32_data_e28ac[ch], CTX);
		for (i = 0, j = 0; i < 4; i++) {
			if (i == ch) {
				r->h[i] = t32_data_e28a4[i]; r->wd[i] = t32_data_e289c[i];
				continue;
			}
			memcpy(r->dsb[j], (void *)(uintptr_t)t32_data_e2768[i], BLK);
			memcpy(r->dcx[j], (void *)(uintptr_t)t32_data_e28ac[i], CTX);
			r->h[i] = t32_data_e28a4[i]; r->wd[i] = t32_data_e289c[i];
			j++;
		}
		for (i = 0; i < 4; i++) {
			r->sets[i] = t32_data_e2768[i] == T32O_PTR(dsb[i]);
			r->ctx[i] = t32_data_e28ac[i] == T32O_PTR(dcx[i]);
		}
		memcpy(&r->par, (void *)(uintptr_t)t32_data_e28ac[ch], 4);
		r->osd = t32_data_e28bc[0];
	} else {
		static struct t32_msca_init_st s;
		u8 *dsb[4], *dcx[4];

		memset(&s, 0, sizeof(s));
		for (i = 0; i < 4; i++) {
			dsb[i] = t32o_alloc(BLK); dcx[i] = t32o_alloc(CTX);
			memset(dsb[i], noise[i], BLK); memset(dcx[i], noise[i], CTX);
			s.sets[i] = dsb[i]; s.ctx[i] = dcx[i];
			s.height[i] = 0x1111 * (i + 1); s.width[i] = 0x2222 * (i + 1);
			s.par_base[i] = pbase[i];
		}
		s.osd_info = 0x77;
		st = s;
		io.shadow = (u8)mode; io.w = w; io.n = 0; io.rd = rdv;
		kn = 0;
		r->ret = (u32)t32_msca_init(&ops, &io, &st, ch, pp);
		r->n = io.n;
		memcpy(r->w, w, (io.n < MAXW ? io.n : MAXW) * sizeof(w[0]));
		r->printfs = t32o_log.printfs;
		memcpy(r->sb, st.sets[ch], BLK);
		memcpy(r->cx, st.ctx[ch], CTX);
		for (i = 0, j = 0; i < 4; i++) {
			if (i == ch) {
				r->h[i] = st.height[i]; r->wd[i] = st.width[i];
				continue;
			}
			memcpy(r->dsb[j], st.sets[i], BLK);
			memcpy(r->dcx[j], st.ctx[i], CTX);
			r->h[i] = st.height[i]; r->wd[i] = st.width[i];
			j++;
		}
		for (i = 0; i < 4; i++) {
			r->sets[i] = st.sets[i] == dsb[i];
			r->ctx[i] = st.ctx[i] == dcx[i];
		}
		memcpy(&r->par, st.ctx[ch], 4);
		r->osd = st.osd_info;
	}
	r->rn = rn;
	memcpy(r->rl, rlog, sizeof(rlog));
	r->kn = kn; r->locks = locks; r->irqn = irqn;
	memcpy(r->ksz, ksz, sizeof(ksz)); memcpy(r->kfl, kfl, sizeof(kfl));
	memcpy(r->irqv, irqv, sizeof(irqv));
}

/* allocation failure: port alone */
static int fail_checks(void)
{
	static const struct t32_msca_init_ops ops = { p_kmalloc, p_lock, p_err, p_irq };
	struct t32_msca_init_st st;
	struct t32_ms_io io;
	static struct t32_front_write w[MAXW];
	u8 p[1024] = { 0 };
	int f;

	for (f = 1; f <= 2; f++) {
		memset(&st, 0, sizeof(st));
		t32o_reset();
		kn = 0; kfail = f; irqn = 0; locks = 0;
		io.shadow = 0; io.w = w; io.n = 0; io.rd = rdv;
		if (t32_msca_init(&ops, &io, &st, 1, p) != -1 || io.n || irqn || locks != 1 || kn != (unsigned int)f ||
		    t32o_log.printfs != (f == 1 ? 1u : 0u) || (f == 1 ? st.sets[1] != NULL : st.ctx[1] != NULL)) {
			printf("MISMATCH: allocation failure %d\n", f);
			return 1;
		}
	}
	kfail = 0;
	return 0;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 4000;
	unsigned int c, i;

	if (fail_checks())
		return 1;
	for (c = 0; c < n; c++) {
		static u8 p[1024];
		static struct res r, o;
		u32 mode = rnd() & 1, ch = rnd() % 4, pbase[4];
		u8 noise[4];

		for (i = 0; i < 1024; i++)
			p[i] = (u8)rnd();
		t32_msa_w32(p + 900, rnd() % 3 ? 1 : rnd() % 4);
		for (i = 0; i < 4; i++) {
			pbase[i] = rnd();
			noise[i] = (u8)rnd();
		}
		salt = rnd();
		run(mode, ch, p, pbase, noise, 1, &r);
		run(mode, ch, p, pbase, noise, 0, &o);
		compared++;
		seen[mode]++;
		if (r.n != o.n || r.rn != o.rn || r.printfs != o.printfs || r.ret != o.ret ||
		    memcmp(r.rl, o.rl, sizeof(u32) * (r.rn < 64 ? r.rn : 64))) {
			printf("MISMATCH mode %u: writes %u/%u reads %u/%u printfs %u/%u\n", mode, r.n, o.n, r.rn, o.rn,
			       r.printfs, o.printfs);
			return 1;
		}
		for (i = 0; i < r.n; i++)
			if (r.w[i].reg != o.w[i].reg || r.w[i].val != o.w[i].val) {
				printf("MISMATCH mode %u: write #%u vendor %08x=%08x ours %08x=%08x\n", mode, i,
				       r.w[i].reg, r.w[i].val, o.w[i].reg, o.w[i].val);
				return 1;
			}
		if (memcmp(r.sb, o.sb, BLK) || memcmp(r.cx, o.cx, CTX) || memcmp(r.dsb, o.dsb, sizeof(r.dsb)) ||
		    memcmp(r.dcx, o.dcx, sizeof(r.dcx)) || r.par != o.par || r.osd != o.osd ||
		    memcmp(r.h, o.h, sizeof(r.h)) || memcmp(r.wd, o.wd, sizeof(r.wd)) ||
		    memcmp(r.sets, o.sets, sizeof(r.sets)) || memcmp(r.ctx, o.ctx, sizeof(r.ctx))) {
			printf("MISMATCH mode %u: memory / globals\n", mode);
			return 1;
		}
		if (r.kn != 2 || r.ksz[0] != 2636 || r.ksz[1] != 304 || r.kfl[0] != 0x24000c0 || r.kfl[1] != 0x24000c0 ||
		    memcmp(&r.kn, &o.kn, 4) || memcmp(r.ksz, o.ksz, 8) || memcmp(r.kfl, o.kfl, 8) || r.locks != 1 ||
		    o.locks != 1 || r.irqn != 1 || o.irqn != 1 || memcmp(r.irqv, o.irqv, sizeof(r.irqv)) ||
		    r.irqv[0][1] != 33 || r.irqv[0][0] != ch || r.irqv[0][2] != IRQ_TOKEN) {
			printf("MISMATCH mode %u: allocations / lock / irq\n", mode);
			return 1;
		}
	}
	if (!seen[0] || !seen[1]) {
		printf("t32_mscak_oracle: a register mode never ran\n");
		return 1;
	}
	printf("t32_mscak_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
