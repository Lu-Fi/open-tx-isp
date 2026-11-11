/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host oracle helpers for the T32 tx_* kernel-glue ports (op3 oracles:
 * t32_tx*_oracle).  Included by exactly one oracle .c each.
 *
 * - The reference copies are built with REFFIX=op3_ren.sh: every external
 *   symbol NAME the vendor function calls or reads becomes op3_NAME, so each
 *   oracle defines its own recording stubs and data objects without touching
 *   the shared env.c.
 * - One event log holds, in order, every call of a kernel / driver hook
 *   (name + the arguments the hook takes), every printk / isp_printf (format
 *   string, level, arguments; %s arguments compared as strings) and every
 *   store to a hardware window (KSEG1 address, value).
 * - Hardware windows (0xb2220000 RISC-V mailbox, ...) are mapped PROT_NONE at
 *   their vendor address: each access faults, the handler opens the page,
 *   single-steps the instruction (x86 trap flag), logs the stored value and
 *   closes the page again.  Loads are not logged (a read-modify-write
 *   instruction would hide them); the loaded values come from the window
 *   content the case sets up, and the final window content is compared too.
 *
 * Our side calls the same hooks through the t32_tx_os_* functions of the
 * driver headers; this file implements them as recorders with the same
 * names as the vendor stubs.
 */
#ifndef T32_OP3_GLUE_H
#define T32_OP3_GLUE_H

#define _GNU_SOURCE
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include "t32_oracle_env.h"

#define OP3_MAXEV	512
#define OP3_MAXARG	10

enum { OP3_CALL = 1, OP3_PRINT, OP3_STORE };

struct op3_ev {
	u32 kind, n;
	const char *name;		/* hook name or format (pointer into the log's string pool) */
	u32 a[OP3_MAXARG];
	char s[4][48];			/* %s arguments of a print */
};

struct op3_log {
	unsigned int n;
	struct op3_ev e[OP3_MAXEV];
};

static struct op3_log op3_log;

static struct op3_ev *op3_new(u32 kind, const char *name)
{
	struct op3_ev *e;

	if (op3_log.n >= OP3_MAXEV) {
		op3_log.n++;
		return NULL;
	}
	e = &op3_log.e[op3_log.n++];
	memset(e, 0, sizeof(*e));
	e->kind = kind;
	e->name = name;
	return e;
}

/* ---- scripted return values of the hooks, by name ---- */
#define OP3_MAXRET	32
static struct { const char *name; u32 v; } op3_rets[OP3_MAXRET];
static unsigned int op3_nret;

static void op3_ret(const char *name, u32 v)
{
	unsigned int i;

	for (i = 0; i < op3_nret; i++)
		if (!strcmp(op3_rets[i].name, name)) {
			op3_rets[i].v = v;
			return;
		}
	if (op3_nret < OP3_MAXRET) {
		op3_rets[op3_nret].name = name;
		op3_rets[op3_nret++].v = v;
	}
}

static u32 op3_retval(const char *name)
{
	unsigned int i;

	for (i = 0; i < op3_nret; i++)
		if (!strcmp(op3_rets[i].name, name))
			return op3_rets[i].v;
	return 0;
}

/* a hook call with its first n arguments */
static u32 op3_callv(const char *name, unsigned int n, const u32 *a)
{
	struct op3_ev *e = op3_new(OP3_CALL, name);
	unsigned int i;

	if (e) {
		e->n = n;
		for (i = 0; i < n && i < OP3_MAXARG; i++)
			e->a[i] = a[i];
	}
	return op3_retval(name);
}

static u32 op3_call(const char *name, unsigned int n, u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 a[4] = { a0, a1, a2, a3 };

	return op3_callv(name, n, a);
}

/* vendor stub: u64 op3_<name>(a0..a3), logs the first n arguments */
#define OP3_STUB(name, n) \
	u64 op3_##name(u32 a0, u32 a1, u32 a2, u32 a3); \
	u64 op3_##name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ return op3_call(#name, n, a0, a1, a2, a3); }

/* ---- prints: format, level, the arguments the format consumes ---- */
static unsigned int op3_nconv(const char *f, char *types)
{
	unsigned int n = 0;

	for (; *f; f++) {
		if (*f != '%')
			continue;
		f++;
		if (*f == '%')
			continue;
		while (*f && !strchr("diouxXcsp", *f))
			f++;
		if (!*f)
			break;
		if (n < OP3_MAXARG)
			types[n] = *f;
		n++;
	}
	return n;
}

