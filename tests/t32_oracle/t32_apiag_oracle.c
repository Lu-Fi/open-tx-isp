// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our AE sensor setters (driver/t32/tx_isp_t32_api_ag.h)
 * against the vendor tisp_set_ae_long_ag / tisp_set_ae_short_ag (with the
 * reference tisp_set_sensor_analog_gain / _short_, which take the sensor
 * block by value) and tisp_set_ae_sensor_fps from Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * The math helpers (tisp_log2_fixed_to_fixed, tisp_math_exp2,
 * fix_point_mult2_32, fix_point_div_32) and the sensor function entries are
 * recording stubs with argument-hashed results (ours reaches the same stubs
 * through the ops table); a sensor gain entry also writes the reported step
 * into info.  Compared: every call (order, arguments, info block bytes),
 * every byte of tune, buf, AE state and the sensor blocks of all channels.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_ag.h"

extern u32 t32_data_e1588[4], t32_data_e15a8[4];
extern u8 t32_data_dd2d4[0x400];

static u32 rs = 0x6c0ffeu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 hash(u32 h, u32 a, u32 b, u32 c)
{
	h ^= a * 0x9e3779b1u; h = (h << 7 | h >> 25) * 0x85ebca6bu;
	h ^= b * 0xc2b2ae35u; h = (h << 11 | h >> 21) * 0x27d4eb2fu;
	h ^= c; h ^= h >> 15;
	return h;
}

#define NREC 8
struct rec { u32 fn, a[3], bl; u8 b[128]; };
static struct rec lg[NREC];
static unsigned int nlg;
static u32 salt;

static u32 rec(u32 fn, u32 a0, u32 a1, u32 a2, const void *blob, u32 bl)
{
	struct rec *r = &lg[nlg < NREC ? nlg : NREC - 1];

	memset(r, 0, sizeof(*r));
	r->fn = fn; r->a[0] = a0; r->a[1] = a1; r->a[2] = a2;
	if (blob) {
		memcpy(r->b, blob, bl);
		r->bl = bl;
	}
	nlg++;
	return hash(salt + fn, a0, a1, a2);
}

/* math helpers */
u64 tisp_log2_fixed_to_fixed(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(1, a0, a1, a2, 0, 0); }
u64 tisp_math_exp2(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(2, a0, a1, a2, 0, 0); }
u64 fix_point_mult2_32(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(3, a0, a1, a2, 0, 0); }
u64 fix_point_div_32(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return rec(4, a0, a1, a2, 0, 0); }

/* sensor function entries (stored in the sensor blocks); info is below 4 GiB */
static u64 sensor_entry(u32 id, u32 a0, u32 a1, u32 bl)
{
	u8 *info = T32O_HOST(a1);
	u32 r = rec(id, a0, 0, 0, info, bl);

	if (id == 10 || id == 11) {	/* alloc_again: report the sensor's step in info */
		info[id == 10 ? 4 : 14] = (u8)r;
		info[id == 10 ? 5 : 15] = (u8)(r >> 8);
	}
	return r;
}
static u64 s_alloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sensor_entry(10, a0, a1, 32); }
static u64 s_alloc_s(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sensor_entry(11, a0, a1, 32); }
static u64 s_set(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sensor_entry(12, a0, a1, 32); }
static u64 s_set_s(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sensor_entry(13, a0, a1, 32); }
static u64 s_fps(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sensor_entry(14, a0, a1, 128); }

