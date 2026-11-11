// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_aetrig.h (tisp_ae_par_sensor_trig,
 * tisp_ae_long/short_set_sensor_integration_time, tisp_ae_algo_waitdone,
 * tisp_ae_algorithm, tisp_awb_algo_handle) against the vendor functions
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Per case random channel
 * (high bits set where the vendor masks), random flags (1 / 0 / other),
 * random parameter / state / tuning / output blocks (the same on both
 * sides), scripted completion result.  Compared: return values, every byte
 * of the parameter and output blocks of all channels, every callee call in
 * order with its arguments and the buffer contents it gets (the stubs
 * write deterministic results into the buffers), the sensor callback's
 * time and info word 2, the AWB attribute words the vendor sets (0 .. 5,
 * 16).  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_aetrig.h"

extern u8 t32_data_e1514[0x300], t32_data_dd2d4[0x400];
extern u32 t32_data_e1588[4], t32_data_e15a8[4], t32_data_e15d8[4];
u32 t32_data_e15b8[4] __attribute__((weak));	/* .bss: AE algorithm running flags */
extern u64 tisp_ae_par_sensor_trig(u32, u32, u32, u32), tisp_ae_long_set_sensor_integration_time(u32, u32, u32, u32),
	   tisp_ae_short_set_sensor_integration_time(u32, u32, u32, u32), tisp_ae_algo_waitdone(u32, u32, u32, u32),
	   tisp_ae_algorithm(u32, u32, u32, u32), tisp_awb_algo_handle(u32, u32, u32, u32);

static u32 rs = 0x2545f491u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 8
#define LW 12
struct hlog { unsigned int n; u32 e[LOGN][LW]; };
static struct hlog vl, ol, *cur;
static void hrec(u32 kind, const u32 *v, unsigned int nv)
{
	if (cur->n < LOGN) {
		memset(cur->e[cur->n], 0, sizeof(cur->e[0]));
		cur->e[cur->n][0] = kind;
		memcpy(&cur->e[cur->n][1], v, nv * 4);
	}
	cur->n++;
}
static u32 wait_ret;
static u32 mix(u32 a, u32 b) { return (a * 0x9e3779b1u) ^ (b + 0x7f4a7c15u) ^ (a >> 7); }

/* shared callee bodies (vendor stubs and our hooks use the same) */
static u32 c_wait(u32 comp, u32 to) { u32 v[2] = { comp, to }; hrec(1, v, 2); return wait_ret; }
static void c_mean(u32 ch, u32 *mean, u32 *sum, u32 w, u32 h, u32 *wsum)
{
	u32 v[8] = { ch, w, h, *mean, sum[0], sum[1], wsum[0], wsum[1] };

	hrec(2, v, 8);
	*mean = mix(ch, w); sum[0] = mix(w, h); sum[1] = mix(h, ch);
	wsum[0] = mix(sum[0], 3); wsum[1] = mix(ch, sum[1]);
}
static u32 c_tune(u32 ch, u32 mean, u32 *sum, u32 *out)
{
	u32 v[6] = { ch, mean, sum[0], sum[1], out[0], out[1] };

	hrec(3, v, 6);
	return mix(mean, sum[1]) ^ out[0];
}
static u32 c_awb(u32 a0, const u32 *attr)
{
	u32 v[8] = { a0, attr[0], attr[1], attr[2], attr[3], attr[4], attr[5], attr[16] };

	hrec(4, v, 8);
	return mix(a0, attr[1]);
}
static u32 c_sns(u32 kind, u32 t, const u32 *info) { u32 v[2] = { t, info[2] }; hrec(kind, v, 2); return mix(t, kind); }

u64 private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return c_wait(a0, a1); }
u64 tisp_ae_weight_mean(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	c_mean(a0, T32O_HOST(a1), T32O_HOST(a2), a3, a4, T32O_HOST(a5));
	return 0;
}
u64 tisp_ae_tune(u32 a0, u32 a1, u32 a2, u32 a3) { return c_tune(a0, a1, T32O_HOST(a2), T32O_HOST(a3)); }
u64 tisp_s_awb_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return c_awb(a0, T32O_HOST(a1)); }
/* the sensor callbacks get a 32-bit stack address from the vendor and a host pointer from ours:
 * the second parameter is pointer sized (the vendor's u32 argument arrives zero-extended) */
static u64 v_sns_long(u32 a0, uintptr_t a1) { return c_sns(5, a0, (const u32 *)a1); }
static u64 v_sns_short(u32 a0, uintptr_t a1) { return c_sns(6, a0, (const u32 *)a1); }

static u32 o_wait(void *c, u32 to) { return c_wait(T32O_PTR(c), to); }
static u32 o_awb(u32 a0, u32 *attr) { return c_awb(a0, attr); }
static const struct t32_aetrig_ops ops = { o_wait, c_mean, c_tune, o_awb };

#define PARB 1300
#define STB 2948
#define TUNB 8572
#define OUTB 10860