/* a print argument the vendor takes from a stale register / stack word: not compared (set by the oracle) */
static const char *op3_mask_fmt, *op3_mask_fmt2;
static u32 op3_mask_args, op3_mask_args2;

static void op3_printv(u32 level, const char *fmt, const u32 *arg)
{
	struct op3_ev *e = op3_new(OP3_PRINT, fmt);
	char t[OP3_MAXARG];
	unsigned int i, n, ns = 0;

	if (!e)
		return;
	n = op3_nconv(fmt, t);
	e->n = n;
	e->a[OP3_MAXARG - 1] = level;
	for (i = 0; i < n && i < OP3_MAXARG - 1; i++) {
		if (t[i] == 's' && ns < 4) {
			const char *s = T32O_HOST(arg[i]);

			if (arg[i] < 0x10000u || arg[i] >= 0x80000000u)	/* not a host string (vendor bug or KSEG): the value */
				snprintf(e->s[ns++], sizeof(e->s[0]), "<ptr %08x>", arg[i]);
			else
				snprintf(e->s[ns++], sizeof(e->s[0]), "%s", s);
		} else {
			e->a[i] = arg[i];
		}
	}
	if (op3_mask_fmt && !strcmp(op3_mask_fmt, fmt))
		for (i = 0; i < OP3_MAXARG - 1; i++)
			if (op3_mask_args & (1u << i))
				e->a[i] = 0;
	if (op3_mask_fmt2 && !strcmp(op3_mask_fmt2, fmt)) {
		for (i = 0; i < OP3_MAXARG - 1; i++)
			if (op3_mask_args2 & (1u << i))
				e->a[i] = 0;
		for (i = 0; i < 4; i++)		/* a masked %s: bit 16 + string index */
			if (op3_mask_args2 & (0x10000u << i))
				e->s[i][0] = 0;
	}
}

/* vendor isp_printf(level, fmt, ...) / printk(fmt, ...): up to 8 arguments after the format */
u64 op3_isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, u32 a8, u32 a9);
u64 op3_isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, u32 a8, u32 a9)
{
	u32 a[8] = { a2, a3, a4, a5, a6, a7, a8, a9 };

	op3_printv(a0, T32O_HOST(a1), a);
	return 0;
}

u64 op3_printk(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, u32 a8, u32 a9);
u64 op3_printk(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7, u32 a8, u32 a9)
{
	u32 a[8] = { a1, a2, a3, a4, a5, a6, a7, a8 };

	(void)a9;
	op3_printv(0xffffffffu, T32O_HOST(a0), a);
	return 0;
}

/* our side: the driver headers print through these (host build) */
static void op3_our_print(u32 level, const char *fmt, va_list ap)
{
	char t[OP3_MAXARG];
	u32 a[OP3_MAXARG] = { 0 };
	unsigned int i, n = op3_nconv(fmt, t);

	for (i = 0; i < n && i < OP3_MAXARG; i++)
		a[i] = t[i] == 's' ? T32O_PTR(va_arg(ap, const char *)) : va_arg(ap, u32);
	op3_printv(level, fmt, a);
}

void t32_tx_os_printk(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	op3_our_print(0xffffffffu, fmt, ap);
	va_end(ap);
}

void t32_tx_os_isp_printf(u32 level, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	op3_our_print(level, fmt, ap);
	va_end(ap);
}

/* ---- hardware windows ---- */
#define OP3_MAXWIN	4
static struct { uintptr_t base, len; } op3_win[OP3_MAXWIN];
static unsigned int op3_nwin;
static uintptr_t op3_pend;
static int op3_pend_w;

static int op3_inwin(uintptr_t a)
{
	unsigned int i;

	for (i = 0; i < op3_nwin; i++)
		if (a >= op3_win[i].base && a < op3_win[i].base + op3_win[i].len)
			return (int)i;
	return -1;
}

