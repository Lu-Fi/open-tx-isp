// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AWB face position (tisp_awb_face_pos).  Runs the unmodified
 * vendor function (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our
 * t32_awb_face_pos from driver/t32/tx_isp_t32_stats.h from the same random
 * state and compares the tune / sec / hw blocks of channel ch and of a decoy
 * channel, the register writes and the trigger calls (logged in order, both
 * sides through the same recording stubs), and the event log.  Each case also
 * runs the vendor with t32_data_d03a0 = 1 (shadow register indices 1923 /
 * 1924, queued instead of written) and checks that only the addresses differ
 * (1923 -> 0x160e0, 1924 -> 0x160e4); our port models the direct addresses.
 * Exit 0 = all equal, 1 = first mismatch, 2 = setup failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_stats.h"

#define SECB	0x1e000u
#define TUNEB	0x2000u

extern u32 t32_data_e16b4[4];
extern u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_face_pos(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);

/* mode 0 (d03a0 != 1): the vendor tail call to system_reg_write; mode 1: queued as shadow index -> value */
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a3;
	if (t32_data_d03a0[0] != 1)
		return system_reg_write(a1, a2, 0, 0);
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = 0x80000000u | a1;
		t32o_log.w[t32o_log.n].val = a2;
	}
	t32o_log.n++;
	return 0;
}

u64 system_reg_set_awb_trig(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = 0xf0000000u + a0;
		t32o_log.w[t32o_log.n].val = a1;
	}
	t32o_log.n++;
	return 0;
}

static void wr_hook(void *priv, u32 reg, u32 val)
{
	(void)priv;
	system_reg_write(reg, val, 0, 0);
}

static void trig_hook(void *priv, u32 kind, u32 arg)
{
	(void)priv;
	system_reg_set_awb_trig(kind, arg, 0, 0);
}

static u32 rs = 0x4f1bbcdcu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct env {
	u8 *tune[2], *sec[2];
};
static u8 d_tune[2][TUNEB], d_sec[2][SECB];
struct snap {
	u8 tune[2][TUNEB], sec[2][SECB];
	u32 evlog[T32O_EVLOG][9], evn, logn;
	struct { u32 reg, val; } w[32];
};
static struct snap sr, so, sm;

static int setup(struct env *e, u32 ch, u32 mode)
{
	u32 k, slot[2] = { ch, (ch + 1) & 3 };

	for (k = 0; k < 2; k++) {
		u8 *ctx = t32o_alloc(16);

		e->tune[k] = t32o_alloc(TUNEB);
		e->sec[k] = t32o_alloc(SECB);
		if (!ctx || !e->tune[k] || !e->sec[k])
			return 2;
		memcpy(e->tune[k], d_tune[k], TUNEB);
		memcpy(e->sec[k], d_sec[k], SECB);
		*(u32 *)(ctx + 0) = T32O_PTR(e->tune[k]);
		*(u32 *)(ctx + 4) = T32O_PTR(e->sec[k]);
		*(u32 *)(ctx + 8) = 0;
		*(u32 *)(ctx + 12) = 0;
		t32_data_e16b4[slot[k]] = T32O_PTR(ctx);
	}
	t32_data_d03a0[0] = (u8)mode;
	return 0;
}

static void take(struct snap *s, const struct env *e)
{
	u32 k;

	for (k = 0; k < 2; k++) {
		memcpy(s->tune[k], e->tune[k], TUNEB);
		memcpy(s->sec[k], e->sec[k], SECB);
	}
	memcpy(s->evlog, t32o_evlog, sizeof(s->evlog));
	s->evn = t32o_ev_n;
	s->logn = t32o_log.n;
	for (k = 0; k < 32 && k < t32o_log.n; k++) {
		s->w[k].reg = t32o_log.w[k].reg;
		s->w[k].val = t32o_log.w[k].val;
	}
}

static void fill(u8 *p, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		p[i] = (u8)(rnd() >> 11);
}

static u32 pick16(void)
{
	u32 r = rnd() % 8;

	return r == 0 ? 0 : r == 1 ? 1 : r == 2 ? 2 : r == 3 ? rnd() % 40 : r == 4 ? 0xffff : r == 5 ? rnd() % 700 : rnd() & 0xffff;
}

