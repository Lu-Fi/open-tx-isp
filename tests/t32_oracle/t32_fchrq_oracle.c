// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our frame channel buffer request
 * (driver/t32/tx_isp_t32_fch_req.h) against the unmodified vendor
 * frame_channel_reqbufs (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Random channels (buffer count, memory type, state, index), requests
 * (count 0..80), kernel / user callers, module flags and tables, a kmalloc
 * pool inside the image with a scripted failure, and scripted hook answers
 * run through ours, are restored and run through the vendor; compared:
 * every byte (the allocated buffers included), the module tables, the
 * return value and the ordered hook log.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fch_req.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
char t32_data_c5fcc[] = "The parameter from user is invalid!\n";
char t32_data_c5f08[] = "Failed to copy from user\n";
char t32_data_c5f98[] = "Failed to copy to user\n";
char t32_data_c5ff4[] = "reqbufs: streaming active\n";
char t32_data_c6010[] = "reqbufs: the memory type isn't userptr!\n";
char t32_data_c603c[] = "Memory alloc for buffer struct failed\n";
char t32_data_c6064[] = "Allocated %d buffers!\n";
char t32_data_c607c[] = "Memory allocation failed\n";
char t32_data_c6098[] = "Can't allocation %d buffers!\n";
char t32_data_c60b8[] = "streamon: driver refused to start streaming\n";
char t32_data_c60e8[] = "requested buffer Failed\n";
u32 t32_data_d9008, t32_data_d9014;
u32 t32_data_f37d4[12], t32_data_f37a4[12], t32_data_f3774[12], t32_data_d9b44[12], t32_data_d9d14[4];

u64 frame_channel_reqbufs(u32, u32, u32, u32);

static u32 rng = 0x165667b1u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 96
static struct { unsigned int n; u32 e[MAXE][4]; } EV[2];
static int side;
static u8 *img;
static u32 q_reset, r_cfu, r_ctu, r_ev[2], nev, nkm, km_fail, ub[5];
static void ev(u32 a, u32 b, u32 c, u32 d)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	}
}
static u32 hash(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

#define O_CH	0x0000u
#define O_UBUF	0x0400u
#define O_POOL	0x0500u		/* 70 x 0x80 */
#define IMGSZ	0x2800u
static u8 save0[IMGSZ], save1[IMGSZ];

static u32 h_cfu(u8 *dst, u32 user, u32 n) { ev(1, user, n, 0); memcpy(dst, ub, n); return r_cfu; }
static u32 h_ctu(u32 user, const u8 *src, u32 n) { ev(2, user, n, hash(src, n)); return r_ctu; }
static void h_qfree(u8 *q, u32 n) { ev(3, T32O_PTR(q), n, 0); if (q_reset)	/* like __vb2_queue_free */
		t32_g_wr32(q - T32_RQ_QUEUE, T32_RQ_NBUFS, 0); }
static u32 h_km(u32 size, u32 fl)
{
	u32 k = nkm++;

	ev(4, size, fl, k);
	return k == km_fail || k >= 70 ? 0 : T32O_PTR(img + O_POOL + k * 0x80);
}
static int h_ev(u32 pad, u32 cmd, u8 *d) { ev(5, pad, cmd, t32_g_rd32(d, 0)); return (int)r_ev[nev++ % 2]; }
static const struct t32_fchrq_os os = { h_cfu, h_ctu, h_qfree, h_km, h_ev, &t32_data_d9008, &t32_data_d9014,
					t32_data_f37d4, t32_data_f37a4, t32_data_f3774, t32_data_d9b44, t32_data_d9d14 };
u64 private_copy_from_user(u32 d, u32 u, u32 n, u32 a3) { (void)a3; return h_cfu(T32O_HOST(d), u, n); }
u64 private_copy_to_user(u32 u, u32 s, u32 n, u32 a3) { (void)a3; return h_ctu(u, T32O_HOST(s), n); }
u64 __vb2_queue_free(u32 q, u32 n, u32 a2, u32 a3) { (void)a2; (void)a3; h_qfree(T32O_HOST(q), n); return 0; }
u64 private_kmalloc(u32 size, u32 fl, u32 a2, u32 a3) { (void)a2; (void)a3; return h_km(size, fl); }
u64 tx_isp_send_event_to_remote(u32 pad, u32 cmd, u32 d, u32 a3) { (void)a3; return (u32)h_ev(pad, cmd, T32O_HOST(d)); }

int isp_printf(unsigned int level, const char *fmt, ...)
{
	va_list ap;
	u32 a = 0;

	va_start(ap, fmt);
	if (strchr(fmt, '%'))
		a = va_arg(ap, unsigned int);
	va_end(ap);
	ev(9, level, hash((const u8 *)fmt, (u32)strlen(fmt)), a);
	return 0;
}

static u32 tab0[5][12], tab1[5][12];
static u32 *const tabs[5] = { t32_data_f37d4, t32_data_f37a4, t32_data_f3774, t32_data_d9b44, t32_data_d9d14 };
static const u32 tlen[5] = { 12, 12, 12, 12, 4 };

static int fail(const char *what, unsigned int c)
{
	printf("t32_fchrq_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, j, n = 40000, hit[4] = { 0 }, big = 0;

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u8 *ch = img + O_CH;
		u32 chw = T32O_PTR(ch), arg = T32O_PTR(img + O_UBUF), ro = 0, rr = 0;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		t32_g_wr32(ch, T32_RQ_INDEX, rnd() % 4 ? rnd() % 12 : rnd() % 10);
		t32_g_wr32(ch, T32_RQ_NBUFS, rnd() % 2 ? 0 : rnd() % 8);
		t32_g_wr32(ch, T32_RQ_BUFSIZE, 0x68 + rnd() % 0x18);
		t32_g_wr32(ch, T32_RQ_MEMORY, rnd() % 3);
		t32_g_wr32(ch, T32_RQ_STATE, rnd() % 6 ? rnd() & ~1u : rnd());
		t32_g_wr32(ch, T32_RQ_KERNEL, rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd());
		ub[0] = rnd() % 6 ? rnd() % 9 : rnd() % 8 ? rnd() % 81 : rnd();
		for (i = 1; i < 5; i++)
			ub[i] = rnd();
		if (rnd() % 6)
			ub[2] = t32_g_rd32(ch, T32_RQ_MEMORY);
		memcpy(img + O_UBUF, ub, 20);
		if (rnd() % 8 == 0)
			t32_g_wr32(img + O_UBUF, 0, rnd() % 9);
		for (i = 0; i < 5; i++)
			for (j = 0; j < 12; j++)
				tab0[i][j] = i < 3 ? (rnd() % 4 ? rnd() % 2 : rnd()) : rnd() % 3;
		t32_data_d9008 = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		t32_data_d9014 = rnd() % 6;
		r_cfu = rnd() % 8 ? 0 : 1 + rnd() % 20;
		r_ctu = rnd() % 8 ? 0 : 1 + rnd() % 20;
		for (i = 0; i < 2; i++)
			r_ev[i] = rnd() % 3 == 0 ? 0 : rnd() % 4 ? (u32)-515 : rnd();
		km_fail = rnd() % 4 ? 999 : rnd() % 10;
		q_reset = rnd() % 2 || t32_g_rd32(img + O_UBUF, 0) > 50 || ub[0] > 50;	/* else the slots run into the count word */
		if (rnd() % 20 == 0)
			chw = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		if (rnd() % 20 == 0)
			arg = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			for (i = 0; i < 5; i++)
				memcpy(tabs[i], tab0[i], tlen[i] * 4);
			EV[side].n = 0;
			nev = nkm = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_frame_channel_reqbufs(&os, T32O_HOST(chw), arg);
				memcpy(save1, img, IMGSZ);
				for (i = 0; i < 5; i++)
					memcpy(tab1[i], tabs[i], tlen[i] * 4);
			} else {
				rr = (u32)t32o_call(R(frame_channel_reqbufs), chw, arg, 0, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		for (i = 0; i < 5; i++)
			if (memcmp(tabs[i], tab1[i], tlen[i] * 4))
				return fail("module tables", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		hit[ro == 0 ? 0 : ro == (u32)-22 ? 1 : ro == (u32)-12 ? 2 : 3]++;
		big += nkm > 60;
	}
	printf("t32_fchrq_oracle: %u cases, 0 mismatches; 0 / -22 / -12 / other: %u %u %u %u; > 60 allocations: %u\n",
	       n, hit[0], hit[1], hit[2], hit[3], big);
	return 0;
}
