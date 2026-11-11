// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA frame-done interrupt (tx_isp_t32_msca_irq.h): tisp_msca_irq_frame_done,
 * the commit the ISP OSD path (#336) runs at every MSCA frame done; built from t32_mscap_oracle.
 * Runs the unmodified vendor function AND its vendor callees (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc) and our port (which calls our ports of the callees: dn_refresh, para_calc, curve_calc,
 * init_chx_cfg, scaling_algorithm, ch_curve_write(_multi), set_osd / mask / line, shdram flag) from
 * the same random state.  Compares in direct mode (t32_data_d03a0 = 0) every register write (address,
 * value, order), in shadow mode (1) every write value and, where our port models a shadow word, its
 * address (callees modelled in direct mode only may be shadow words in the vendor), every register read
 * (order), the lock / unlock / isp_printf events, the parameter block, the sets, the context coef
 * array, the flags (dn / shadow-scale / autozoom / pending), t32_data_e28bc and the shadow / scale
 * words, and the return value (1).  Other channels' blocks and flags are decoys that must stay
 * unchanged.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_irq.h"

#define MAXW	1536
#define PB	512
#define SB	(4 * 872 + 64)
#define CXS	(4 + 300 + 16)

extern u64 tisp_msca_irq_frame_done(u32, u32, u32, u32);
u32 t32_data_e2740[8];
u32 t32_data_e2758[2];
u8 t32_data_e2760[8], t32_data_e2764[8];
extern u8 t32_data_e275c[8];
u32 t32_data_e28bc[1];
char t32_data_ca10c[4], t32_data_ca154[4], t32_data_c9ed0[4];
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x6d1f03bu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* events: 1 lock, 2 unlock, 3 busy printf, 4 | idx << 8 line printf, 99 other */
static u32 ev[64], evn;
static void evadd(u32 e)
{
	if (evn < 64)
		ev[evn] = e;
	evn++;
}

u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	evadd(1);
	return 0;
}

u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	evadd(2);
	return 0;
}

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a3;
	evadd(a1 == T32O_PTR(t32_data_ca10c) ? 3 : a1 == T32O_PTR(t32_data_c9ed0) ? 4 | a2 << 8 : 99);
	return 0;
}

static u32 g_flag, g_busy, g_five, g_ch, salt;
static u32 rlog[64], rn;

