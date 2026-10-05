/*
 * Freestanding i386 runtime for the T20 firmware differential harness:
 * no libc and no 32-bit multilib are needed, only "gcc -m32" and a kernel
 * with IA32 emulation.  Built with -fno-builtin so the string helpers are
 * not turned back into calls to themselves.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include "rt.h"

static long sys3(long nr, long a, long b, long c)
{
	long r;
	__asm__ volatile("int $0x80" : "=a"(r) : "a"(nr), "b"(a), "c"(b), "d"(c) : "memory");
	return r;
}
long rt_write(int fd, const void *b, unsigned long n) { return sys3(4, fd, (long)b, (long)n); }
long rt_read(int fd, void *b, unsigned long n) { return sys3(3, fd, (long)b, (long)n); }
int rt_open(const char *p, int flags) { return (int)sys3(5, (long)p, flags, 0644); }
int rt_close(int fd) { return (int)sys3(6, fd, 0, 0); }
void rt_exit(int code) { sys3(1, code, 0, 0); for (;;) ; }

void *memset(void *d, int c, size_t n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *p = d; const unsigned char *q = s;
	if (p < q) while (n--) *p++ = *q++;
	else { p += n; q += n; while (n--) *--p = *--q; }
	return d;
}
int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *p = a, *q = b;
	for (; n; n--, p++, q++) if (*p != *q) return *p - *q;
	return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
int strcmp(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return (unsigned char)*a - (unsigned char)*b; }
int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) { if (*a != *b) return (unsigned char)*a - (unsigned char)*b; if (!*a) break; }
	return 0;
}
char *strrchr(const char *s, int c) { const char *r = 0; do { if (*s == (char)c) r = s; } while (*s++); return (char *)r; }
char *strstr(const char *h, const char *n)
{
	size_t l = strlen(n);
	for (; *h; h++) if (!strncmp(h, n, l)) return (char *)h;
	return l ? 0 : (char *)h;
}

/* libgcc 64-bit helpers (no -m32 libgcc on the host) */
unsigned long long __udivmoddi4(unsigned long long n, unsigned long long d, unsigned long long *rem)
{
	unsigned long long q = 0, r = 0;
	int i;
	if (d == 0) { __builtin_trap(); }
	for (i = 63; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1);
		if (r >= d) { r -= d; q |= 1ULL << i; }
	}
	if (rem) *rem = r;
	return q;
}
unsigned long long __udivdi3(unsigned long long n, unsigned long long d) { return __udivmoddi4(n, d, 0); }
unsigned long long __umoddi3(unsigned long long n, unsigned long long d) { unsigned long long r; __udivmoddi4(n, d, &r); return r; }
long long __divdi3(long long n, long long d)
{
	int neg = (n < 0) ^ (d < 0);
	unsigned long long q = __udivmoddi4(n < 0 ? -(unsigned long long)n : n, d < 0 ? -(unsigned long long)d : d, 0);
	return neg ? -(long long)q : (long long)q;
}
long long __moddi3(long long n, long long d)
{
	unsigned long long r;
	__udivmoddi4(n < 0 ? -(unsigned long long)n : n, d < 0 ? -(unsigned long long)d : d, &r);
	return n < 0 ? -(long long)r : (long long)r;
}
/* MIPS div/divu by zero does not trap (kernel: -mno-check-zero-division);
 * the result is unpredictable.  Make it deterministic and visible. */
unsigned long rt_div0_count;
long long div64_s64(long long a, long long b) { if (!b) { rt_div0_count++; return -1; } return __divdi3(a, b); }
unsigned long long div64_u64(unsigned long long a, unsigned long long b) { if (!b) { rt_div0_count++; return ~0ULL; } return __udivdi3(a, b); }

