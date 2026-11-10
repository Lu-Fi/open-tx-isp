// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: AWB API accessors tisp_awb_api_set_face / get_face /
 * set_ct_trend_offset / get_ct_trend_offset / get_awb_stable.  Runs the
 * unmodified vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc)
 * and our t32_awb_api_* from driver/t32/tx_isp_t32_stats.h from the same
 * random state and compares the whole tune and section block of the used
 * channel and of a decoy channel, the output buffer, the register log, the
 * event log and every argument of tisp_awb_face_pos (a recording stub on
 * both sides; its body is not part of this group).
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
#define OUTB	64u

extern u32 t32_data_e16b4[4];
extern u64 tisp_awb_api_set_face(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_api_get_face(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_api_set_ct_trend_offset(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_api_get_ct_trend_offset(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_awb_api_get_awb_stable(u32 a0, u32 a1, u32 a2, u32 a3);

struct posrec {
	u32 n, a[6];
};
static struct posrec pr;

u64 tisp_awb_face_pos(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	pr.n++;
	pr.a[0] = a0; pr.a[1] = a1; pr.a[2] = a2; pr.a[3] = a3; pr.a[4] = a4; pr.a[5] = a5;
	return 0;
}

static void pos_hook(void *priv, u32 ch, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	(void)priv;
	tisp_awb_face_pos(ch, a1, a2, a3, a4, a5);
}

static u32 rs = 0x9e3779b1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_SETFACE, C_GETFACE, C_SETCT, C_GETCT, C_STABLE, C_N };
static const char *const names[C_N] = {
	"set_face", "get_face", "set_ct_trend_offset", "get_ct_trend_offset", "get_awb_stable"
};

struct env {
	u8 *tune[2], *sec[2], *in, *out;
};
static u8 d_tune[2][TUNEB], d_sec[2][SECB], d_in[64];
static u8 r_tune[2][TUNEB], r_sec[2][SECB], r_out[OUTB];
static u32 r_evlog[T32O_EVLOG][9], r_evn;
static unsigned int r_n;

static int setup(struct env *e, u32 ch)
{
	u32 k, d = (ch + 1) & 3;
	u32 slot[2] = { ch, d };

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
		t32_data_e16b4[slot[k]] = T32O_PTR(ctx);
	}
	e->in = t32o_alloc(64);
	e->out = t32o_alloc(OUTB);
	if (!e->in || !e->out)
		return 2;
	memcpy(e->in, d_in, 64);
	memset(e->out, 0xa5, OUTB);
	return 0;
}

static void fill(u8 *p, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		p[i] = (u8)(rnd() >> 11);
}

#define VC(fn, a0, a1) t32o_call((t32o_fn)(void (*)(void))fn, a0, a1, 0, 0, 0)

static int run_case(unsigned int which, u32 ch)
{
	struct env e;
	struct t32_awb_ctx c;
	struct posrec rp;
	const char *what = NULL;
	unsigned int i, rn;

	memset(&pr, 0, sizeof(pr));
	t32o_reset();
	if (setup(&e, ch))
		return 2;
	switch (which) {
	case C_SETFACE: VC(tisp_awb_api_set_face, ch, T32O_PTR(e.in)); break;
	case C_GETFACE: VC(tisp_awb_api_get_face, ch, T32O_PTR(e.out)); break;
	case C_SETCT: VC(tisp_awb_api_set_ct_trend_offset, ch, T32O_PTR(e.in)); break;
	case C_GETCT: VC(tisp_awb_api_get_ct_trend_offset, ch, T32O_PTR(e.out)); break;
	case C_STABLE: VC(tisp_awb_api_get_awb_stable, ch, T32O_PTR(e.out)); break;
	}
	rp = pr;
	memset(&pr, 0, sizeof(pr));
	for (i = 0; i < 2; i++) {
		memcpy(r_tune[i], e.tune[i], TUNEB);
		memcpy(r_sec[i], e.sec[i], SECB);
	}
	memcpy(r_out, e.out, OUTB);
	memcpy(r_evlog, t32o_evlog, sizeof(r_evlog));
	r_evn = t32o_ev_n;
	rn = r_n = t32o_log.n;

	t32o_reset();
	if (setup(&e, ch))
		return 2;
	c.tune = e.tune[0];
	c.sec = e.sec[0];
	c.mode = NULL;
	c.base = NULL;
	switch (which) {
	case C_SETFACE: t32_awb_api_set_face(&c, ch, e.in, pos_hook, NULL); break;
	case C_GETFACE: t32_awb_api_get_face(&c, e.out); break;
	case C_SETCT: t32_awb_api_set_ct_trend_offset(&c, e.in); break;
	case C_GETCT: t32_awb_api_get_ct_trend_offset(&c, e.out); break;
	case C_STABLE: *(u32 *)e.out = t32_awb_api_get_awb_stable(&c); break;
	}
	if (rn != t32o_log.n || r_evn != t32o_ev_n)
		what = "register / event count";
	if (!what && memcmp(r_evlog, t32o_evlog, sizeof(r_evlog)))
		what = "event log";
	if (!what && memcmp(&rp, &pr, sizeof(pr)))
		what = "face_pos call";
	for (i = 0; !what && i < 2; i++) {
		if (memcmp(r_tune[i], e.tune[i], TUNEB))
			what = i ? "decoy tune block" : "tune block";
		else if (memcmp(r_sec[i], e.sec[i], SECB))
			what = i ? "decoy section block" : "section block";
	}
	if (!what && memcmp(r_out, e.out, OUTB))
		what = "output buffer";
	if (what) {
		printf("MISMATCH %s ch %u: %s\n", names[which], ch, what);
		return 1;
	}
	return 0;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 2000;
	unsigned int c;

	for (c = 0; c < n; c++) {
		unsigned int which = c % C_N;
		u32 ch = rnd() & 3;
		int rc;

		fill(d_tune[0], TUNEB); fill(d_tune[1], TUNEB);
		fill(d_sec[0], SECB); fill(d_sec[1], SECB);
		fill(d_in, 64);
		rc = run_case(which, ch);
		if (rc) {
			printf("t32_awbaa_oracle: FAILED at case %u\n", c);
			return rc;
		}
	}
	printf("t32_awbaa_oracle: random %u compared, 0 mismatches\n", n);
	return 0;
}
