// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: video_input_cmd_set of driver/t32/tx_isp_t32_vicmd.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc, built
 * through fix_op8_vicmd.sh).
 *
 * Both sides run on the same arena (file, seq, device, vi, five sensors with
 * ops / core ops, the 640-byte text table; restored between the runs);
 * compared: every arena byte, the calls in order (kmalloc size / flags, the
 * copied command text, strtoull text and base, mutexes, the sensor op with
 * all 64 request bytes, seq_printf / sprintf / isp_printf with their words
 * and %s texts), return values.  Inputs: commands "r sen_reg<d> <n>",
 * "w sen_reg<d> <n> <n>" with spaces, other digits, 8-character prefixes,
 * junk; missing / ERR_PTR device, vi or sensor 0; missing core ops or op;
 * op results 0 / error; copy failures (access mask, partial copy) with
 * counts around 128.  Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_vicmd.h"

u64 video_input_cmd_set(u32, u32, u32, u32);

enum { F_KMALLOC = 1, F_KFREE, F_CFU, F_STRTOULL, F_LOCK, F_UNLOCK, F_SEQ, F_SPRINTF, F_PRINTF, F_GREG, F_SREG };
char t32_data_d9460[640];
char t32_data_bda84[] = "video_input_cmd_set";	/* data.c strings */
char t32_data_c42f4[] = "don't have active sensor, please set sensor firstly!\n";
char t32_data_c432c[] = "r sen_reg";
char t32_data_c4338[] = "Error: [ %s:%d ]\n";
char t32_data_c434c[] = "isp: sensor reg read 0x%x(0x%x)\n";
char t32_data_c4370[] = "reg:0x%x, value:0x%x, state:read ok";
char t32_data_c4394[] = "w sen_reg";
char t32_data_c43a0[] = "isp: sensor reg write 0x%x(0x%x)\n";
char t32_data_c43c4[] = "reg:0x%x, value:0x%x, state:%s %s";
char t32_data_c43e8[] = "ok";
char t32_data_c43ec[] = "write";
char t32_data_c43f4[] = "reg:0x%x, value:null, state:%s %s";
char t32_data_c4418[] = "failed";

#define ARENA 0x4000
static struct t32o8_log *cur;
static u8 *arena, *kbuf, *ubuf;
static u32 cu_left, r_op, r_val, r_kmfail;
static u32 off(u32 p) { return p - T32O_PTR(arena) < ARENA ? 0x40000000u | (p - T32O_PTR(arena)) : p == T32O_PTR(kbuf) ? 0x20000000u : p - T32O_PTR(t32_data_d9460) < 640 ? 0x10000000u | (p - T32O_PTR(t32_data_d9460)) : p; }

/* the formatter both sides use (%x and %s only, as the vendor's formats) */
static void fmt_out(char *out, const char *fmt, const u32 *w)
{
	int k = 0;

	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			*out++ = *fmt;
			continue;
		}
		fmt++;
		if (*fmt == 'x')
			out += sprintf(out, "%x", w[k++]);
		else if (*fmt == 's')
			out += sprintf(out, "%s", (const char *)T32O_HOST(w[k++]));
	}
	*out = 0;
}
static void do_sprintf(u32 buf, const char *fmt, const u32 *w, int nw)
{
	t32o8_rec_fmt(cur, F_SPRINTF, off(buf), fmt, w, nw);
	fmt_out(T32O_HOST(buf), fmt, w);
}

u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; t32o8_rec(cur, F_KMALLOC, a0, a1, 0, 0); return r_kmfail ? 0 : T32O_PTR(kbuf); }
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_KFREE, off(a0), 0, 0, 0); return 0; }
static u32 do_copy(u32 to, u32 from, u32 n)
{
	struct t32o8_call *c = t32o8_rec(cur, F_CFU, off(to), from, n, 0);
	u32 left = cu_left > n ? n : cu_left;

	memcpy(T32O_HOST(to), T32O_HOST(from), n - left);
	memcpy(c->data, T32O_HOST(from), (n - left) < sizeof(c->data) ? n - left : sizeof(c->data));
	return left;
}
u64 __copy_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return do_copy(a0, a1, a2); }
static u64 do_strtoull(u32 cp, u32 endp, u32 base)
{
	struct t32o8_call *c = t32o8_rec(cur, F_STRTOULL, off(cp), endp ? 1 : 0, base, 0);
	char *e;
	unsigned long long v = strtoull(T32O_HOST(cp), &e, (int)base);

	snprintf(c->data, sizeof(c->data), "%s", (const char *)T32O_HOST(cp));
	if (endp)
		*(u32 *)T32O_HOST(endp) = T32O_PTR(e);
	return v;
}
u64 private_simple_strtoull(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return do_strtoull(a0, a1, a2); }
u64 t32o8_mutex_lock(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_LOCK, off(a0), 0, 0, 0); return 0; }
u64 t32o8_mutex_unlock(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_UNLOCK, off(a0), 0, 0, 0); return 0; }
u64 private_seq_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 w[2] = { a2, a3 };

	t32o8_rec_fmt(cur, F_SEQ, off(a0), T32O_HOST(a1), w, 2);
	return 0;
}
u64 t32o8_sprintf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	u32 w[4] = { a2, a3, a4, a5 };

	do_sprintf(a0, T32O_HOST(a1), w, 4);
	return 0;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	u32 w[5] = { a2, a3, a4, a5, a6 };

	t32o8_rec_fmt(cur, F_PRINTF, a0, T32O_HOST(a1), w, 5);
	return 0;
}
/* the sensor core ops (32-bit function pointers in the arena); pointer-wide arguments: the vendor
 * passes zero-extended u32 words, ours host pointers (its request is on the host stack) */