static int run_case(u32 ch, u32 flag, u32 a2, u32 a3, u32 a4, u32 a5)
{
	struct env e;
	struct t32_awb_ctx c;
	const char *what = NULL;
	u32 k;

	t32o_reset();
	if (setup(&e, ch, 0))
		return 2;
	t32o_arg5 = a5;
	(void)t32o_call((t32o_fn)(void (*)(void))tisp_awb_face_pos, ch, flag, a2, a3, a4);
	take(&sr, &e);

	t32o_reset();
	if (setup(&e, ch, 0))
		return 2;
	memset(&c, 0, sizeof(c));
	c.tune = e.tune[0];
	c.sec = e.sec[0];
	t32_awb_face_pos(&c, flag, a2, a3, a4, a5, wr_hook, trig_hook, NULL);
	take(&so, &e);

	t32o_reset();
	if (setup(&e, ch, 1))
		return 2;
	t32o_arg5 = a5;
	(void)t32o_call((t32o_fn)(void (*)(void))tisp_awb_face_pos, ch, flag, a2, a3, a4);
	take(&sm, &e);

	if (so.logn != sr.logn || so.evn != sr.evn || memcmp(so.evlog, sr.evlog, sizeof(so.evlog)))
		what = "register / event log";
	for (k = 0; !what && k < sr.logn && k < 32; k++)
		if (so.w[k].reg != sr.w[k].reg || so.w[k].val != sr.w[k].val)
			what = "register write";
	for (k = 0; !what && k < 2; k++) {
		if (memcmp(so.tune[k], sr.tune[k], TUNEB))
			what = k ? "decoy tune block" : "tune block";
		else if (memcmp(so.sec[k], sr.sec[k], SECB))
			what = k ? "decoy sec block" : "sec block";
	}
	/* shadow variant: same memory, same values, the two window registers addressed by index */
	if (!what && (sm.logn != sr.logn || memcmp(sm.tune, sr.tune, sizeof(sr.tune)) || memcmp(sm.sec, sr.sec, sizeof(sr.sec))))
		what = "shadow variant state";
	for (k = 0; !what && k < sm.logn && k < 32; k++) {
		u32 reg = sm.w[k].reg;

		if (reg == (0x80000000u | 1923))
			reg = T32_AWB_FACE_REG_Y;
		else if (reg == (0x80000000u | 1924))
			reg = T32_AWB_FACE_REG_X;
		if (reg != so.w[k].reg || sm.w[k].val != so.w[k].val)
			what = "shadow variant write";
	}
	if (what) {
		printf("MISMATCH ch %u flag %u: %s\n", ch, flag, what);
		return 1;
	}
	return 0;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 8000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		u32 ch = rnd() & 3, k, r, flag, a[4];
		int rc;

		for (k = 0; k < 2; k++) {
			fill(d_tune[k], TUNEB);
			fill(d_sec[k], SECB);
		}
		/* picture size: area threshold small / large; the counter word near the wrap value */
		r = rnd() % 4;
		*(uint16_t *)(d_sec[0] + T32_AWB_SEC_FACE_W) = r == 0 ? (uint16_t)(rnd() % 80) : r == 1 ? 1920 : rnd() & 0xffff;
		*(uint16_t *)(d_sec[0] + T32_AWB_SEC_FACE_H) = r == 0 ? (uint16_t)(rnd() % 80) : r == 1 ? 1080 : rnd() & 0xffff;
		r = rnd() % 4;
		if (r == 0)
			*(u32 *)(d_sec[0] + T32_AWB_SEC_FACE_CNT) = 254;
		else if (r == 1)
			*(u32 *)(d_sec[0] + T32_AWB_SEC_FACE_CNT) = 255;
		for (k = 0; k < 4; k++)
			a[k] = pick16() | ((rnd() & 3) == 0 ? (rnd() & 0xffff0000u) : 0);
		if (rnd() & 1) {	/* a plausible face box on a picture of the stored size */
			u32 w = *(uint16_t *)(d_sec[0] + T32_AWB_SEC_FACE_W) | 1, h = *(uint16_t *)(d_sec[0] + T32_AWB_SEC_FACE_H) | 1;

			a[0] = rnd() % w; a[1] = a[0] + rnd() % w; a[2] = rnd() % h; a[3] = a[2] + rnd() % h;
		}
		r = rnd() % 4;
		flag = r == 0 ? 1 : r == 1 ? 0 : r == 2 ? 0x101 : rnd();
		rc = run_case(ch, flag, a[0], a[1], a[2], a[3]);
		if (rc) {
			printf("t32_awbad_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_awbad_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
