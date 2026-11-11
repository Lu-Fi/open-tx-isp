// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our frame channel buffer-done event
 * (driver/t32/tx_isp_t32_fch_event.h) against the unmodified vendor
 * frame_chan_event (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Random channels with queued lists of 0..7 buffers (states, formats,
 * addresses, the event's buffer at any place or missing), done lists,
 * pad / event / command errors, modes, module flags d9004 / d9008 / d9014
 * / d9874, the f3848 callback, TTFF counters near their limits and the OS
 * timer run through ours, are restored and run through the vendor;
 * compared: every byte, the module tables, the return value and the
 * ordered hook log (spin locks with flags, dump, raw clock, wake-up,
 * complete / up, sprintf with all arguments, encoder and callback,
 * isp_printf with all arguments, %s by its text).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fch_event.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
char t32_data_be4c0[] = "frame_channel_buffer_done";
char t32_data_c66f8[] = "error: %s, %d buf == NULL\n";
char t32_data_c6714[] = "mscaler: index = %d, buffer idx = %d\n";
char t32_data_c673c[] = "buflist:0x%08lx\n";
char t32_data_c6750[] = "buf get:0x%08x isp_index = %d\n";
char t32_data_c6770[] = "Chn:%d Buf:%d Write Done TS:%lld";
char t32_data_c6794[] = "TTFF %s:%d %s:%d\n";
char t32_data_c67a8[] = "error: %s, %d vb = %p, buf = 0x%lx, state = %d chan %d\n";
char t32_data_c67e0[] = "vb is null, buf get:0x%08x lastaddr = 0x%08x idx=%d ch = %d isp_index = %d chan %d\n";
u32 t32_data_d9004, t32_data_d9008, t32_data_d9014, t32_data_d9874, t32_data_f3848;
u32 t32_data_f3774[12], t32_data_d9a54[12], t32_data_d9a84[12], t32_data_d9bd4[12];

u64 frame_chan_event(u32, u32, u32, u32);

static u32 rng = 0x6d2b79f5u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 96
static struct { unsigned int n; u32 e[MAXE][8]; } EV[2];
static int side;
static u32 nl, lk_fl[4], ts_s[8], ts_n[8], nts;
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

/* both sides */
static void l_common(u32 l, u32 *fl) { ev(1, l, 0, 0); *fl = lk_fl[nl++ % 4]; }
static void m_common(u32 *ts) { ev(3, 0, 0, 0);	/* the stack address differs */ ts[0] = ts_s[nts % 8]; ts[1] = ts_n[nts % 8]; nts++; }
static void s_common(char *buf, const char *fmt, u32 chn, u32 idx, u32 lo, u32 hi)
{
	u32 *e = ev(7, hstr(fmt), chn, idx);

	e[4] = lo; e[5] = hi;
	snprintf(buf, 64, fmt, (int)chn, (int)idx, (long long)((u64)hi << 32 | lo));
}
static void cb_frame(u32 chn, u32 idx) { ev(9, chn, idx, 0); }

/* ours */
static void h_lock(u8 *l, u32 *fl) { l_common(T32O_PTR(l), fl); }
static void h_unlock(u8 *l, u32 fl) { ev(2, T32O_PTR(l), fl, 0); }
static void h_dump(void) { ev(10, 0, 0, 0); }
static void h_mono(u32 ts[2]) { m_common(ts); }
static void h_wake(u8 *w) { ev(4, T32O_PTR(w), 0, 0); }
static void h_complete(u8 *c) { ev(5, T32O_PTR(c), 0, 0); }
static void h_up(u8 *s) { ev(6, T32O_PTR(s), 0, 0); }
static void h_sprintf(char *buf, const char *fmt, u32 chn, u32 idx, long long ts) { s_common(buf, fmt, chn, idx, (u32)ts, (u32)((u64)ts >> 32)); }
static void h_send(u32 chn) { ev(8, chn, 0, 0); }
static const struct t32_fchev_os os = { h_lock, h_unlock, h_dump, h_mono, h_wake, h_complete, h_up, h_sprintf, h_send,
					(const u8 *)(uintptr_t)0xb0000000u, &t32_data_d9004, &t32_data_d9008, &t32_data_d9014,
					&t32_data_d9874, &t32_data_f3848, t32_data_f3774, t32_data_d9a54, t32_data_d9a84,
					t32_data_d9bd4 };

/* vendor */
u64 __private_spin_lock_irqsave(u32 l, u32 fl, u32 a2, u32 a3)
{
	u32 v;

	(void)a2; (void)a3;
	l_common(l, &v);
	t32_g_wr32(T32O_HOST(fl), 0, v);
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 l, u32 fl, u32 a2, u32 a3) { (void)a2; (void)a3; ev(2, l, fl, 0); return 0; }
u64 dump_msca_regs(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; ev(10, 0, 0, 0); return 0; }
u64 private_getrawmonotonic(u32 ts, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; m_common(T32O_HOST(ts)); return 0; }
u64 private_wake_up(u32 w, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(4, w, 0, 0); return 0; }
u64 t32o_fe_complete(u32 c, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(5, c, 0, 0); return 0; }
u64 up(u32 s, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(6, s, 0, 0); return 0; }
u64 t32o_fe_sprintf(u32 buf, u32 fmt, u32 chn, u32 idx, u32 lo, u32 hi)
{
	s_common(T32O_HOST(buf), T32O_HOST(fmt), chn, idx, lo, hi);
	return 0;
}
u64 VPU_Encoder_SendFrame(u32 chn, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(8, chn, 0, 0); return 0; }

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
#define O_MOD	0x0010u
#define O_EV	0x0200u
#define O_CH	0x0400u		/* 3 x 816 */
#define O_VB	0x1000u		/* 10 x 0x100 */
#define NVB	10
#define IMGSZ	0x1a00u
static u8 *img, save0[IMGSZ], save1[IMGSZ];
static u32 tab0[4][12], tab1[4][12];
static u32 *const tabs[4] = { t32_data_f3774, t32_data_d9a54, t32_data_d9a84, t32_data_d9bd4 };

static int fail(const char *what, unsigned int c)
{
	unsigned int i, k, s;

	printf("t32_fchev_oracle: FAIL case %u: %s\n", c, what);
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

/* a circular list at head through the nodes at offset off of the buffers in ord[0..n) */
static void mklist(u8 *head, const unsigned int *ord, unsigned int n, u32 off)
{
	u32 prev = T32O_PTR(head), node;
	unsigned int i;

	for (i = 0; i < n; i++) {
		node = T32O_PTR(img + O_VB + ord[i] * 0x100 + off);
		t32_g_wr32(T32O_HOST(prev), 0, node);
		t32_g_wr32(T32O_HOST(node), 4, prev);
		prev = node;
	}
	t32_g_wr32(T32O_HOST(prev), 0, T32O_PTR(head));
	t32_g_wr32(head, 4, prev);
}

int main(void)
{
	unsigned int c, i, j, n = 60000, hit[4] = { 0 }, ord[NVB], nq, nd;
	u8 *cpm;

	cpm = mmap((void *)(uintptr_t)0xb0002000u, 4096, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (cpm != (void *)(uintptr_t)0xb0002000u) {
		printf("t32_fchev_oracle: SKIP (cannot map the OST page at 0xb0002000)\n");
		return 0;
	}
	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u32 padw = T32O_PTR(img + O_PAD), cmd = T32_FE_EVENT, evw = T32O_PTR(img + O_EV), ro = 0, rr = 0;
		u8 *ch = img + O_CH + (c % 3) * T32_FE_CH_BYTES;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		t32_g_wr32(img + O_PAD, 0, T32O_PTR(img + O_MOD));
		img[O_PAD + 4] = (u8)(c % 3);
		t32_g_wr32(img + O_MOD, T32_FE_CHANNELS, T32O_PTR(img + O_CH));
		for (i = 0; i < NVB; i++)
			ord[i] = i;
		for (i = NVB - 1; i > 0; i--) {
			j = rnd() % (i + 1);
			nq = ord[i]; ord[i] = ord[j]; ord[j] = nq;
		}
		nq = rnd() % 8;
		nd = rnd() % (NVB - nq + 1);
		if (nd > 2)
			nd = 2;
		mklist(ch + T32_FE_QLIST, ord, nq, T32_FE_VB_QNODE);
		mklist(ch + T32_FE_DLIST, ord + nq, nd, T32_FE_VB_DNODE);
		for (i = 0; i < NVB; i++) {
			u8 *vb = img + O_VB + i * 0x100;

			t32_g_wr32(vb, 52, 0x10000000u + i * 0x1000 + rnd() % 4);
			t32_g_wr32(vb, T32_FE_VB_STATE, rnd() % 6 ? 3 : rnd() % 2 ? 4 : rnd());
			t32_g_wr32(vb, 92, rnd() % 3 == 0 ? 0 : rnd() % 2 ? 4 : rnd() % 3 ? rnd() % 8 : rnd());
		}
		t32_g_wr32(img + O_EV, 8, nq && rnd() % 5 ? t32_g_rd32(img + O_VB + ord[rnd() % nq] * 0x100, 52) :
			   rnd() % 2 ? 0x10000000u + rnd() % NVB * 0x1000 + 3 : rnd());
		t32_g_wr32(ch, T32_FE_INDEX, rnd() % 12);
		t32_g_wr32(ch, T32_FE_MODE, rnd() % 3 ? rnd() % 2 : rnd() % 2 ? 2 : rnd());
		for (i = 0; i < 12; i++) {
			tab0[0][i] = rnd() % 4;
			tab0[1][i] = rnd() % 5;
			tab0[2][i] = rnd() % 4 ? 9 + rnd() % 4 : rnd() % 2 ? rnd() : 0x80000000u + rnd() % 4;
			tab0[3][i] = rnd();
		}
		t32_data_d9004 = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		t32_data_d9008 = rnd() % 4 ? (rnd() % 2 ? 1 : rnd()) : 0;
		t32_data_d9014 = rnd() % 6;
		t32_data_d9874 = rnd() % 2 ? 1 : rnd() % 2 ? 0 : rnd();
		t32_data_f3848 = rnd() % 2 ? T32O_PTR(cb_frame) : 0;
		t32_g_wr32(cpm, 0x78, rnd());
		for (i = 0; i < 8; i++) {
			ts_s[i] = rnd() % 2 ? rnd() % 100000 : rnd();
			ts_n[i] = rnd() % 2 ? rnd() % 1000000000u : rnd();
		}
		for (i = 0; i < 4; i++)
			lk_fl[i] = rnd();
		if (rnd() % 30 == 0)
			padw = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		if (rnd() % 30 == 0)
			cmd = rnd() % 2 ? T32_FE_EVENT + 1 : rnd();
		if (rnd() % 30 == 0)
			evw = 0;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			for (i = 0; i < 4; i++)
				memcpy(tabs[i], tab0[i], 48);
			EV[side].n = 0;
			nl = 0;
			nts = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_frame_chan_event(&os, T32O_HOST(padw), cmd, T32O_HOST(evw));
				memcpy(save1, img, IMGSZ);
				for (i = 0; i < 4; i++)
					memcpy(tab1[i], tabs[i], 48);
			} else {
				rr = (u32)t32o_call(R(frame_chan_event), padw, cmd, evw, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		for (i = 0; i < 4; i++)
			if (memcmp(tabs[i], tab1[i], 48))
				return fail("module tables", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		if (EV[0].n > MAXE)
			return fail("hook log too long", c);
		hit[ro ? 1 : EV[0].n == 0 ? 2 : t32_g_rd32(img + O_VB, T32_FE_VB_STATE) == 4 ? 0 : 3]++;
	}
	printf("t32_fchev_oracle: %u cases, 0 mismatches; -22 / no event / with buffer 0 done / other: %u %u %u %u\n",
	       n, hit[1], hit[2], hit[0], hit[3]);
	return 0;
}