static u8 *reg_arena_copy;
static u64 sensor_op(u32 id, uintptr_t sd, uintptr_t reg)
{
	struct t32o8_call *c = t32o8_rec(cur, id, off((u32)sd), 0, 0, 0);

	memcpy(c->data, (void *)reg, 64);
	if (id == F_GREG)
		*(u32 *)((u8 *)reg + 56) = r_val;
	(void)reg_arena_copy;
	return r_op;
}
u64 t32o8_g_register(uintptr_t a0, uintptr_t a1) { return sensor_op(F_GREG, a0, a1); }
u64 t32o8_s_register(uintptr_t a0, uintptr_t a1) { return sensor_op(F_SREG, a0, a1); }

/* ours */
static void *o_kmalloc(u32 n, u32 f) { return T32O_HOST(private_kmalloc(n, f, 0, 0)); }
static void o_kfree(void *p) { private_kfree(T32O_PTR(p), 0, 0, 0); }
static unsigned long o_cfu(void *to, const void *from, unsigned long n)
{
	u32 mask = *(u32 *)(t32o8_ti + 24), a = T32O_PTR(from);

	if ((a | (a + (u32)n) | (u32)n) & mask) {
		memset(to, 0, n);
		return n;
	}
	return do_copy(T32O_PTR(to), a, (u32)n);
}
static u64 o_strtoull(const char *cp, char **endp, u32 base)
{
	static u32 e;
	u64 v = do_strtoull(T32O_PTR(cp), endp ? T32O_PTR(&e) : 0, base);

	if (endp)
		*endp = T32O_HOST(e);
	return v;
}
static void o_lock(void *m) { t32o8_mutex_lock(T32O_PTR(m), 0, 0, 0); }
static void o_unlock(void *m) { t32o8_mutex_unlock(T32O_PTR(m), 0, 0, 0); }
static int o_seq(void *seq, const char *fmt, ...)
{
	u32 w[4];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 4);
	va_end(ap);
	t32o8_rec_fmt(cur, F_SEQ, off(T32O_PTR(seq)), fmt, w, n);
	return 0;
}
static int o_sprintf(char *buf, const char *fmt, ...)
{
	u32 w[4] = { 0 };
	va_list ap;

	va_start(ap, fmt);
	t32o8_va_words(fmt, ap, w, 4);
	va_end(ap);
	do_sprintf(T32O_PTR(buf), fmt, w, 4);
	return 0;
}
static int o_printf(int level, const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(cur, F_PRINTF, (u32)level, fmt, w, n);
	return 0;
}
static const struct t32_vicmd_ops ops = { o_kmalloc, o_kfree, o_cfu, o_strtoull, o_lock, o_unlock, o_seq, o_sprintf,
					  o_printf };

static u32 rs = 0xc3d5e7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* arena: +0 file, +0x100 seq, +0x200 device, +0x400 vi, +0x800 + 0x200 * i sensors (i < 5),
 * +0x1400 ops, +0x1500 core ops (two variants); kbuf / ubuf from the pool */
