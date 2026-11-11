// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our IVDC interrupt service routine
 * (driver/t32/tx_isp_t32_ivdc_irq.h) against the unmodified vendor
 * ivdc_core_interrupt_service_routine (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc).  Random status words (sparse and dense), channel registers,
 * d989c and counter values run through ours, are restored and run through
 * the vendor; compared: every byte (registers included), the 30 counter
 * words d98bc..d9930, the return value and the ordered hook log (fps /
 * CSC-CR / full-range queries with the channel, the event with pad, cmd
 * and its named fields).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ivdc_irq.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
u32 t32o_ivdci_cnt[T32_IVDCI_COUNTERS], t32_data_d989c;
__asm__(".globl t32_data_d98bc\n.set t32_data_d98bc, t32o_ivdci_cnt + 0");
__asm__(".globl t32_data_d98c0\n.set t32_data_d98c0, t32o_ivdci_cnt + 4");
__asm__(".globl t32_data_d98c4\n.set t32_data_d98c4, t32o_ivdci_cnt + 8");
__asm__(".globl t32_data_d98c8\n.set t32_data_d98c8, t32o_ivdci_cnt + 12");
__asm__(".globl t32_data_d98cc\n.set t32_data_d98cc, t32o_ivdci_cnt + 16");
__asm__(".globl t32_data_d98d0\n.set t32_data_d98d0, t32o_ivdci_cnt + 20");
__asm__(".globl t32_data_d98d4\n.set t32_data_d98d4, t32o_ivdci_cnt + 24");
__asm__(".globl t32_data_d98d8\n.set t32_data_d98d8, t32o_ivdci_cnt + 28");
__asm__(".globl t32_data_d98dc\n.set t32_data_d98dc, t32o_ivdci_cnt + 32");
__asm__(".globl t32_data_d98e0\n.set t32_data_d98e0, t32o_ivdci_cnt + 36");
__asm__(".globl t32_data_d98e4\n.set t32_data_d98e4, t32o_ivdci_cnt + 40");
__asm__(".globl t32_data_d98e8\n.set t32_data_d98e8, t32o_ivdci_cnt + 44");
__asm__(".globl t32_data_d98ec\n.set t32_data_d98ec, t32o_ivdci_cnt + 48");
__asm__(".globl t32_data_d98f0\n.set t32_data_d98f0, t32o_ivdci_cnt + 52");
__asm__(".globl t32_data_d98f4\n.set t32_data_d98f4, t32o_ivdci_cnt + 56");
__asm__(".globl t32_data_d98f8\n.set t32_data_d98f8, t32o_ivdci_cnt + 60");
__asm__(".globl t32_data_d98fc\n.set t32_data_d98fc, t32o_ivdci_cnt + 64");
__asm__(".globl t32_data_d9900\n.set t32_data_d9900, t32o_ivdci_cnt + 68");
__asm__(".globl t32_data_d9904\n.set t32_data_d9904, t32o_ivdci_cnt + 72");
__asm__(".globl t32_data_d9908\n.set t32_data_d9908, t32o_ivdci_cnt + 76");
__asm__(".globl t32_data_d990c\n.set t32_data_d990c, t32o_ivdci_cnt + 80");
__asm__(".globl t32_data_d9910\n.set t32_data_d9910, t32o_ivdci_cnt + 84");
__asm__(".globl t32_data_d9914\n.set t32_data_d9914, t32o_ivdci_cnt + 88");
__asm__(".globl t32_data_d9918\n.set t32_data_d9918, t32o_ivdci_cnt + 92");
__asm__(".globl t32_data_d991c\n.set t32_data_d991c, t32o_ivdci_cnt + 96");
__asm__(".globl t32_data_d9920\n.set t32_data_d9920, t32o_ivdci_cnt + 100");
__asm__(".globl t32_data_d9924\n.set t32_data_d9924, t32o_ivdci_cnt + 104");
__asm__(".globl t32_data_d9928\n.set t32_data_d9928, t32o_ivdci_cnt + 108");
__asm__(".globl t32_data_d992c\n.set t32_data_d992c, t32o_ivdci_cnt + 112");
__asm__(".globl t32_data_d9930\n.set t32_data_d9930, t32o_ivdci_cnt + 116");