static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	u8 *par[2][4], *out[2][4], *st[4], *tun[4], *ctx[4];
	u32 *msg = t32o_alloc(32);
	unsigned long n, N = 20000, per[6] = { 0 };
	u32 c, s, i;

	for (c = 0; c < 4; c++) {
		for (s = 0; s < 2; s++) { par[s][c] = t32o_alloc(PARB); out[s][c] = t32o_alloc(OUTB); }
		st[c] = t32o_alloc(STB); tun[c] = t32o_alloc(TUNB); ctx[c] = t32o_alloc(2016);
	}
	for (n = 0; n < N; n++) {
		struct t32_aetrig_dev d;
		u8 trig[4], sns[0x400];
		u32 alg[4], ctxw[4], parw[2][4], outw[2][4];
		int fn = (int)(rnd() % 6);
		u32 ch = rnd() & 3, a0 = ch | (rnd() & 0xffffff00u), rv = 0, ro = 0, sv;

		for (c = 0; c < 4; c++) {
			u32 r = rnd() % 3;

			trig[c] = r == 0 ? 1 : (r == 1 ? 0 : rnd());
			r = rnd() % 3;
			alg[c] = r == 0 ? 1 : (r == 1 ? 0 : rnd());
			fill(par[0][c], PARB); memcpy(par[1][c], par[0][c], PARB);
			fill(out[0][c], OUTB); memcpy(out[1][c], out[0][c], OUTB);
			fill(st[c], STB); fill(tun[c], TUNB);
			if (rnd() & 1) { u32 one = 1; memcpy(st[c] + T32_AET_ST_MODE, &one, 4); }
			{ u32 w[2] = { T32O_PTR(st[c]), T32O_PTR(tun[c]) }; memcpy(ctx[c], w, 8); }
			ctxw[c] = T32O_PTR(ctx[c]);
			for (s = 0; s < 2; s++) { parw[s][c] = T32O_PTR(par[s][c]); outw[s][c] = T32O_PTR(out[s][c]); }
		}
		fill(sns, sizeof(sns));
		for (c = 0; c < 4; c++) {
			u32 fl = T32O_PTR((void *)v_sns_long), fs = T32O_PTR((void *)v_sns_short);

			memcpy(sns + T32_AET_SNS_STRIDE * c + T32_AET_SNS_SET_LONG, &fl, 4);
			memcpy(sns + T32_AET_SNS_STRIDE * c + T32_AET_SNS_SET_SHORT, &fs, 4);
		}
		for (i = 0; i < 8; i++) msg[i] = rnd();
		wait_ret = (rnd() & 1) ? 0 : rnd();
		sv = rs;

		/* ours */
		t32o_reset();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		d.ops = &ops; d.sensor_trig = trig; d.par = parw[1]; d.ctx = ctxw; d.alg_run = alg;
		d.out = outw[1]; d.sensor = sns;
		switch (fn) {
		case 0: t32_ae_par_sensor_trig(&d, ch); break;
		case 1: ro = t32_ae_long_set_sensor_integration_time(&d, a0); break;
		case 2: ro = t32_ae_short_set_sensor_integration_time(&d, a0); break;
		case 3: ro = (u32)t32_ae_algo_waitdone(&d, ch); break;
		case 4: ro = t32_ae_algorithm(&d, a0); break;
		default: ro = t32_awb_algo_handle(&d, a0, msg); break;
		}

		/* vendor */
		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl; rs = sv;
		memcpy(t32_data_e1514, trig, 4);
		memcpy(t32_data_dd2d4, sns, sizeof(sns));
		for (c = 0; c < 4; c++) {
			t32_data_e1588[c] = parw[0][c]; t32_data_e15a8[c] = ctxw[c];
			t32_data_e15b8[c] = alg[c]; t32_data_e15d8[c] = outw[0][c];
		}
		switch (fn) {
		case 0: t32o_call((t32o_fn)(void (*)(void))tisp_ae_par_sensor_trig, ch, rnd(), rnd(), rnd(), 0); break;
		case 1: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_long_set_sensor_integration_time, a0, rnd(), rnd(), rnd(), 0); break;
		case 2: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_short_set_sensor_integration_time, a0, rnd(), rnd(), rnd(), 0); break;
		case 3: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_algo_waitdone, ch, rnd(), rnd(), rnd(), 0); break;
		case 4: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_ae_algorithm, a0, rnd(), rnd(), rnd(), 0); break;
		default: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_awb_algo_handle, a0, T32O_PTR(msg), rnd(), rnd(), 0); break;
		}

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e))) {
			printf("t32_aetrig_oracle: FAIL case %lu fn %d ch %u: ret %#x/%#x hooks %u/%u\n", n, fn, ch, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x / %u %#x %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3],
				       ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3]);
			return 1;
		}
		for (c = 0; c < 4; c++)
			if (memcmp(par[0][c], par[1][c], PARB) || memcmp(out[0][c], out[1][c], OUTB)) {
				printf("t32_aetrig_oracle: FAIL case %lu fn %d ch %u: block of channel %u differs\n", n, fn, ch, c);
				return 1;
			}
		per[fn]++;
	}
	printf("t32_aetrig_oracle: PASS %lu cases (par_sensor_trig %lu, long_it %lu, short_it %lu, waitdone %lu, algorithm %lu, awb_algo_handle %lu)\n",
	       N, per[0], per[1], per[2], per[3], per[4], per[5]);
	return 0;
}