static void build(void)
{
	u8 *file = arena, *seq = arena + 0x100, *dev = arena + 0x200, *vi = arena + 0x400;
	u32 i, k;

	for (k = 0; k < ARENA; k++)
		arena[k] = (u8)rnd();
	*(u32 *)(file + 120) = T32O_PTR(seq);
	*(u32 *)(seq + 72) = (rnd() % 12) ? T32O_PTR(dev) : (rnd() & 1) ? 0 : 0xfffff001u + rnd() % 0xfff;
	*(u32 *)(dev + 284) = (rnd() % 12) ? T32O_PTR(vi) : (rnd() & 1) ? 0 : 0xfffff001u + rnd() % 0xfff;
	for (i = 0; i < 5; i++) {
		u8 *sd = arena + 0x800 + 0x200 * i;

		*(u32 *)(vi + 296 + 4 * i) = T32O_PTR(sd);
		*(u32 *)(sd + 264) = T32O_PTR(arena + 0x1400 + 0x10 * (rnd() % 3));
	}
	if (!(rnd() % 12))
		*(u32 *)(vi + 296) = (rnd() & 1) ? 0 : 0xfffff001u + rnd() % 0xfff;
	*(u32 *)(arena + 0x1400) = T32O_PTR(arena + 0x1500);	/* ops 0: core ops with both */
	*(u32 *)(arena + 0x1410) = T32O_PTR(arena + 0x1540);	/* ops 1: core ops without */
	*(u32 *)(arena + 0x1420) = 0;				/* ops 2: no core ops */
	*(u32 *)(arena + 0x1500 + 16) = T32O_PTR(t32o8_g_register);
	*(u32 *)(arena + 0x1500 + 28) = T32O_PTR(t32o8_s_register);
	*(u32 *)(arena + 0x1540 + 16) = 0;
	*(u32 *)(arena + 0x1540 + 28) = 0;
}

static u32 command(char *s)
{
	static const char *pre[] = { "r sen_reg", "w sen_reg", "r sen_rex", "w sen_rex", "x" };
	int n = sprintf(s, "%s", pre[(rnd() % 6) % 5]);

	n += sprintf(s + n, "%c", (rnd() % 8) ? '0' + rnd() % 5 : ' ' + rnd() % 90);
	n += sprintf(s + n, "%.*s", (int)(rnd() % 3), "   ");
	n += sprintf(s + n, (rnd() & 1) ? "0x%x" : "%u", rnd() % 0x10000);
	n += sprintf(s + n, " %s%x", (rnd() & 1) ? "0x" : "", rnd() % 0x100);
	return (u32)n + ((rnd() & 3) ? 1 : 0);
}

int main(void)
{
	unsigned int c, failed = 0;
	static u8 init[ARENA], vend[ARENA];
	static char tinit[640], tvend[640], kinit[400];

	t32o_reset();
	arena = t32o_alloc(ARENA);
	kbuf = t32o_alloc(400);
	ubuf = t32o_alloc(400);
	for (c = 0; c < 30000; c++) {
		u32 count, vr, orr, k;
		int bad;

		build();
		memset(ubuf, 0, 400);
		count = command((char *)ubuf);
		if (!(rnd() % 8))
			count = 120 + rnd() % 20;
		*(u32 *)(t32o8_ti + 24) = (rnd() % 6) ? 0x80000000u : 0x7f000000u;
		cu_left = (rnd() % 6) ? 0 : 1 + rnd() % 3;
		r_op = (rnd() & 1) ? 0 : (rnd() & 1) ? (u32)-(int)(1 + rnd() % 30) : 1 + rnd() % 5;
		r_val = rnd();
		r_kmfail = !(rnd() % 20);
		for (k = 0; k < 640; k++)
			t32_data_d9460[k] = (char)rnd();
		for (k = 0; k < 400; k++)
			kbuf[k] = (u8)rnd();
		memcpy(init, arena, ARENA);
		memcpy(tinit, t32_data_d9460, 640);
		memcpy(kinit, kbuf, 400);
		t32o8_clear();
		cur = &t32o8_v;
		vr = (u32)t32o_call((t32o_fn)(void (*)(void))video_input_cmd_set, T32O_PTR(arena), T32O_PTR(ubuf), count, 0, 0);
		memcpy(vend, arena, ARENA);
		memcpy(tvend, t32_data_d9460, 640);
		memcpy(arena, init, ARENA);
		memcpy(t32_data_d9460, tinit, 640);
		memcpy(kbuf, kinit, 400);
		cur = &t32o8_o;
		orr = (u32)t32_video_input_cmd_set(&ops, arena, ubuf, count, t32_data_d9460);
		bad = t32o8_cmp("vicmd", failed < 8) || vr != orr || memcmp(vend, arena, ARENA) ||
		      memcmp(tvend, t32_data_d9460, 640);
		if (bad && failed++ < 8)
			printf("MISMATCH case %u: ret %08x / %08x cmd '%.40s' count %u\n", c, vr, orr, (char *)ubuf, count);
	}
	printf("t32_vicmd_oracle: 30000 compared, %u mismatches\n", failed);
	return failed ? 1 : 0;
}
