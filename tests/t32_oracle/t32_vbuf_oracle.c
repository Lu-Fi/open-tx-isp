// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the frame-channel queue helpers of driver/t32/tx_isp_t32_vbuf.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc): check_state, find_new_buffer,
 * __enqueue_in_driver, __fill_tisp_buffer, __vb2_queue_free,
 * __vb2_queue_cancel, __frame_channel_vb2_streamoff, find_subdev_link_pad.
 *
 * Both sides run on the same arena (64 KiB of queue / buffer / channel /
 * subdev images, restored between the runs, so pointers are equal) and the
 * same globals; compared: every byte of the arena and the globals, the
 * kernel calls in order (arguments, format text, %s strings, lock flags),
 * return values.  Inputs: random images with list heads empty / not,
 * streaming bit, buffer states 0..6, memory type 4 / other, copy failures,
 * event results 0 / -515 / other, direct mode 0..5 with index 0..11, pad
 * descriptors (name match or not, type 0..3, index around the counts).
 * Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_vbuf.h"

u64 check_state(u32, u32, u32, u32);
u64 find_new_buffer(u32, u32, u32, u32);
u64 __enqueue_in_driver(u32, u32, u32, u32);
u64 __fill_tisp_buffer(u32, u32, u32, u32);
u64 __vb2_queue_free(u32, u32, u32, u32);
u64 __vb2_queue_cancel(u32, u32, u32, u32);
u64 __frame_channel_vb2_streamoff(u32, u32, u32, u32);
u64 find_subdev_link_pad(u32, u32, u32, u32);

#define CALL(f, a, b) t32o_call((t32o_fn)(void (*)(void))(f), a, b, 0, 0, 0)
enum { F_EVENT = 1, F_PRINTF, F_KFREE, F_LOCK, F_UNLOCK, F_WAKE, F_CTU };

u8 t32_data_d96b0[0x200] __attribute__((aligned(4)));
u32 t32_data_d9014, t32_data_d9d04[4];
char t32_data_c5ee4[] = "Failed to qbuf to driver; chan%d!\n";	/* data.c strings */
char t32_data_c5fb0[] = "[ %s:%d ] copy error!!!\n";
char t32_data_be544[] = "__fill_tisp_buffer";
char t32_data_c62c0[] = "streamon: invalid stream type\n";
char t32_data_c6388[] = "streamoff: not streaming\n";
char t32_data_bea84[] = "find_subdev_link_pad";
char t32_data_c6abc[] = "[ %s:%d ] Can't find the matched pad!\n";

static u32 r_event, r_flags, r_ctu;
static struct t32o8_log *cur = &t32o8_v;

u64 tx_isp_send_event_to_remote(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	t32o8_rec(cur, F_EVENT, a0, a1, a2, 0);
	return r_event;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 w[2] = { a2, a3 };

	t32o8_rec_fmt(cur, F_PRINTF, a0, T32O_HOST(a1), w, 2);
	return 0;
}
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_KFREE, a0, 0, 0, 0); return 0; }
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o8_rec(cur, F_LOCK, a0, 0, 0, 0);
	*(u32 *)T32O_HOST(a1) = r_flags;
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o8_rec(cur, F_UNLOCK, a0, a1, *(u32 *)T32O_HOST(a0), 0);	/* + the lock word at unlock time */
	return 0;
}
u64 private_wake_up_all(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_WAKE, a0, 0, 0, 0); return 0; }
u64 private_copy_to_user(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	t32o8_rec(cur, F_CTU, a0, a1, a2, *(u32 *)T32O_HOST(a1));
	return r_ctu;
}

/* ours */
static int o_event(u32 sd, u32 ev, void *arg) { return (int)tx_isp_send_event_to_remote(sd, ev, T32O_PTR(arg), 0); }
static int o_printf(int level, const char *fmt, ...)
{
	u32 w[4];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 4);
	va_end(ap);
	t32o8_rec_fmt(cur, F_PRINTF, (u32)level, fmt, w, n);
	return 0;
}
static void o_kfree(void *p) { private_kfree(T32O_PTR(p), 0, 0, 0); }
static void o_lock(void *l, u32 *f)
{
	static u32 tmp;	/* below 4 GiB for the stub */

	__private_spin_lock_irqsave(T32O_PTR(l), T32O_PTR(&tmp), 0, 0);
	*f = tmp;
}
static void o_unlock(void *l, u32 f) { private_spin_unlock_irqrestore(T32O_PTR(l), f, 0, 0); }
static void o_wake(void *wq) { private_wake_up_all(T32O_PTR(wq), 0, 0, 0); }
static unsigned long o_ctu(void *to, const void *from, unsigned long n)
{
	return private_copy_to_user(T32O_PTR(to), T32O_PTR(from), (u32)n, 0);
}
static const struct t32_vbuf_ops *ops_p;
static void o_cancel(u8 *q) { t32_vb2_queue_cancel(ops_p, q); }
static const struct t32_vbuf_ops ops = { o_event, o_printf, o_kfree, o_lock, o_unlock, o_wake, o_ctu, o_cancel };

