// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our IVDC pad event handler
 * (driver/t32/tx_isp_t32_ivdc_pad.h) against the unmodified vendor
 * ivdc_pad_event_handle (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Random pads (index 0..4, enabled or not), events 0x3000000..0x3000009
 * and others, formats, module data (one-shot flags, modes -1..5, line
 * counts, strides, stream count, IVDC memory incl. NULL / 0x80000000) and
 * scripted remote results (0, -515, others) run through ours, are restored
 * and run through the vendor; compared: every byte (pads, subdev, private,
 * both register blocks, argument), the module data, the return value and
 * the ordered hook log (remote events with pad / event / argument, IRQ
 * enable / disable, mutex, isp_printf with all arguments).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ivdc_pad.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
u32 t32_data_be370[8] = { 0xf184, 0xf1a0, 0xf5c4, 0xf6ec, 0xf874, 0xf86c, 0xf8a8, 0xfb98 };	/* the vendor jump table */
char t32_data_be424[] = "ivdc_frame_channel_reqbufs";
char t32_data_c5c7c[] = "width = %d, height = %d\n";
char t32_data_c5c98[] = "\x1b[31m ivdc_mem_stride=%d, width=%d, not support. \x1b[0m\n";
char t32_data_c5cd0[] = "ivdc mem paddr is NULL\n";
char t32_data_c5ce8[] = "dma alloc frame buffer Failed\n";
char t32_data_c5d08[] = "[%s] s%d stride:%d, ivdc_mem_line=%d\n";
char t32_data_c5d30[] = "ivdc_paddr = 0x%08x\n";
u32 t32_data_cf2dc, t32_data_cf2e0, t32_data_cf2e4, t32_data_ce230, t32_data_d900c, t32_data_d9010;
u32 t32_data_d9014, t32_data_d9898, t32_data_d98b4, t32_data_d98b8;
static u32 *const dat[10] = { &t32_data_cf2dc, &t32_data_cf2e0, &t32_data_cf2e4, &t32_data_ce230, &t32_data_d900c,
			      &t32_data_d9010, &t32_data_d9014, &t32_data_d9898, &t32_data_d98b4, &t32_data_d98b8 };

u64 ivdc_pad_event_handle(u32, u32, u32, u32);

static u32 rng = 0x6d2b79f5u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 16
static struct { unsigned int n; u32 e[MAXE][8]; } EV[2];
static int side;
static u32 *ev(u32 a, u32 b, u32 c, u32 d)
{
	static u32 dummy[8];
	unsigned int n = EV[side].n++;

	if (n >= MAXE)
		return dummy;
	EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	return EV[side].e[n];
}
static u32 hash(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}
static u32 hstr(const char *s) { return hash((const u8 *)s, (u32)strlen(s)); }

static u32 r_send;
/* both sides */
static u32 send_common(u32 pad, u32 event, u32 arg) { ev(1, pad, event, arg); return r_send; }
/* ours */
static int h_send(u8 *pad, u32 event, u8 *arg) { return (int)send_common(T32O_PTR(pad), event, T32O_PTR(arg)); }
static void h_en(u8 *sd) { ev(2, T32O_PTR(sd), 0, 0); }
static void h_dis(u8 *sd) { ev(3, T32O_PTR(sd), 0, 0); }
static void h_lock(u8 *m) { ev(4, T32O_PTR(m), 0, 0); }
static void h_unlock(u8 *m) { ev(5, T32O_PTR(m), 0, 0); }
static const struct t32_ivdcp_os os = { h_send, h_en, h_dis, h_lock, h_unlock, &t32_data_cf2dc, &t32_data_cf2e0,
					&t32_data_cf2e4, &t32_data_ce230, &t32_data_d900c, &t32_data_d9010, &t32_data_d9014,
					&t32_data_d9898, &t32_data_d98b4, &t32_data_d98b8 };
/* vendor */
u64 tx_isp_send_event_to_remote(u32 pad, u32 event, u32 arg, u32 a3) { (void)a3; return (u64)send_common(pad, event, arg); }
u64 ivdc_enable_irq(u32 sd, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(2, sd, 0, 0); return 0; }
u64 ivdc_disable_irq(u32 sd, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(3, sd, 0, 0); return 0; }
u64 t32o_mutex_lock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(4, m, 0, 0); return 0; }
u64 t32o_mutex_unlock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(5, m, 0, 0); return 0; }

/* both sides: level, format text, then each conversion (%s by its text) */
int isp_printf(unsigned int level, const char *fmt, ...)
{
	unsigned int k = 3;
	const char *f;
	va_list ap;
	u32 *e = ev(11, level, hstr(fmt), 0);

	va_start(ap, fmt);
	for (f = fmt; *f && k < 8; f++) {
		if (*f != '%')
			continue;
		while (f[1] && !strchr("sdxucp", f[1]))
			f++;
		f++;
		if (*f == 's') {	/* ours may pass a host stack buffer, the vendor passes 32-bit words */
			unsigned long p = va_arg(ap, unsigned long);

			e[k++] = hstr((const char *)(uintptr_t)(side ? (u32)p : p));
		} else if (*f == 'p')
			e[k++] = (u32)va_arg(ap, unsigned long);
		else
			e[k++] = va_arg(ap, unsigned int);
	}
	va_end(ap);
	return 0;
}