u64 ivdc_core_interrupt_service_routine(u32, u32, u32, u32);

static u32 rng = 0xc2b2ae35u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 8
static struct { unsigned int n; u32 e[MAXE][8]; } EV[2];
static int side;
static u32 r_fps, r_csc, r_fr, r_ev;
static void ev(u32 a, u32 b, u32 c, u32 d)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	}
}
static void h_fps(u32 ch, u32 *fps) { ev(1, ch, 0, 0); *fps = r_fps; }
static u32 h_csc(u32 ch) { ev(2, ch, 0, 0); return r_csc; }
static u32 h_fr(u32 ch) { ev(3, ch, 0, 0); return r_fr; }
static int h_ev(u32 pad, u32 cmd, u8 *d)
{
	unsigned int n = EV[side].n;

	ev(4, pad, cmd, t32_g_rd32(d, 8));
	if (n < MAXE) {
		EV[side].e[n][4] = t32_g_rd32(d, 12);
		EV[side].e[n][5] = t32_g_rd32(d, 24) ^ t32_g_rd32(d, 28) * 3u;
		EV[side].e[n][6] = t32_data_d989c ? t32_g_rd32(d, 32) : 0;
		EV[side].e[n][7] = t32_g_rd32(d, 48);
	}
	return (int)r_ev;
}
static const struct t32_ivdci_os os = { h_fps, h_csc, h_fr, h_ev };
u64 tisp_get_fps(u32 ch, u32 p, u32 a2, u32 a3) { (void)a2; (void)a3; h_fps(ch, (u32 *)T32O_HOST(p)); return 0; }
u64 lib_tisp_api_get_csccr_output(u32 ch, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return h_csc(ch); }
u64 tisp_csc_get_full_range(u32 ch, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return h_fr(ch); }
u64 tx_isp_send_event_to_remote(u32 pad, u32 cmd, u32 d, u32 a3) { (void)a3; return (u32)h_ev(pad, cmd, T32O_HOST(d)); }

#define O_SD	0x0000u
#define O_PRIV	0x0200u
#define O_REG	0x0400u
#define IMGSZ	0x0500u
static u8 *img, save0[IMGSZ], save1[IMGSZ];
static u32 c0[T32_IVDCI_COUNTERS], c1[T32_IVDCI_COUNTERS];

static int fail(const char *what, unsigned int c)
{
	printf("t32_ivdci_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 40000, done = 0;
	u32 d989c = 0;

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u8 *sd = img + O_SD, *priv = img + O_PRIV, *reg = img + O_REG;
		u32 ro = 0, rr = 0, st;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		st = rnd() % 3 == 0 ? rnd() : rnd() % 2 ? 1u << (rnd() % 32) : rnd() & rnd();
		t32_g_wr32(reg, T32_IVDCI_REG_STATUS, st);
		t32_g_wr32(sd, T32_IVDC_SD_PRIV, T32O_PTR(priv));
		t32_g_wr32(priv, T32_IVDC_PRIV_REGS, T32O_PTR(reg));
		for (i = 0; i < T32_IVDCI_COUNTERS; i++)
			c0[i] = rnd() % 4 ? rnd() % 1000 : rnd();
		d989c = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		r_fps = rnd(); r_csc = rnd(); r_fr = rnd(); r_ev = rnd();
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			memcpy(t32o_ivdci_cnt, c0, sizeof(c0));
			t32_data_d989c = d989c;
			EV[side].n = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_ivdc_core_interrupt_service_routine(&os, t32o_ivdci_cnt, &t32_data_d989c, sd);
				memcpy(save1, img, IMGSZ);
				memcpy(c1, t32o_ivdci_cnt, sizeof(c1));
			} else {
				rr = (u32)t32o_call(R(ivdc_core_interrupt_service_routine), T32O_PTR(sd), 0, 0, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		if (memcmp(t32o_ivdci_cnt, c1, sizeof(c1)))
			return fail("counters", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		done += st >> 17 & 1;
	}
	printf("t32_ivdci_oracle: %u cases, 0 mismatches; frame-done events: %u\n", n, done);
	return 0;
}
