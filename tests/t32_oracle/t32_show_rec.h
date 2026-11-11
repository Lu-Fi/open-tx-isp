/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Print-stream recorder of the show / print oracles (t32_sh*, t32_tpr*): the vendor
 * reference (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our driver headers print
 * through the same functions here: seq_printf, private_seq_printf, isp_printf and
 * printk (renamed t32show_printk in the reference by fix_show.sh, env.c owns printk).
 * Every call is one event: kind, seq_file / level, and the text rendered from the
 * format and the arguments it consumes.  The arguments are taken as 32-bit words (the
 * vendor's o32 varargs; "%s" a pointer below 4 GiB, "%l" is 32 bits there), so a wrong
 * argument, a wrong order or a wrong format shows as a different text.  seq_printf adds
 * the text length to seq_file.count (+12) as the kernel does, the vendor reads it back.
 * sr_side: 0 = reference run, 1 = ours; sr_cmp() compares the two logs.
 */
#ifndef T32_SHOW_REC_H
#define T32_SHOW_REC_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include "t32_oracle_env.h"

#define SR_MAXE	8192
#define SR_TXT	1024
enum { SR_SEQ = 1, SR_PSEQ, SR_ISP, SR_PRINTK, SR_HOOK };
struct sr_ev { u32 kind, a, b, c; char t[SR_TXT]; };
static struct sr_ev sr_ev[2][SR_MAXE];
static unsigned int sr_n[2];
static int sr_side;

static __attribute__((unused)) void sr_begin(void) { sr_n[0] = sr_n[1] = 0; sr_side = 0; }

static void sr_put(u32 kind, u32 a, u32 b, u32 c, const char *t)
{
	struct sr_ev *e;

	if (sr_n[sr_side] >= SR_MAXE) { sr_n[sr_side]++; return; }
	e = &sr_ev[sr_side][sr_n[sr_side]++];
	e->kind = kind; e->a = a; e->b = b; e->c = c;
	size_t l = t ? strlen(t) : 0;

	if (l >= sizeof(e->t))
		l = sizeof(e->t) - 1;
	memcpy(e->t, t ? t : "", l);
	e->t[l] = 0;
}

/* render fmt with 32-bit word arguments (flags, width, precision, h / hh / l length; d i u x X o c s p) */
static int sr_vfmt(char *o, size_t on, const char *f, va_list ap)
{
	size_t k = 0;
	char sub[32], tmp[512];

	while (*f) {
		const char *s = f;
		size_t sl = 0;
		int h = 0, n = 0;
		char c;
		u32 v;

		if (*f != '%' || f[1] == '%') {
			if (k + 1 < on)
				o[k++] = *f;
			f += (*f == '%') ? 2 : 1;
			continue;
		}
		f++;
		while (*f && strchr("-+ #0", *f))
			f++;
		while (*f >= '0' && *f <= '9')
			f++;
		if (*f == '.') {
			f++;
			while (*f >= '0' && *f <= '9')
				f++;
		}
		sl = (size_t)(f - s);
		while (*f == 'h' || *f == 'l' || *f == 'z') {
			if (*f == 'h')
				h++;
			f++;
		}
		c = *f;
		if (!c || sl + 2 > sizeof(sub)) {
			fprintf(stderr, "sr_vfmt: bad format \"%s\"\n", s);
			exit(3);
		}
		f++;
		memcpy(sub, s, sl);
		sub[sl] = c; sub[sl + 1] = 0;
		v = va_arg(ap, u32);
		switch (c) {
		case 'd': case 'i':
			n = snprintf(tmp, sizeof(tmp), sub, h == 2 ? (int)(signed char)v : h == 1 ? (int)(short)v : (int)v);
			break;
		case 'u': case 'x': case 'X': case 'o':
			n = snprintf(tmp, sizeof(tmp), sub, h == 2 ? (unsigned int)(u8)v : h == 1 ? (unsigned int)(u16)v : v);
			break;
		case 'c':
			n = snprintf(tmp, sizeof(tmp), sub, (int)(unsigned char)v);
			break;
		case 's':
			n = snprintf(tmp, sizeof(tmp), sub, v ? (const char *)T32O_HOST(v) : "(null)");
			break;
		case 'p':
			n = snprintf(tmp, sizeof(tmp), "%08x", v);
			break;
		default:
			fprintf(stderr, "sr_vfmt: conversion %%%c\n", c);
			exit(3);
		}
		if (n < 0)
			n = 0;
		if ((size_t)n >= sizeof(tmp))
			n = sizeof(tmp) - 1;
		for (sl = 0; sl < (size_t)n; sl++)
			if (k + 1 < on)
				o[k++] = tmp[sl];
	}
	if (on)
		o[k < on ? k : on - 1] = 0;
	return (int)k;
}

static int sr_vlog(u32 kind, u32 a, u32 fmt, va_list ap)
{
	char b[1024];
	int n = sr_vfmt(b, sizeof(b), (const char *)T32O_HOST(fmt), ap);

	sr_put(kind, a, fmt ? 1 : 0, (u32)n, b);
	return n;
}

static void sr_seq_count(u32 m, int n)
{
	if (m)
		*(u32 *)T32O_HOST(m + 12) += (u32)n;
}

/* the reference's symbols (o32 words) */
u64 seq_printf(u32 m, u32 fmt, ...);
u64 private_seq_printf(u32 m, u32 fmt, ...);
int t32show_printk(const char *fmt, ...);
int isp_printf(unsigned int level, const char *fmt, ...);

u64 seq_printf(u32 m, u32 fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = sr_vlog(SR_SEQ, m, fmt, ap);
	va_end(ap);
	sr_seq_count(m, n);
	return 0;
}
u64 private_seq_printf(u32 m, u32 fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = sr_vlog(SR_PSEQ, m, fmt, ap);
	va_end(ap);
	sr_seq_count(m, n);
	return 0;
}
int t32show_printk(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = sr_vlog(SR_PRINTK, 0, (u32)(uintptr_t)fmt, ap);
	va_end(ap);
	return n;
}
int isp_printf(unsigned int level, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = sr_vlog(SR_ISP, level, (u32)(uintptr_t)fmt, ap);
	va_end(ap);
	return n;
}

/* our side: the same recorders behind the header's print macros (pointers below 4 GiB: t32o_alloc / .data) */
#define T32_SHOW_SEQ(m, f, ...)		((void)seq_printf(T32O_PTR(m), T32O_PTR(f), ##__VA_ARGS__))
#define T32_SHOW_PSEQ(m, f, ...)	((void)private_seq_printf(T32O_PTR(m), T32O_PTR(f), ##__VA_ARGS__))
#define T32_SHOW_PRINTK		t32show_printk

static __attribute__((unused)) int sr_cmp(const char *who, unsigned int c, const char *what)
{
	unsigned int i;
	const struct sr_ev *x, *y;

	if (sr_n[0] != sr_n[1] || sr_n[0] > SR_MAXE) {
		printf("%s: case %u %s: %u vs %u logged calls\n", who, c, what, sr_n[0], sr_n[1]);
		if (sr_n[0] && sr_n[1] && sr_n[0] <= SR_MAXE && sr_n[1] <= SR_MAXE) {
			for (i = 0; i < sr_n[0] && i < sr_n[1]; i++)
				if (strcmp(sr_ev[0][i].t, sr_ev[1][i].t))
					break;
			if (i < sr_n[0] && i < sr_n[1])
				printf("  first diff at %u: vendor \"%s\" / ours \"%s\"\n", i, sr_ev[0][i].t, sr_ev[1][i].t);
		}
		return 1;
	}
	for (i = 0; i < sr_n[0]; i++) {
		x = &sr_ev[0][i]; y = &sr_ev[1][i];
		if (x->kind != y->kind || x->a != y->a || x->b != y->b || x->c != y->c || strcmp(x->t, y->t)) {
			printf("%s: case %u %s call %u: vendor %u/%08x \"%s\" / ours %u/%08x \"%s\"\n", who, c, what, i,
			       x->kind, x->a, x->t, y->kind, y->a, y->t);
			return 1;
		}
	}
	return 0;
}

static u32 sr_rs = 0x2545f491u;
static __attribute__((unused)) u32 sr_rnd(void)
{
	sr_rs ^= sr_rs << 13; sr_rs ^= sr_rs >> 17; sr_rs ^= sr_rs << 5;
	return sr_rs;
}
static __attribute__((unused)) void sr_fill(void *p, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		((u8 *)p)[i] = (u8)(sr_rnd() >> 11);
}
/* error pointers (-4095 .. -1) readable: the two pages at 0xfffff000 and a 192 KiB block at 0x20200000 are all bytes
 * 0x20, so any word read there (aligned or not) is 0x20202020, an address inside the block: a port that dereferences an
 * error pointer the vendor rejects keeps reading non-zero words and prints something else instead of crashing */
static void *sr_map(u32 at, u32 len)
{
	void *p = mmap((void *)(uintptr_t)at, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

	if (p != (void *)(uintptr_t)at) {
		fprintf(stderr, "sr_errpage: mmap at %08x failed\n", at);
		exit(3);
	}
	return p;
}
static __attribute__((unused)) void sr_errpage(void)
{
	static void *pg, *blk;

	if (!pg) {
		pg = sr_map(0xfffff000u, 0x2000);
		blk = sr_map(0x20200000u, 0x30000);
	}
	memset(pg, 0x20, 0x2000);
	memset(blk, 0x20, 0x30000);
}
static __attribute__((unused)) u32 sr_errptr(void) { return (sr_rnd() & 3) ? (u32)-4095 + sr_rnd() % 4095 : (u32)-4095; }
#define SR_R(f)	((t32o_fn)(void (*)(void))(f))
#include "t32_show_str.inc"
#endif