#define O_PAD	0x0000u
#define O_SD	0x0040u
#define O_PRIV	0x0180u
#define O_FC	0x0380u
#define O_ARG	0x0580u
#define O_PADS	0x0600u		/* 5 x 36 */
#define O_REGS	0x0700u		/* 0x700 */
#define O_FREGS	0x0e00u
#define IMGSZ	0x0e80u
static u8 *img, save0[IMGSZ], save1[IMGSZ];
static u32 dat0[10], dat1[10];

static int fail(const char *what, unsigned int c)
{
	unsigned int i, k, s;

	printf("t32_ivdcp_oracle: FAIL case %u: %s\n", c, what);
	if (getenv("T32O_VERBOSE"))
		for (s = 0; s < 2; s++)
			for (i = 0; i < EV[s].n && i < MAXE; i++) {
				printf("%s %2u:", s ? "ref" : "our", i);
				for (k = 0; k < 8; k++)
					printf(" %08x", EV[s].e[i][k]);
				printf("\n");
			}
	return 1;
}

static u32 pick(const u32 *v, unsigned int n) { return v[rnd() % n]; }

int main(void)
{
	static const u32 modes[] = { 0, 1, 1, 2, 3, 4, 5, (u32)-1 };
	static const u32 sends[] = { 0, 0, (u32)-515, (u32)-22, 1 };
	unsigned int c, i, n = 60000, hit[4] = { 0 };

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u32 padw = T32O_PTR(img + O_PAD), evn, argw = T32O_PTR(img + O_ARG), ro = 0, rr = 0;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		t32_g_wr32(img + O_PAD, 0, T32O_PTR(img + O_SD));
		img[O_PAD + 4] = (u8)(rnd() % 6);
		if (rnd() % 10 == 0)
			img[O_PAD + 5] = 0;
		t32_g_wr32(img + O_PAD, 32, T32O_PTR(img + O_FC));
		t32_g_wr32(img + O_SD, T32_IVP_SD_PADS, T32O_PTR(img + O_PADS));
		t32_g_wr32(img + O_SD, T32_IVP_SD_PRIV, T32O_PTR(img + O_PRIV));
		t32_g_wr32(img + O_PRIV, T32_IVP_REGS, T32O_PTR(img + O_REGS));
		t32_g_wr32(img + O_FC, T32_IVP_REGS, T32O_PTR(img + O_FREGS));
		t32_g_wr32(img + O_ARG, 4, rnd() % 4 ? 16 + rnd() % 4000 : rnd());
		t32_g_wr32(img + O_ARG, 8, rnd() % 4 ? 16 + rnd() % 4000 : rnd());
		t32_g_wr32(img + O_FC, 332, rnd() % 4 ? 16 + rnd() % 4000 : rnd());
		t32_g_wr32(img + O_FC, 336, rnd() % 4 ? 16 + rnd() % 4000 : rnd());
		evn = rnd() % 16 ? T32_IVP_EV_BASE - 1 + rnd() % 10 : rnd();
		t32_data_cf2dc = rnd() % 3 ? rnd() % 2 : rnd();
		t32_data_cf2e0 = rnd() % 3 ? rnd() % 2 : rnd();
		t32_data_cf2e4 = rnd() % 3 ? rnd() % 2 : rnd();
		t32_data_ce230 = rnd() % 4 ? rnd() % 4200 : rnd();
		if (rnd() % 8 == 0)
			t32_data_ce230 = t32_g_rd32(img + O_ARG, 4);
		t32_data_d900c = rnd() % 2 ? 0 : rnd() % 3 ? rnd() % 4000 : rnd();
		t32_data_d9010 = rnd() % 3 == 0 ? 0 : rnd() % 3 ? rnd() % 8000 : rnd();
		t32_data_d9014 = pick(modes, 8);
		t32_data_d9898 = rnd() % 3 ? rnd() % 3 : rnd();
		t32_data_d98b4 = rnd() % 3 ? rnd() % 2 : rnd();
		t32_data_d98b8 = rnd() % 6 == 0 ? 0 : rnd() % 6 == 0 ? 0x80000000u : rnd();
		r_send = rnd() % 8 ? pick(sends, 5) : rnd();
		for (i = 0; i < 10; i++)
			dat0[i] = *dat[i];
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			for (i = 0; i < 10; i++)
				*dat[i] = dat0[i];
			EV[side].n = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_ivdc_pad_event_handle(&os, T32O_HOST(padw), evn, T32O_HOST(argw));
				memcpy(save1, img, IMGSZ);
				for (i = 0; i < 10; i++)
					dat1[i] = *dat[i];
			} else {
				rr = (u32)t32o_call(R(ivdc_pad_event_handle), padw, evn, argw, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		for (i = 0; i < 10; i++)
			if (*dat[i] != dat1[i])
				return fail("module data", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		hit[ro == 0 ? (EV[0].n ? 0 : 1) : ro == (u32)-22 || ro == (u32)-12 ? 2 : 3]++;
	}
	printf("t32_ivdcp_oracle: %u cases, 0 mismatches; 0 with hooks / 0 without / -22 or -12 / other: %u %u %u %u\n",
	       n, hit[0], hit[1], hit[2], hit[3]);
	return 0;
}