/* ---- our ops ---- */
static u32 o_log2(u32 v, u32 a, u32 b) { return (u32)tisp_log2_fixed_to_fixed(v, a, b, 0); }
static u32 o_exp2(u32 v, u32 a, u32 b) { return (u32)tisp_math_exp2(v, a, b, 0); }
static u32 o_mul(u32 q, u32 a, u32 b) { return (u32)fix_point_mult2_32(q, a, b, 0); }
static u32 o_div(u32 q, u32 a, u32 b) { return (u32)fix_point_div_32(q, a, b, 0); }
static u32 o_sns(const u8 *sblk, u32 off, u32 arg, u8 *info)
{
	static u8 low[128];	/* our info is on the host stack: the entry gets a copy below 4 GiB */
	u32 fn, bl = off == T32_SNS_SET_FPS ? 128 : 32, r;

	memcpy(&fn, sblk + off, 4);
	memcpy(low, info, bl);
	r = (u32)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(arg, T32O_PTR(low), 0, 0);
	memcpy(info, low, bl);
	return r;
}
static const struct t32_ae_ag_ops ops = {
	.log2_fixed_to_fixed = o_log2, .math_exp2 = o_exp2, .mult2_32 = o_mul, .div_32 = o_div,
	.sensor_call = o_sns,
};

u64 tisp_set_ae_long_ag(u32, u32, u32, u32);
u64 tisp_set_ae_short_ag(u32, u32, u32, u32);
u64 tisp_set_ae_sensor_fps(u32, u32, u32, u32);

#define TUNEB	2048
#define BUFB	11800
#define STB	1300
#define SNSB	(4 * 232)
static u8 *tune, *buf, *st, *tblk;
static u8 tune0[TUNEB], buf0[BUFB], st0[STB], sns0[SNSB];

static void w32(u8 *p, u32 v) { memcpy(p, &v, 4); }

static unsigned int failed, compared;