static u32 rs = 0x7b0f7b0fu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define ARENA 0x10000
static u8 *arena, a_init[ARENA], a_vend[ARENA], g_init[0x200 + 24], g_vend[0x200 + 24];
static void g_save(u8 *g)
{
	memcpy(g, t32_data_d96b0, 0x200);
	memcpy(g + 0x200, &t32_data_d9014, 4);
	memcpy(g + 0x204, t32_data_d9d04, 16);
}
static void g_load(const u8 *g)
{
	memcpy(t32_data_d96b0, g, 0x200);
	memcpy(&t32_data_d9014, g + 0x200, 4);
	memcpy(t32_data_d9d04, g + 0x204, 16);
}

/* arena layout */
#define Q_CH	0x0000		/* frame channel (queue at +44), 1 KiB */
#define Q_VB	0x0800		/* 12 buffers of 256 B */
#define Q_CHAN2	0x1800		/* the buffers' channel object */
#define Q_OUT	0x1c00		/* user copy of a buffer */
#define Q_DEV	0x2000		/* media device (+84 subdevs) */
#define Q_SD	0x2400		/* 4 subdevs of 512 B */
#define Q_NAME	0x3400		/* names */
#define Q_PADS	0x3800		/* pad arrays */

static void build(void)
{
	u8 *q = arena + Q_CH + 44, *dev = arena + Q_DEV;
	u32 i, nb = rnd() % 13;
	static const char *names[4] = { "isp-w00", "isp-m0", "vic", "csi" };

	for (i = 0; i < ARENA; i++)
		arena[i] = (u8)rnd();
	*t32_vb_w(q, 488) = (rnd() % 5) ? nb : rnd();
	if (*t32_vb_w(q, 488) > 12)
		*t32_vb_w(q, 488) = nb;
	for (i = 0; i < 12; i++) {
		u8 *vb = arena + Q_VB + 256 * i;

		*t32_vb_w(q, 232 + 4 * i) = T32O_PTR(vb);
		*t32_vb_w(vb, 96) = T32O_PTR(arena + Q_CHAN2);
		*t32_vb_w(vb, 100) = rnd() % 7;
		*t32_vb_w(vb, 92) = (rnd() & 1) ? 4 : rnd() % 6;
	}
	if (rnd() & 1)
		*t32_vb_w(q, 504) = T32O_PTR(q + 504);
	*t32_vb_w(arena + Q_CH, 44) = rnd() % 3;
	*t32_vb_w(arena + Q_CH, 720) = (rnd() & 1) ? rnd() % 12 : rnd();
	for (i = 0; i < 4; i++)
		strcpy((char *)arena + Q_NAME + 32 * i, names[i]);
	for (i = 0; i < 16; i++)
		*t32_vb_w(dev, 84 + 4 * i) = (rnd() % 3) ? 0 : T32O_PTR(arena + Q_SD + 512 * (rnd() % 4));
	for (i = 0; i < 4; i++) {
		u8 *sd = arena + Q_SD + 512 * i;

		*t32_vb_w(sd, 8) = T32O_PTR(arena + Q_NAME + 32 * ((i + (rnd() & 1)) % 4));
		*(u16 *)(sd + 268) = (u16)(rnd() % 6);
		*(u16 *)(sd + 270) = (u16)(rnd() % 6);
		*t32_vb_w(sd, 272) = T32O_PTR(arena + Q_PADS);
		*t32_vb_w(sd, 276) = T32O_PTR(arena + Q_PADS + 0x200);
	}
	/* pad descriptor at Q_OUT + 0x200 */
	*t32_vb_w(arena + Q_OUT + 0x200, 0) = T32O_PTR(arena + Q_NAME + 32 * (rnd() % 4));
	arena[Q_OUT + 0x204] = (u8)(rnd() % 4);
	arena[Q_OUT + 0x205] = (u8)(rnd() % 7);
	for (i = 0; i < 0x200; i++)
		t32_data_d96b0[i] = (rnd() % 3) ? 1 : 0;
	if (rnd() & 1) {	/* all slots in use but one (or none) */
		u32 k = rnd() % 21;

		for (i = 0; i < 20; i++)
			t32_data_d96b0[13 + 20 * i] = i != k;
	}
	t32_data_d9014 = rnd() % 6;
	for (i = 0; i < 4; i++)
		t32_data_d9d04[i] = rnd();
}