static u32 rdv(u32 reg)
{
	u32 v;

	if (rn < 64)
		rlog[rn] = reg;
	rn++;
	if (reg == 0xf09e0)
		return g_flag;
	if (reg == 0xf8100)
		return g_busy;
	if (reg == 256 || reg == T32_MS_SHADOW_IDX(g_ch, 11))
		return g_five;
	v = (reg ^ salt) * 2654435761u;
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

struct cs {
	u8 p[PB];
	u16 coef[T32_MSCA_COEFS];
	u8 sets[SB];
	u32 shadow[6], scale;
	u8 pend, dn, shd, az;
	u32 osd;
};

struct cap {
	struct t32_front_write w[MAXW];
	unsigned int n, rn, evn;
	u32 rl[64], ev[64], ret;
	u8 p[PB], xp[PB], sets[SB];
	u16 coef[T32_MSCA_COEFS];
	u8 pend, dn, shd, az;
	u32 osd, scale, shadow[6];
};

static unsigned int compared, stat[2][2], busy_seen, notready_seen, linemsg;

static void put32(u8 *p, unsigned int o, u32 v)
{
	memcpy(p + o, &v, 4);
}

static void gen(struct cs *s)
{
	static const u16 mk[3] = { 0xffff, 0x3ff, 0xf };
	unsigned int i, k;
	u16 m = (rnd() & 1) ? mk[rnd() % 3] : 0xffff;

	for (i = 0; i < PB; i++)
		s->p[i] = (u8)rnd();
	for (i = 0; i < T32_MSCA_COEFS; i++)
		s->coef[i] = (u16)rnd();
	for (i = 0; i < SB; i++)
		s->sets[i] = (u8)rnd();
	for (i = 0; i + 1 < SB; i += 2) {
		u16 v;

		memcpy(&v, s->sets + i, 2);
		v &= m;
		memcpy(s->sets + i, &v, 2);
	}
	for (i = 172; i <= 174; i++)
		s->p[i] = (u8)(rnd() % 3 ? 1 : rnd() % 3);
	for (i = 181; i <= 186; i++)
		s->p[i] = (u8)(rnd() % 3 ? 1 : rnd() % 3);
	for (k = 0; k < 3; k++) {
		unsigned int b = 4 + 32 * k;
		u32 a = rnd(), e = a + 1 + rnd() % 1000, c = rnd() % 100000, d = c + rnd() % 1000;

		put32(s->p, b, a);
		put32(s->p, b + 4, e);
		put32(s->p, b + 8, c);
		put32(s->p, b + 12, d);
	}
	for (i = 0; i < 6; i++)
		s->shadow[i] = rnd();
	s->scale = rnd();
	s->pend = (u8)(rnd() % 3 == 0);
	s->dn = (u8)(rnd() % 5 == 0);
	s->shd = (u8)(rnd() % 4 == 0 ? 1 + rnd() % 2 : 0);
	s->az = (u8)(rnd() % 4 == 0 ? 1 : rnd() % 6 == 0 ? 2 : 0);
	s->osd = rnd() % 4 ? rnd() % 100 : rnd();
	/* pending words: each mostly none, sometimes a few bits; the sets' sizes must not be 0 (curve_calc hangs) */
	{
		static const unsigned int fo[5] = { 880, 884, 1756, 2628, 888 };

		for (i = 0; i < 5; i++) {
			u32 f = rnd() % 3 == 0 ? 0 : rnd() % 4 == 0 ? 0xffffffffu : rnd() & rnd() & rnd();

			if (rnd() % 4 == 0)
				f = 0;
			put32(s->sets, fo[i], f);
		}
	}
	for (k = 0; k < 3; k++) {
		unsigned int o;

		for (o = 26; o <= 34; o += 2)
			if (!s->sets[k * 872 + o] && !s->sets[k * 872 + o + 1])
				s->sets[k * 872 + o] = 1;
	}
}

static void set_globals(u32 mode, u32 chi, const struct cs *in)
{
	unsigned int i;

	t32_data_d03a0[0] = (u8)mode;
	g_ch = chi;
	for (i = 0; i < 8; i++) {
		t32_data_e2764[i] = 7; t32_data_e275c[i] = 7; t32_data_e2760[i] = 7;
	}
	for (i = 0; i < 4; i++)
		t32_data_e2898[i] = 0x5a;
	t32_data_e2764[chi] = in->dn; t32_data_e275c[chi] = in->shd; t32_data_e2760[chi] = in->az;
	t32_data_e2898[chi] = in->pend;
	memcpy(t32_data_e2740, in->shadow, sizeof(in->shadow));
	t32_data_e2758[0] = in->scale;
	t32_data_e28bc[0] = in->osd;
}

static void ref_run(int which, u32 mode, u32 chi, u32 arg, const struct cs *in, struct cap *r)
{
	u8 *pb, *xp, *sb, *cx, *dpb, *dsb, *dcx;
	unsigned int i;
	u32 pv;

	t32o_reset();
	pb = t32o_alloc(PB); xp = t32o_alloc(PB); sb = t32o_alloc(SB); cx = t32o_alloc(CXS);
	dpb = t32o_alloc(PB); dsb = t32o_alloc(SB); dcx = t32o_alloc(CXS);
	memcpy(pb, in->p, PB); memcpy(xp, in->p, PB); memcpy(sb, in->sets, SB);
	memset(cx, 0, CXS);
	memcpy(cx + 4, in->coef, sizeof(in->coef));
	memcpy(dpb, in->p, PB); memcpy(dsb, in->sets, SB); memcpy(dcx, cx, CXS);
	pv = T32O_PTR(dpb);
	memcpy(dcx, &pv, 4);
	pv = T32O_PTR(in->dn ? xp : pb);
	memcpy(cx, &pv, 4);
	t32_data_dd0a4[chi] = T32O_PTR(pb) - 0xef50;
	for (i = 0; i < 4; i++) {
		t32_data_e28ac[i] = T32O_PTR(i == chi ? cx : dcx);
		t32_data_e2768[i] = T32O_PTR(i == chi ? sb : dsb);
	}
	set_globals(mode, chi, in);
	rn = 0; evn = 0;
	t32o_rd_hook = rdv;
	(void)which;
	r->ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_irq_frame_done, chi, arg, 0, 0, 0);
	t32o_rd_hook = NULL;
	memcpy(&pv, cx, 4);
	if (pv != T32O_PTR(pb)) {
		printf("MISMATCH: vendor par pointer %08x\n", pv);
		exit(1);
	}
	r->n = t32o_log.n;
	if (r->n > MAXW) {
		fprintf(stderr, "ref wrote %u > MAXW\n", r->n);
		exit(2);
	}
	for (i = 0; i < r->n; i++) {
		r->w[i].reg = t32o_log.w[i].reg;
		r->w[i].val = t32o_log.w[i].val;
	}
	r->rn = rn; r->evn = evn;
	memcpy(r->rl, rlog, sizeof(rlog)); memcpy(r->ev, ev, sizeof(ev));
	memcpy(r->p, pb, PB); memcpy(r->xp, xp, PB); memcpy(r->sets, sb, SB);
	memcpy(r->coef, cx + 4, sizeof(r->coef));
	r->pend = t32_data_e2898[chi]; r->dn = t32_data_e2764[chi]; r->shd = t32_data_e275c[chi];
	r->az = t32_data_e2760[chi];
	r->osd = t32_data_e28bc[0]; r->scale = t32_data_e2758[0];
	memcpy(r->shadow, t32_data_e2740, sizeof(r->shadow));
	for (i = 0; i < 8; i++)
		if (i != chi && (t32_data_e2764[i] != 7 || t32_data_e275c[i] != 7 || t32_data_e2760[i] != 7)) {
			printf("MISMATCH: vendor changed another channel's flags\n");
			exit(1);
		}
	for (i = 0; i < 4; i++)
		if (i != chi && t32_data_e2898[i] != 0x5a) {
			printf("MISMATCH: vendor changed another channel's pending flag\n");
			exit(1);
		}
	if (memcmp(dpb, in->p, PB) || memcmp(dsb, in->sets, SB) || memcmp(dcx + 4, in->coef, sizeof(in->coef))) {
		printf("MISMATCH: vendor changed another channel's blocks (ch %u)\n", chi);
		exit(1);
	}
}