/* small vsnprintf: %[-0][width][l|ll|h|hh|z](d|i|u|x|X|p|s|c|%) */
static void put(char *b, size_t sz, size_t *o, char c) { if (*o + 1 < sz) b[*o] = c; (*o)++; }
int vsnprintf(char *b, size_t sz, const char *f, va_list ap)
{
	size_t o = 0;
	for (; *f; f++) {
		int left = 0, zero = 0, width = 0, lng = 0, base = 10, neg = 0, upper = 0;
		unsigned long long v;
		char tmp[24];
		int n = 0;
		if (*f != '%') { put(b, sz, &o, *f); continue; }
		f++;
		for (;; f++) { if (*f == '-') left = 1; else if (*f == '0') zero = 1; else break; }
		while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
		while (*f == 'l' || *f == 'h' || *f == 'z') { if (*f == 'l') lng++; f++; }
		switch (*f) {
		case 'd': case 'i': {
			long long s = lng > 1 ? va_arg(ap, long long) : va_arg(ap, int);
			if (s < 0) { neg = 1; v = -(unsigned long long)s; } else v = s;
			break; }
		case 'u': v = lng > 1 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int); break;
		case 'X': upper = 1; /* fallthrough */
		case 'x': base = 16; v = lng > 1 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int); break;
		case 'p': base = 16; v = (uintptr_t)va_arg(ap, void *); put(b, sz, &o, '0'); put(b, sz, &o, 'x'); break;
		case 'c': put(b, sz, &o, (char)va_arg(ap, int)); continue;
		case 's': {
			const char *s = va_arg(ap, const char *);
			int l;
			if (!s) s = "(null)";
			l = (int)strlen(s);
			if (!left) while (l < width--) put(b, sz, &o, ' ');
			while (*s) put(b, sz, &o, *s++);
			if (left) while (l < width--) put(b, sz, &o, ' ');
			continue; }
		case '%': put(b, sz, &o, '%'); continue;
		case 0: f--; continue;
		default: put(b, sz, &o, '%'); put(b, sz, &o, *f); continue;
		}
		do { int dgt = (int)(v % base); tmp[n++] = (char)(dgt < 10 ? '0' + dgt : (upper ? 'A' : 'a') + dgt - 10); v /= base; } while (v);
		if (neg) { if (zero) { put(b, sz, &o, '-'); width--; } else tmp[n++] = '-'; }
		if (!left) while (n < width--) put(b, sz, &o, zero ? '0' : ' ');
		while (n) put(b, sz, &o, tmp[--n]);
		if (left) while (width-- > (int)0) put(b, sz, &o, ' ');
	}
	if (sz) b[o < sz ? o : sz - 1] = 0;
	return (int)o;
}
int snprintf(char *b, size_t sz, const char *f, ...) { va_list ap; int r; va_start(ap, f); r = vsnprintf(b, sz, f, ap); va_end(ap); return r; }
int sprintf(char *b, const char *f, ...) { va_list ap; int r; va_start(ap, f); r = vsnprintf(b, 1 << 20, f, ap); va_end(ap); return r; }

static char outbuf[1 << 16];
static size_t outlen;
void rt_flush(void) { if (outlen) rt_write(1, outbuf, outlen); outlen = 0; }
void rt_puts(const char *s) { size_t l = strlen(s); if (outlen + l > sizeof(outbuf)) rt_flush(); if (l > sizeof(outbuf)) { rt_write(1, s, l); return; } memcpy(outbuf + outlen, s, l); outlen += l; }
void rt_printf(const char *f, ...) { char b[1024]; va_list ap; va_start(ap, f); vsnprintf(b, sizeof(b), f, ap); va_end(ap); rt_puts(b); }

extern int main(int argc, char **argv);
__attribute__((used)) void rt_start_c(long *sp) { int argc = (int)sp[0]; char **argv = (char **)(sp + 1); int r = main(argc, argv); rt_flush(); rt_exit(r); }
__asm__(".text\n.globl _start\n_start:\n\txorl %ebp,%ebp\n\tmovl %esp,%eax\n\tandl $-16,%esp\n\tsubl $12,%esp\n\tpushl %eax\n\tcall rt_start_c\n\thlt\n");
