/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Call recorder of the op8 glue oracles: the vendor's kernel calls (o32 stubs in the oracle)
 * and ours (the ops struct of our header) go into two logs that are compared call by call.
 * A call is a function id, up to 6 argument words and up to 128 bytes of data (a format
 * string, copied bytes); pointers are mapped to the vendor's addresses by the oracle.
 */
#ifndef T32O8_REC_H
#define T32O8_REC_H
#include <stdio.h>
#include <string.h>
#include "t32_oracle_env.h"

#define T32O8_CALLS 1024
struct t32o8_call { u32 fn, a[6]; char data[128]; };
struct t32o8_log { unsigned int n; struct t32o8_call c[T32O8_CALLS]; };
static struct t32o8_log t32o8_v, t32o8_o;	/* vendor, ours */

static struct t32o8_call *t32o8_rec(struct t32o8_log *l, u32 fn, u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct t32o8_call *c = &l->c[l->n < T32O8_CALLS - 1 ? l->n++ : T32O8_CALLS - 1];

	memset(c, 0, sizeof(*c));
	c->fn = fn; c->a[0] = a0; c->a[1] = a1; c->a[2] = a2; c->a[3] = a3;
	return c;
}

static void t32o8_clear(void)
{
	memset(&t32o8_v, 0, sizeof(t32o8_v));
	memset(&t32o8_o, 0, sizeof(t32o8_o));
}

/* 0 = both logs equal; else prints the first difference (when verbose) */
static int t32o8_cmp(const char *what, int verbose)
{
	unsigned int i;

	if (t32o8_v.n == t32o8_o.n && !memcmp(t32o8_v.c, t32o8_o.c, t32o8_v.n * sizeof(t32o8_v.c[0])))
		return 0;
	if (verbose) {
		printf("MISMATCH %s: calls vendor %u ours %u\n", what, t32o8_v.n, t32o8_o.n);
		for (i = 0; i < t32o8_v.n || i < t32o8_o.n; i++) {
			const struct t32o8_call *v = &t32o8_v.c[i], *o = &t32o8_o.c[i];

			printf("  #%u vendor %u %08x %08x %08x %08x '%.24s' ours %u %08x %08x %08x %08x '%.24s'\n", i,
			       v->fn, v->a[0], v->a[1], v->a[2], v->a[3], v->data, o->fn, o->a[0], o->a[1], o->a[2],
			       o->a[3], o->data);
			if (i > 10)
				break;
		}
	}
	return 1;
}


/* fake thread_info of fix_op8_glue.sh (gp; +24 = addr_limit mask); only the glue oracles include this */
#ifndef T32O8_NO_TI
u8 t32o8_ti[64] __attribute__((aligned(8)));
#endif


/*
 * printf-style recorder: a[0] = level (or 0), data = the format text, then "|" and the text
 * of every %s argument; the other conversions go to a[1..5] as words.  w = the argument
 * words after the format (vendor: the o32 registers / stack words; ours: from va_list).
 */
/* argument word index of w[0] (o32: isp_printf(level, fmt, ...) = 2, printk(fmt, ...) = 1) */
static int t32o8_woff = 2;

static void t32o8_rec_fmt(struct t32o8_log *l, u32 fn, u32 level, const char *fmt, const u32 *w, int nw)
{
	struct t32o8_call *r = t32o8_rec(l, fn, level, 0, 0, 0);
	const char *p;
	int k = 0, na = 1, ll;
	size_t len;

	snprintf(r->data, sizeof(r->data), "%s", fmt ? fmt : "(null)");
	for (p = fmt; p && *p && k < nw; p++) {
		if (*p != '%')
			continue;
		if (p[1] == '%') {
			p++;
			continue;
		}
		ll = 0;
		while (*++p && strchr("-+ #0123456789.lhz", *p))
			ll += *p == 'l';
		if (!*p)
			break;
		if (ll >= 2) {	/* o32: a 64-bit argument starts on an even argument word */
			k += (k + t32o8_woff) & 1;
			if (k + 1 >= nw)
				break;
			if (na < 5) {
				r->a[na++] = w[k];
				r->a[na++] = w[k + 1];
			}
			k += 2;
			continue;
		}
		if (*p == 's') {
			len = strlen(r->data);
			snprintf(r->data + len, sizeof(r->data) - len, "|%s", (const char *)T32O_HOST(w[k]));
		} else if (na < 6) {
			r->a[na++] = w[k];
		}
		k++;
	}
}

#include <stdarg.h>
/* ours: collect the argument words of a format from a va_list (%s as pointer, the rest as u32) */
static int t32o8_va_words(const char *fmt, va_list ap, u32 *w, int max)
{
	const char *p;
	int k = 0, ll;

	for (p = fmt; p && *p && k < max; p++) {
		if (*p != '%')
			continue;
		if (p[1] == '%') {
			p++;
			continue;
		}
		ll = 0;
		while (*++p && strchr("-+ #0123456789.lhz", *p))
			ll += *p == 'l';
		if (!*p)
			break;
		if (ll >= 2) {	/* the same word layout as the o32 caller: pad to an even word */
			unsigned long long v = va_arg(ap, unsigned long long);

			if ((k + t32o8_woff) & 1)
				w[k++] = 0;
			if (k + 2 > max)
				break;
			w[k++] = (u32)v;
			w[k++] = (u32)(v >> 32);
			continue;
		}
		if (*p == 's' || *p == 'p')
			w[k++] = T32O_PTR(va_arg(ap, const void *));
		else
			w[k++] = va_arg(ap, u32);
	}
	return k;
}
#endif