static void p_lock(void) { evadd(1); }
static void p_unlock(void) { evadd(2); }
static void p_msg(unsigned int kind, u32 idx) { evadd(kind == 0 ? 3 : 4 | idx << 8); }

static void port_run(int which, u32 mode, u32 chi, u32 arg, const struct cs *in, struct cap *o)
{
	static const struct t32_msca_cfg_ops ops = { p_lock, p_unlock, p_msg };
	struct t32_msca_ch oc;
	struct t32_msca_cfg_st st;
	struct t32_ms_io io;
	u32 osd = in->osd;

	memset(o, 0, sizeof(*o));
	memcpy(o->p, in->p, PB);
	memcpy(o->xp, in->p, PB);
	memcpy(o->sets, in->sets, SB);
	memset(&oc, 0, sizeof(oc));
	oc.par = in->dn ? o->xp : o->p;
	memcpy(oc.coef, in->coef, sizeof(oc.coef));
	memcpy(oc.shadow, in->shadow, sizeof(oc.shadow));
	oc.scale_bits = in->scale;
	oc.pending = in->pend;
	st.c = &oc; st.sb = o->sets; st.dn = in->dn; st.shd = in->shd; st.az = in->az;
	st.par_base = T32O_PTR(o->p) - 0xef50;
	st.osd_info = &osd;
	st.w302 = &oc.coef[149];
	io.shadow = (u8)mode; io.w = o->w; io.n = 0; io.rd = rdv;
	rn = 0; evn = 0;
	(void)which; (void)arg;
	o->ret = (u32)t32_msca_irq_frame_done(&ops, &io, &st, chi);
	o->n = io.n; o->rn = rn; o->evn = evn;
	memcpy(o->rl, rlog, sizeof(rlog)); memcpy(o->ev, ev, sizeof(ev));
	memcpy(o->coef, oc.coef, sizeof(o->coef));
	o->pend = oc.pending; o->dn = st.dn; o->shd = st.shd; o->az = st.az;
	o->osd = osd; o->scale = oc.scale_bits;
	memcpy(o->shadow, oc.shadow, sizeof(o->shadow));
}

