// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our frame channel dequeue (driver/t32/tx_isp_t32_fch_dq.h)
 * against the unmodified vendor frame_channel_vb2_dqbuf (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc).  A random image (channel with a done
 * list of 0..3 buffers and a queued list, ISP core, kernel-side v4l2_buffer)
 * runs through ours, is restored and runs through the vendor; compared:
 * every byte, the return value and the ordered log of the hook calls (user
 * copies with the copied-out bytes, waits with their condition, spin locks
 * with the flags, buffer fill with the v4l2_buffer bytes, isp_printf).  The
 * hooks answer from the same random script on both sides: a wait may add a
 * buffer to the done list, stop streaming or fail.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fch_dq.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
char t32_data_c5fcc[] = "The parameter from user is invalid!\n";
char t32_data_c5f08[] = "Failed to copy from user\n";
char t32_data_c6230[] = "dqbuf: invalid buffer type\n";
char t32_data_c624c[] = "Streaming off, will not wait for buffers\n";
char t32_data_c6278[] = "Sleep was interrupted\n";
char t32_data_c5f98[] = "Failed to copy to user\n";
u32 t32_data_d9014, t32_data_d9950;

u64 frame_channel_vb2_dqbuf(u32, u32, u32, u32);
u64 check_state(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

static u32 rng = 0x1b873593u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 24
static struct { unsigned int n; u32 e[MAXE][4]; } EV[2];
static int side;
static u8 *img;
static u32 r_cfu, r_ctu, ub[24], nw, nl, w_act[8], w_ret[8], lk_fl[8], fl_seed;
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
#define O_VB	0x0400u		/* 4 x 0x100 */
#define O_CORE	0x0800u
#define O_UBUF	0x0a00u
#define IMGSZ	0x0b00u
static u8 save0[IMGSZ], save1[IMGSZ];

static void list_add_tail(u8 *head, u8 *node)
{
	u32 prev = t32_g_rd32(head, 4);

	t32_g_wr32(node, 0, T32O_PTR(head));
	t32_g_wr32(node, 4, prev);
	t32_g_wr32(T32O_HOST(prev), 0, T32O_PTR(node));
	t32_g_wr32(head, 4, T32O_PTR(node));
}

static u32 h_cfu(u8 *dst, u32 user, u32 n) { ev(1, user, n, 0); memcpy(dst, ub, n); return r_cfu; }
static u32 h_ctu(u32 user, const u8 *src, u32 n) { ev(2, user, n, hash(src, n)); return r_ctu; }
static int w_common(u32 wq, u32 cond, u32 arg)
{
	u32 k = nw++;

	ev(3, wq, cond, arg);
	if (k >= 6)
		return -4;
	switch (w_act[k]) {
	case 1:		/* a buffer is done */
		list_add_tail(img + O_CH + T32_DQ_DONE, img + O_VB + 3 * 0x100 + T32_DQ_VB_DNODE);
		break;
	case 2:		/* streaming stopped */
		t32_g_wr32(img + O_CH, T32_DQ_STATE, t32_g_rd32(img + O_CH, T32_DQ_STATE) & ~1u);
		break;
	}
	return (int)w_ret[k];
}
static int h_wait(u8 *wq, u8 *arg) { return w_common(T32O_PTR(wq), 1, T32O_PTR(arg)); }
static void l_common(u32 l, u32 *fl) { ev(4, l, 0, 0); *fl = lk_fl[nl++ % 8]; }
static void h_lock(u8 *l, u32 *fl) { l_common(T32O_PTR(l), fl); }
static void h_unlock(u8 *l, u32 fl) { ev(5, T32O_PTR(l), fl, 0); }
static void f_common(u32 vb, u8 *buf)
{
	u32 i, s = fl_seed;

	ev(6, vb, hash(buf, T32_DQ_BUF_BYTES), 0);
	for (i = 0; i < T32_DQ_BUF_BYTES; i += 4) {
		s = s * 1103515245u + 12345u;
		if (s >> 30 == 0)
			t32_g_wr32(buf, i, s);
	}
}
static void h_fill(u8 *vb, u8 *buf) { f_common(T32O_PTR(vb), buf); }
static const struct t32_fchdq_os os = { h_cfu, h_ctu, h_wait, h_lock, h_unlock, h_fill, &t32_data_d9014, &t32_data_d9950 };

u64 private_copy_from_user(u32 d, u32 u, u32 n, u32 a3) { (void)a3; return h_cfu(T32O_HOST(d), u, n); }
u64 private_copy_to_user(u32 u, u32 s, u32 n, u32 a3) { (void)a3; return h_ctu(u, T32O_HOST(s), n); }
u64 private_wait_event_interruptible(u32 wq, u32 cond, u32 arg, u32 a3)
{
	(void)a3;
	return (u32)w_common(wq, cond == T32O_PTR(check_state), arg);
}
u64 __private_spin_lock_irqsave(u32 l, u32 fl, u32 a2, u32 a3)
{
	u32 v;

	(void)a2; (void)a3;
	l_common(l, &v);
	t32_g_wr32(T32O_HOST(fl), 0, v);
	return rnd();	/* v0 unused by the vendor */
}
u64 private_spin_unlock_irqrestore(u32 l, u32 fl, u32 a2, u32 a3) { (void)a2; (void)a3; ev(5, l, fl, 0); return 0; }
u64 __fill_tisp_buffer(u32 vb, u32 buf, u32 a2, u32 a3) { (void)a2; (void)a3; f_common(vb, T32O_HOST(buf)); return 0; }

int isp_printf(unsigned int level, const char *fmt, ...)
{
	ev(9, level, hash((const u8 *)fmt, (u32)strlen(fmt)), 0);
	return 0;
}

static int fail(const char *what, unsigned int c)
{
	printf("t32_fchdq_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 60000, hit[6] = { 0 }, rq = 0;

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u8 *ch = img + O_CH;
		u32 chw = T32O_PTR(ch), argw = T32O_PTR(img + O_UBUF), ro = 0, rr = 0, nd = rnd() % 4, nq = rnd() % 3;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		t32_g_wr32(ch, T32_DQ_DONE, T32O_PTR(ch + T32_DQ_DONE));
		t32_g_wr32(ch, T32_DQ_DONE + 4, T32O_PTR(ch + T32_DQ_DONE));
		t32_g_wr32(ch, T32_DQ_QLIST, T32O_PTR(ch + T32_DQ_QLIST));
		t32_g_wr32(ch, T32_DQ_QLIST + 4, T32O_PTR(ch + T32_DQ_QLIST));
		for (i = 0; i < nd; i++)
			list_add_tail(ch + T32_DQ_DONE, img + O_VB + i * 0x100 + T32_DQ_VB_DNODE);
		for (i = 0; i < nq; i++)	/* other buffers' queued nodes (the done ones are not queued) */
			list_add_tail(ch + T32_DQ_QLIST, img + O_VB + (3 - i) * 0x100 + T32_DQ_VB_QNODE - 0x40);
		t32_g_wr32(ch, T32_DQ_STATE, rnd() % 6 ? rnd() | 1 : rnd() & ~1u);
		t32_g_wr32(ch, T32_DQ_FRAME, rnd() % 4 ? rnd() % 12 : rnd());
		t32_g_wr32(ch, T32_DQ_KERNEL, rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd());
		for (i = 0; i < 4; i++)
			t32_g_wr32(img + O_VB + i * 0x100, T32_DQ_VB_XFLAGS, rnd() % 3 ? 0 : rnd());
		t32_g_wr32(img + O_CORE, T32_DQ_CORE_LOCK, rnd() % 2 ? 1 : rnd() % 3);
		for (i = 0; i < 24; i++)
			ub[i] = rnd();
		if (rnd() % 10)
			ub[1] = t32_g_rd32(ch, T32_DQ_TYPE);
		if (rnd() % 10)
			t32_g_wr32(img + O_UBUF, T32_DQ_BUF_TYPE, t32_g_rd32(ch, T32_DQ_TYPE));
		for (i = 0; i < 8; i++) {
			u32 a = rnd() % 8;

			w_act[i] = a < 3 ? 0 : a < 6 ? 1 : a < 7 ? 2 : 3;
			w_ret[i] = w_act[i] == 3 ? (rnd() % 2 ? (u32)-4 : (u32)-(1 + rnd() % 600)) : rnd() % 2 ? 0 : (u32)-512;
			if ((int)w_ret[i] > 0)	/* keep the vendor's positive-result UB out */
				w_ret[i] = (u32)-4;
			lk_fl[i] = rnd();
		}
		r_cfu = rnd() % 8 ? 0 : 1 + rnd() % 96;
		r_ctu = rnd() % 8 ? 0 : 1 + rnd() % 96;
		fl_seed = rnd();
		t32_data_d9014 = rnd() % 6;
		t32_data_d9950 = T32O_PTR(img + O_CORE);
		if (rnd() % 20 == 0)
			chw = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		if (rnd() % 20 == 0)
			argw = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			nw = nl = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_frame_channel_vb2_dqbuf(&os, T32O_HOST(chw), argw);
				memcpy(save1, img, IMGSZ);
			} else {
				rr = (u32)t32o_call(R(frame_channel_vb2_dqbuf), chw, argw, 0, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		hit[ro == 0 ? 0 : ro == (u32)-22 ? 1 : ro == (u32)-12 ? 2 : 3]++;
		if (ro == 0 && t32_g_rd32(save1, T32_DQ_QCOUNT) != t32_g_rd32(save0, T32_DQ_QCOUNT))
			rq++;
	}
	printf("t32_fchdq_oracle: %u cases, 0 mismatches; 0 / -22 / -12 / wait error: %u %u %u %u; requeued: %u\n",
	       n, hit[0], hit[1], hit[2], hit[3], rq);
	return 0;
}
