/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hook recorder of the private_* wrapper oracles (t32_priv_oracle,
 * t32_privx_oracle): included once by each oracle after t32o_kern.h.  Every
 * hook call appends (name, arguments) to the log and returns the next value
 * of a seeded generator, so both sides of a case see the same kernel.
 */
#ifndef T32O_KERN_REC_H
#define T32O_KERN_REC_H

#include <stdio.h>

#define T32K_LOGN	1200
struct t32k_ent {
	const char *name;
	int n;
	u64 a[8];
};
struct t32k_log {
	int n, over;
	struct t32k_ent e[T32K_LOGN];
};
static struct t32k_log t32k_cur;
static u64 t32k_rs;
/* called after every recorded hook (an oracle may log device state or change it) */
static void (*t32k_on)(const char *name);

u8 t32k_ti[32] __attribute__((aligned(8)));
u8 t32k_iomem_resource[32];
u8 __this_module[64];
u8 t32k_system_wq[8] __attribute__((aligned(8)));
u32 ispmem_base, ispmem_size;
/* objects pointer arguments point into (low addresses: the link is non-PIE) */
static u8 t32k_pool[4][1024] __attribute__((aligned(16)));

static u64 t32k_next(u64 *s)
{
	u64 x = *s;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	*s = x;
	return x;
}

/* hook result: zero, one, error codes, small, 32 bit and 64 bit values */
static u64 t32k_ret(void)
{
	u64 x = t32k_next(&t32k_rs);

	switch (x & 7) {
	case 0: return 0;
	case 1: return 1;
	case 2: return (u64)-1;
	case 3: return (u64)-4095 - ((x >> 8) & 1);
	case 4: return x >> 52;
	case 5: return (u64)(s64)(s32)(x >> 32);
	case 6: return x;
	default: return (u64)-512;
	}
}

static u64 t32k_hash(const char *s)
{
	u64 h = 1469598103934665603ull;

	if (!s)
		return 0;
	while (*s)
		h = (h ^ (u8)*s++) * 1099511628211ull;
	return h;
}

static struct t32k_ent *t32k_push(const char *name, int n)
{
	struct t32k_ent *e;

	if (t32k_cur.n >= T32K_LOGN) {
		t32k_cur.over = 1;
		return NULL;
	}
	e = &t32k_cur.e[t32k_cur.n++];
	memset(e, 0, sizeof(*e));
	e->name = name;
	e->n = n;
	return e;
}

u64 t32k_rec(const char *name, int n, const int *kind, const u64 *a)
{
	struct t32k_ent *e = t32k_push(name, n);
	int i;

	for (i = 0; e && i < n; i++)
		e->a[i] = kind[i] == T32K_S ? t32k_hash((const char *)(uintptr_t)a[i]) :
			  kind[i] == T32K_O ? (a[i] != 0) : a[i];
	if (t32k_on)
		t32k_on(name);
	return t32k_ret();
}

u64 t32k_rec_vaf(const char *name, u64 a0, const char *fmt, const struct va_format *vaf)
{
	struct t32k_ent *e = t32k_push(name, 7);
	va_list c;
	int i;

	if (e) {
		e->a[0] = a0;
		e->a[1] = t32k_hash(fmt);
		e->a[2] = T32K_U(vaf->fmt);
		va_copy(c, *vaf->va);
		for (i = 3; i < 7; i++)
			e->a[i] = (u64)(s64)va_arg(c, int);
		va_end(c);
	}
	return t32k_ret();
}

int t32k_isp_printf(unsigned int level, const char *fmt, ...)
{
	static const int k_[2] = { T32K_V, T32K_S };
	u64 v[2] = { level, T32K_U(fmt) };

	return (int)t32k_rec("isp_printf", 2, k_, v);
}

/* 0 = equal; prints the first difference */
static int t32k_logcmp(const char *fn, int cs, const struct t32k_log *x, const struct t32k_log *y)
{
	int i, j;

	if (x->over || y->over || x->n != y->n) {
		printf("%s case %d: %d vs %d hook calls\n", fn, cs, x->n, y->n);
		return 1;
	}
	for (i = 0; i < x->n; i++) {
		if (strcmp(x->e[i].name, y->e[i].name) || x->e[i].n != y->e[i].n) {
			printf("%s case %d: call %d %s vs %s\n", fn, cs, i, x->e[i].name, y->e[i].name);
			return 1;
		}
		for (j = 0; j < x->e[i].n; j++)
			if (x->e[i].a[j] != y->e[i].a[j]) {
				printf("%s case %d: call %d %s arg %d 0x%llx vs 0x%llx\n", fn, cs, i, x->e[i].name, j,
				       (unsigned long long)x->e[i].a[j], (unsigned long long)y->e[i].a[j]);
				return 1;
			}
	}
	return 0;
}

#endif