static void run(unsigned int k, u32 ch, unsigned int c)
{
	static struct rec vl[NREC];
	static u8 vt[TUNEB], vb[BUFB], vs[STB], vsn[SNSB];
	unsigned int vn, i, cc = ch & 3;
	u32 fns[5] = { T32O_PTR((void *)s_alloc), T32O_PTR((void *)s_alloc_s), T32O_PTR((void *)s_set),
		       T32O_PTR((void *)s_set_s), T32O_PTR((void *)s_fps) };
	static const u32 offs[5] = { 124, 128, 184, 188, 212 };
	struct t32_ae_ag_ctx ctx;

	t32o_reset();
	tune = t32o_alloc(TUNEB); buf = t32o_alloc(BUFB); st = t32o_alloc(STB); tblk = t32o_alloc(8);
	salt = rnd();
	for (i = 0; i < TUNEB; i++) tune0[i] = (u8)rnd();
	for (i = 0; i < BUFB; i++) buf0[i] = (u8)rnd();
	for (i = 0; i < STB; i++) st0[i] = (u8)rnd();
	for (i = 0; i < SNSB; i++) sns0[i] = (u8)rnd();
	for (i = 0; i < 4 * 5; i++)
		w32(sns0 + (i / 5) * 232 + offs[i % 5], fns[i % 5]);
	if (k < 2) {	/* gain: AE target equal to the sensor gain or not, freeze flags */
		u32 ag = (c & 3) ? rnd() : rnd() & 0xffff;

		w32(st0 + (k ? 596 : 580), ag);
		w32(buf0 + (k ? 7636 : 7620), (c % 5) ? rnd() : ag);
		st0[k ? 1283 : 1279] = (c & 4) ? (u8)rnd() : 0;
		st0[1284] = (c & 8) ? (u8)rnd() : 0;
		if (c & 16)
			w32(st0 + (k ? 668 : 636), 0xffffffffu);	/* the limit does not clamp */
	} else {	/* fps: mode 1 / fixed fps / sensor fps; clamps; equal to the sensor word */
		u32 sh = rnd() % 40, den = (c % 7) ? rnd() % 64 + 1 : rnd(), num = rnd() & 0x3ffff;
		u32 mn = (c & 2) ? 0 : rnd(), mx = (c & 4) ? 0xffffffffu : rnd();

		tune0[1736] = (u8)sh; tune0[1737] = (c & 32) ? 0 : (u8)rnd();
		w32(buf0 + 11772, (c % 3) ? 1 : rnd() % 3);
		w32(buf0 + 11776, num);
		w32(buf0 + 11780, den);
		w32(st0 + 1264, mn); w32(st0 + 1268, mx);
		w32(tune0 + 1740, (c & 8) ? 1 : rnd() % 3);
		if (c & 64) {	/* the AE value sits exactly on a limit */
			u32 v = (num / den) << (sh & 31);

			if (c & 128)
				mn = v, w32(tune0 + 1664, mx);
			else
				mx = v, w32(tune0 + 1664, mn);
			w32(st0 + 1264, mn); w32(st0 + 1268, mx);
		}
		if (c & 16) {	/* the sensor already has the word */
			u32 w = (t32_ag_rd32(buf0 + 11772) == 1) ? (num << 16) + den :
			    (((t32_ag_rd32(tune0 + 1664) << 1) >> (sh & 31)) << 16) + 2;
			w32(sns0 + cc * 232 + 56, w);
		}
	}
	t32_data_e1588[cc] = T32O_PTR(st);
	w32(tblk, T32O_PTR(tune)); w32(tblk + 4, T32O_PTR(buf));
	t32_data_e15a8[cc] = T32O_PTR(tblk);

	memcpy(tune, tune0, TUNEB); memcpy(buf, buf0, BUFB); memcpy(st, st0, STB); memcpy(t32_data_dd2d4, sns0, SNSB);
	nlg = 0;
	t32o_call((t32o_fn)(void (*)(void))(k == 0 ? tisp_set_ae_long_ag : k == 1 ? tisp_set_ae_short_ag :
					    tisp_set_ae_sensor_fps), ch, rnd(), rnd(), rnd(), 0);
	vn = nlg;
	memcpy(vl, lg, sizeof(vl));
	memcpy(vt, tune, TUNEB); memcpy(vb, buf, BUFB); memcpy(vs, st, STB); memcpy(vsn, t32_data_dd2d4, SNSB);

	memcpy(tune, tune0, TUNEB); memcpy(buf, buf0, BUFB); memcpy(st, st0, STB); memcpy(t32_data_dd2d4, sns0, SNSB);
	nlg = 0;
	ctx.tune = tune; ctx.buf = buf; ctx.st = st; ctx.sns = t32_data_dd2d4 + cc * 232;
	if (k < 2)
		t32_set_ae_ag(&ops, &ctx, ch, k);
	else
		t32_set_ae_sensor_fps(&ops, &ctx, ch);
	compared++;
	if (vn != nlg || memcmp(vt, tune, TUNEB) || memcmp(vb, buf, BUFB) || memcmp(vs, st, STB) ||
	    memcmp(vsn, t32_data_dd2d4, SNSB))
		goto bad;
	for (i = 0; i < vn && i < NREC; i++)
		if (vl[i].fn != lg[i].fn || memcmp(vl[i].a, lg[i].a, sizeof(vl[i].a)) || vl[i].bl != lg[i].bl ||
		    memcmp(vl[i].b, lg[i].b, vl[i].bl))
			goto bad;
	return;
bad:
	if (failed++ < 8) {
		printf("MISMATCH k=%u ch=%08x: calls %u/%u mem %d%d%d%d\n", k, ch, vn, nlg, !!memcmp(vt, tune, TUNEB),
		       !!memcmp(vb, buf, BUFB), !!memcmp(vs, st, STB), !!memcmp(vsn, t32_data_dd2d4, SNSB));
		for (i = 0; i < vn && i < NREC; i++)
			printf("  vendor %u(%08x %08x %08x) blob %u\n", vl[i].fn, vl[i].a[0], vl[i].a[1], vl[i].a[2], vl[i].bl);
		for (i = 0; i < nlg && i < NREC; i++)
			printf("  ours   %u(%08x %08x %08x) blob %u\n", lg[i].fn, lg[i].a[0], lg[i].a[1], lg[i].a[2], lg[i].bl);
	}
}

int main(void)
{
	unsigned int c, k, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 10000;

	for (k = 0; k < 3; k++)
		for (c = 0; c < n; c++)
			run(k, (c & 1) ? (rnd() & ~0xffu) | (rnd() & 3) : rnd() & 3, c);
	printf("t32_apiag_oracle: 3 entry points, %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