static u32 run(u32 which, int vendor)
{
	u8 *ch = arena + Q_CH, *q = ch + 44, *vb = arena + Q_VB + 256 * (rnd() % 12);
	u32 a = rnd(), r = 0;

	if (vendor) {
		cur = &t32o8_v;
		switch (which) {
		case 0: return (u32)CALL(check_state, (a & 7) ? T32O_PTR(q) : 0, 0);
		case 1: return (u32)CALL(find_new_buffer, 0, 0);
		case 2: return (u32)CALL(__enqueue_in_driver, T32O_PTR(vb), 0);
		case 3: CALL(__fill_tisp_buffer, T32O_PTR(vb), T32O_PTR(arena + Q_OUT)); return 0;
		case 4: return (u32)CALL(__vb2_queue_free, T32O_PTR(q), *t32_vb_w(q, 488) ? a % (*t32_vb_w(q, 488) + 1) : 0);
		case 5: CALL(__vb2_queue_cancel, (a & 7) ? T32O_PTR(q) : (a & 8) ? 0xfffff001u + (a >> 4) % 0xfff : 0, 0); return 0;
		case 6: return (u32)CALL(__frame_channel_vb2_streamoff, T32O_PTR(ch), (a & 3) ? *t32_vb_w(ch, 44) : a);
		default: return (u32)CALL(find_subdev_link_pad, T32O_PTR(arena + Q_DEV), T32O_PTR(arena + Q_OUT + 0x200));
		}
	}
	cur = &t32o8_o;
	switch (which) {
	case 0: return (u32)t32_check_state((a & 7) ? q : NULL);
	case 1: return T32O_PTR(t32_find_new_buffer(t32_data_d96b0 + 12));
	case 2: return (u32)t32_enqueue_in_driver(&ops, vb);
	case 3: t32_fill_tisp_buffer(&ops, vb, arena + Q_OUT); return 0;
	case 4: return (u32)t32_vb2_queue_free(&ops, q, *t32_vb_w(q, 488) ? a % (*t32_vb_w(q, 488) + 1) : 0);
	case 5:
		t32_vb2_queue_cancel(&ops, (a & 7) ? q : (a & 8) ? T32O_HOST(0xfffff001u + (a >> 4) % 0xfff) : NULL);
		return 0;
	case 6: return (u32)t32_frame_channel_streamoff(&ops, ch, (a & 3) ? *t32_vb_w(ch, 44) : a, t32_data_d9014,
							t32_data_d9d04);
	default: r = T32O_PTR(t32_find_subdev_link_pad(&ops, arena + Q_DEV, arena + Q_OUT + 0x200)); return r;
	}
}

int main(void)
{
	unsigned int c, failed = 0, compared = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 24000;

	ops_p = &ops;
	t32o_reset();
	arena = t32o_alloc(ARENA);
	for (c = 0; c < n; c++) {
		u32 which = c % 8, vr, orr, saved;
		int bad;

		t32o8_clear();
		r_event = (rnd() % 3) ? 0 : (rnd() & 1) ? (u32)-515 : rnd();
		r_flags = rnd();
		r_ctu = (rnd() % 4) ? 0 : 1 + rnd() % 4;
		build();
		memcpy(a_init, arena, ARENA);
		g_save(g_init);
		saved = rs;	/* both runs draw the same call arguments */
		vr = run(which, 1);
		memcpy(a_vend, arena, ARENA);
		g_save(g_vend);
		memcpy(arena, a_init, ARENA);
		g_load(g_init);
		rs = saved;
		orr = run(which, 0);
		bad = t32o8_cmp("vbuf", failed < 8) || vr != orr || memcmp(a_vend, arena, ARENA);
		g_save(g_init);
		bad |= memcmp(g_init, g_vend, sizeof(g_vend)) != 0;
		compared++;
		if (bad && failed++ < 8)
			printf("MISMATCH case %u: ret %08x / %08x arena %d\n", which, vr, orr, memcmp(a_vend, arena, ARENA) != 0);
	}
	printf("t32_vbuf_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