static void op3_segv(int sig, siginfo_t *si, void *ucv)
{
	ucontext_t *uc = ucv;
	uintptr_t a = (uintptr_t)si->si_addr;
	int w = op3_inwin(a);

	(void)sig;
	if (w < 0) {
		fprintf(stderr, "op3: fault at %p, ip %llx\n", si->si_addr, (unsigned long long)uc->uc_mcontext.gregs[REG_RIP]);
		signal(SIGSEGV, SIG_DFL);
		return;		/* a real crash: refaults with the default action */
	}
	mprotect((void *)op3_win[w].base, op3_win[w].len, PROT_READ | PROT_WRITE);
	op3_pend = a & ~(uintptr_t)3;
	op3_pend_w = (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0;
	uc->uc_mcontext.gregs[REG_EFL] |= 0x100;
}

static void op3_trap(int sig, siginfo_t *si, void *ucv)
{
	ucontext_t *uc = ucv;
	int w = op3_inwin(op3_pend);

	(void)sig; (void)si;
	if (w >= 0) {
		if (op3_pend_w) {
			struct op3_ev *e = op3_new(OP3_STORE, "store");

			if (e) {
				e->n = 2;
				e->a[0] = (u32)op3_pend;
				e->a[1] = *(volatile u32 *)op3_pend;
			}
		}
		mprotect((void *)op3_win[w].base, op3_win[w].len, PROT_NONE);
	}
	op3_pend = 0;
	uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
}

static void op3_win_map(uintptr_t base, uintptr_t len)
{
	struct sigaction sa;
	void *p;

	p = mmap((void *)base, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (p != (void *)base) {
		fprintf(stderr, "op3: cannot map the hardware window 0x%lx\n", (unsigned long)base);
		exit(2);
	}
	op3_win[op3_nwin].base = base;
	op3_win[op3_nwin++].len = len;
	mprotect(p, len, PROT_NONE);
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = op3_segv;
	sa.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigaction(SIGSEGV, &sa, NULL);
	sa.sa_sigaction = op3_trap;
	sigaction(SIGTRAP, &sa, NULL);
}

/* the oracle's own (untraced) access to a window */
static void op3_win_open(int on)
{
	unsigned int i;

	for (i = 0; i < op3_nwin; i++)
		mprotect((void *)op3_win[i].base, op3_win[i].len, on ? PROT_READ | PROT_WRITE : PROT_NONE);
}

/* ---- compare two logs ---- */
static int op3_ev_eq(const struct op3_ev *x, const struct op3_ev *y)
{
	unsigned int i;

	if (x->kind != y->kind || x->n != y->n || strcmp(x->name, y->name))
		return 0;
	for (i = 0; i < OP3_MAXARG; i++)
		if (x->a[i] != y->a[i])
			return 0;
	for (i = 0; i < 4; i++)
		if (strcmp(x->s[i], y->s[i]))
			return 0;
	return 1;
}

static void op3_ev_print(const char *who, const struct op3_ev *e)
{
	unsigned int i;

	fprintf(stderr, "  %s: kind %u '%s' n %u:", who, e->kind, e->name, e->n);
	for (i = 0; i < OP3_MAXARG; i++)
		fprintf(stderr, " %x", e->a[i]);
	for (i = 0; i < 4; i++)
		if (e->s[i][0])
			fprintf(stderr, " \"%s\"", e->s[i]);
	fprintf(stderr, "\n");
}

/* 0 = equal; prints the first difference */
static int op3_log_cmp(const char *what, const struct op3_log *v, const struct op3_log *o)
{
	unsigned int i;

	if (v->n > OP3_MAXEV || o->n > OP3_MAXEV) {
		fprintf(stderr, "%s: event log overflow\n", what);
		return 1;
	}
	for (i = 0; i < v->n && i < o->n; i++)
		if (!op3_ev_eq(&v->e[i], &o->e[i])) {
			fprintf(stderr, "%s: event %u differs\n", what, i);
			op3_ev_print("vendor", &v->e[i]);
			op3_ev_print("ours  ", &o->e[i]);
			return 1;
		}
	if (v->n != o->n) {
		fprintf(stderr, "%s: %u vendor events, %u ours\n", what, v->n, o->n);
		if (i < v->n)
			op3_ev_print("vendor", &v->e[i]);
		if (i < o->n)
			op3_ev_print("ours  ", &o->e[i]);
		return 1;
	}
	return 0;
}

static u32 op3_rs = 0x3c6ef372u;
static u32 op3_rnd(void)
{
	op3_rs ^= op3_rs << 13; op3_rs ^= op3_rs >> 17; op3_rs ^= op3_rs << 5;
	return op3_rs;
}

#define OP3_VC(fn, a0, a1, a2, a3, a4) ((u32)t32o_call((t32o_fn)(void (*)(void))(fn), a0, a1, a2, a3, a4))

#endif