static int check(const char *what, u32 mode, u32 chi, const struct cap *r, const struct cap *o)
{
	unsigned int i;

	if (r->n != o->n || r->rn != o->rn || r->evn != o->evn || memcmp(r->rl, o->rl, sizeof(u32) * (r->rn < 64 ? r->rn : 64)) ||
	    memcmp(r->ev, o->ev, sizeof(u32) * (r->evn < 64 ? r->evn : 64)) || memcmp(r->p, o->p, PB) ||
	    memcmp(r->xp, o->xp, PB) || memcmp(r->sets, o->sets, SB) || memcmp(r->coef, o->coef, sizeof(r->coef)) ||
	    r->pend != o->pend || r->dn != o->dn || r->shd != o->shd || r->az != o->az || r->osd != o->osd ||
	    r->scale != o->scale || memcmp(r->shadow, o->shadow, sizeof(r->shadow)) || r->ret != o->ret) {
		printf("MISMATCH %s mode %u ch %u: writes %u/%u reads %u/%u events %u/%u or state (pend %u/%u dn %u/%u shd %u/%u az %u/%u osd %u/%u)\n",
		       what, mode, chi, r->n, o->n, r->rn, o->rn, r->evn, o->evn, r->pend, o->pend, r->dn, o->dn,
		       r->shd, o->shd, r->az, o->az, r->osd, o->osd);
		return 1;
	}
	for (i = 0; i < r->n; i++) {
		u32 x = r->w[i].reg, y = o->w[i].reg;

		if (r->w[i].val != o->w[i].val ||
		    (mode == 0 || (y & 0x80000000u) ? x != y : x != y && !(x & 0x80000000u))) {
			printf("MISMATCH %s mode %u: write #%u vendor %08x=%08x ours %08x=%08x\n", what, mode, i, x,
			       r->w[i].val, y, o->w[i].val);
			return 1;
		}
	}
	return 0;
}

int main(void)
{
	static struct cs in;
	static struct cap r, o;
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, i, k;

	for (c = 0; c < n; c++)
		for (k = 0; k < 1; k++) {
			u32 chi = rnd() & 3, mode = rnd() & 1, arg;

			gen(&in);
			salt = rnd();
			g_busy = rnd() % 4 == 0 ? rnd() | 2 : rnd() & ~2u;
			g_flag = rnd() % 3 ? 90u << 16 | (rnd() & 0xffff) : rnd();
			g_five = rnd() % 2 ? 5 : rnd();
			arg = rnd();	/* a1 garbage: the handler passes force 0 itself */
			ref_run((int)k, mode, chi, arg, &in, &r);
			port_run((int)k, mode, chi, arg, &in, &o);
			compared++;
			stat[k][mode]++;
			for (i = 0; i < r.evn && i < 64; i++)
				if ((r.ev[i] & 0xff) == 3)
					g_flag == 0 ? 0 : (g_busy & 2 ? busy_seen++ : notready_seen++);
				else if ((r.ev[i] & 0xff) == 4)
					linemsg++;
			if (check("irq_frame_done", mode, chi, &r, &o))
				return 1;
		}
	if (!stat[0][0] || !stat[0][1] || !busy_seen || !notready_seen || !linemsg) {
		printf("t32_mscafd_oracle: coverage too low (%u %u %u)\n", busy_seen, notready_seen, linemsg);
		return 1;
	}
	printf("t32_mscafd_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
