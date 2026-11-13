// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_vicdev.h (tisp_vic_ctrl_release,
 * tisp_vic_mmap, tisp_vic_ctrl_ioctl) against the vendor functions (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc).  Per case random raw state (40 B), VMA words, inode /
 * file values, scripted results (single_release, remap 0 / error) and a
 * get_par stub that rewrites address / memMode deterministically.
 * Compared: return values, every call in order with its arguments (raw
 * state pointers as tokens, print texts by content), the raw state bytes.
 * ioctl: commands GET / SET / wrong type / nr >= 51 / other 'v', sensor
 * pointer NULL / error pointer / valid (GET only with a valid one: the
 * vendor dereferences it), access check pass / fail (fake thread_info
 * addr_limit), copy result 0 / partial, SET state 0 .. 3 / other; the user
 * buffer bytes are compared too.  The reference copy is built with
 * fix_op11_vicioctl.sh (gp = fake thread_info, __copy_user's a2 result).
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_vicdev.h"

u8 t32_data_d941c[48] __attribute__((aligned(16)));	/* .bss: VIC raw buffer state */
__asm__(".globl t32_data_d9420\n.set t32_data_d9420, t32_data_d941c + 4");
__asm__(".globl t32_data_d943a\n.set t32_data_d943a, t32_data_d941c + 30");
__attribute__((weak)) char t32_data_bd858[] = "tisp_vic_mmap";					/* data.c:569 */
__attribute__((weak)) char t32_data_c3a48[] = "[%s, %d] vic paddr 0x%x, vaddr 0x%x, memMode is %d\n";	/* data.c:6839 */
__attribute__((weak)) char t32_data_c3a7c[] = "Failed to remap PTE for shared memory\n";		/* data.c:6852 */
__attribute__((weak)) char t32_data_bd844[] = "tisp_vic_ctrl_ioctl";				/* data.c:564 */
__attribute__((weak)) char t32_data_bd82c[] = "tisp_vic_cmd_process";				/* data.c:558 */
__attribute__((weak)) char t32_data_c3e3c[] = "_IOC_TYPE error !!! cmd is %d\n";			/* data.c:7092 */
__attribute__((weak)) char t32_data_c3e5c[] = "_IOC_NR error !!! cmd is %d\n";			/* data.c:7100 */
__attribute__((weak)) char t32_data_c3e7c[] = "[%s, %d]: vic dmaout paddr is 0x%08x \n";		/* data.c:7108 */
__attribute__((weak)) char t32_data_c3ea4[] = "get ioctl par copy_to_user error!!!\n";		/* data.c:7118 */
__attribute__((weak)) char t32_data_c3ecc[] = "mem mode is %d, vic cmd is %d \n";			/* data.c:7128 */
__attribute__((weak)) char t32_data_c3eec[] = "[%s, %d]: vic cmd error !!!\n";			/* data.c:7136 */
u8 t32o_vic_ti[64] __attribute__((aligned(16)));	/* fake thread_info: addr_limit at +24 */
extern u64 tisp_vic_ctrl_release(u32, u32, u32, u32), tisp_vic_mmap(u32, u32, u32, u32),
	   tisp_vic_ctrl_ioctl(u32, u32, u32, u32);

static u32 rs = 0xa54ff53au;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 8
#define LW 20
struct hlog { unsigned int n; u32 e[LOGN][LW]; };
static struct hlog vl, ol, *cur;
static void hrec(u32 k, const u32 *v, unsigned int nv, const char *s)
{
	if (cur->n < LOGN) {
		memset(cur->e[cur->n], 0, sizeof(cur->e[0]));
		cur->e[cur->n][0] = k;
		memcpy(&cur->e[cur->n][1], v, nv * 4);
		if (s)
			strncpy((char *)&cur->e[cur->n][9], s, 4 * (LW - 9) - 1);
	}
	cur->n++;
}
static struct t32_vic_raw oraw;
static u32 tok(uintptr_t p)
{
	if (p == (uintptr_t)t32_data_d941c || p == (uintptr_t)&oraw) return 0x7001;
	if (p == (uintptr_t)(t32_data_d941c + 4) || p == (uintptr_t)&oraw.vaddr) return 0x7002;
	return (u32)p;
}
static u32 s_rel, s_remap, s_pr, gp_a, gp_b, in_ioctl, s_rem, ulimit;
static u8 *ubuf;

static void c_free(u32 a, u32 b) { u32 v[2] = { a, b }; if (in_ioctl) v[0] = v[1] = 0; hrec(1, v, 2, NULL); }
static int nfmt(const char *f) { int k = 0; for (; *f; f++) k += *f == '%'; return k; }
static u32 c_printv(u32 level, const char *fmt, u32 a, u32 b)
{
	u32 v[3] = { level, nfmt(fmt) > 0 ? a : 0, nfmt(fmt) > 1 ? b : 0 };

	hrec(7, v, 3, fmt);
	return s_pr;
}
static int c_access(u32 u, u32 n) { return !(((u + n) | u) & ulimit); }
static u32 c_copy(u32 kind, u32 dtok, u32 stok, void *dst, const void *src, u32 n)
{
	u32 v[3] = { dtok, stok, n };

	hrec(kind, v, 3, NULL);
	memcpy(dst, src, n - s_rem);
	return s_rem;
}
static u32 o_cto(u32 u, const void *src, u32 n) { return c_copy(8, u, tok((uintptr_t)src), T32O_HOST(u), src, n); }
static u32 o_cfrom(void *dst, u32 u, u32 n) { return c_copy(8, tok((uintptr_t)dst), u, dst, T32O_HOST(u), n); }
static void c_dma(void) { hrec(9, NULL, 0, NULL); }
u64 __copy_user(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	return (u64)c_copy(8, tok(a0), tok(a1), T32O_HOST(a0), T32O_HOST(a1), a2) << 32;	/* v1 = not copied */
}
u64 lib_tisp_vic_dma_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; c_dma(); return 0; }
static u32 c_rel(u32 a, u32 b) { u32 v[2] = { a, b }; hrec(2, v, 2, NULL); return s_rel; }
static void c_set(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f) { u32 v[6] = { a, b, c, d, e, f }; hrec(3, v, 6, NULL); }
static void c_get(u32 *vaddr, struct t32_vic_raw *raw)
{
	u32 v[2] = { tok((uintptr_t)vaddr), tok((uintptr_t)raw) };

	hrec(4, v, 2, NULL);
	*vaddr = gp_a;
	raw->paddr = gp_b;
	raw->mem_mode = (u8)(gp_a >> 8);
}
static u32 c_print(u32 level, const char *fmt, const char *fn, u32 line, u32 p, u32 va, u32 mm)
{
	u32 v[7] = { level, 0, 0, 0, 0 };
	char t[48];
	int k = nfmt(fmt) - 2;	/* arguments after "[%s, %d]" */

	snprintf(t, sizeof(t), "%.30s", fmt);
	if (k >= 0) {
		v[1] = line; v[2] = k > 0 ? p : 0; v[3] = k > 1 ? va : 0; v[4] = k > 2 ? mm : 0;
		snprintf(t, sizeof(t), "%.24s|%s", fmt, fn);
	}
	hrec(5, v, 5, t);
	return s_pr;
}
static u32 c_remap(u32 vma, u32 a, u32 pfn, u32 sz, u32 prot) { u32 v[5] = { vma, a, pfn, sz, prot }; hrec(6, v, 5, NULL); return s_remap; }

#define H(a) ((const char *)T32O_HOST(a))
u64 lib_tisp_raw_free(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; c_free(a0, a1); return 0; }
u64 private_single_release(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return c_rel(a0, a1); }
u64 lib_tisp_vic_set_par(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { c_set(a0, a1, a2, a3, a4, a5); return 0; }
u64 lib_tisp_vic_get_par(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; c_get(T32O_HOST(a0), T32O_HOST(a1)); return 0; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	if (!strncmp(H(a1), "[%s", 3))
		return c_print(a0, H(a1), H(a2), a3, a4, a5, a6);
	return c_printv(a0, H(a1), a2, a3);
}
u64 remap_pfn_range(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return c_remap(a0, a1, a2, a3, a4); }

static void o_free(void *a, void *b) { c_free(T32O_PTR(a), T32O_PTR(b)); }
static u32 o_rel(void *a, void *b) { return c_rel(T32O_PTR(a), T32O_PTR(b)); }
static u32 o_remap(struct t32_vicdev_vma *v, u32 a, u32 pfn, u32 sz, u32 prot) { return c_remap(T32O_PTR(v), a, pfn, sz, prot); }
static const struct t32_vicdev_ops ops = { o_free, o_rel, c_set, c_get, c_print, o_remap, c_printv, c_access,
					    o_cto, o_cfrom, c_dma };

/* one ioctl case; 0 = equal */
static int ioctl_case(unsigned long n, u8 *fileb, u8 *devb, u8 *sdb, u8 *attrb, unsigned long *cnt)
{
	u8 raw0[40], u0[40], uo[40];
	u32 r = rnd() % 8, cmd, sd, i, rv, ro, sel = rnd() % 3;

	cmd = r < 3 ? 0x20007600u : r < 6 ? 0x20007601u : r == 6 ? (rnd() & ~0xff00u) | ((rnd() % 255) << 8) : 0;
	if (r == 6 && ((cmd >> 8) & 0xff) == 'v')
		cmd ^= 0x100;
	if (r == 7)	/* 'v' with nr >= 51, or another valid 'v' nr */
		cmd = (rnd() & 0xffff0000u) | 0x7600 | ((rnd() & 1) ? 51 + rnd() % 205 : rnd() % 51);
	if (cmd == 0x20007600u) sel = 2;	/* GET needs the sensor (vendor dereferences it) */
	sd = sel == 0 ? 0 : sel == 1 ? ((rnd() & 1) ? 0xfffff001u : 0xfffff001u + rnd() % 4095) : T32O_PTR(sdb);
	{ u32 w = T32O_PTR(devb); memcpy(fileb + 120, &w, 4); }
	memcpy(devb + 72, &sd, 4);
	{ u32 w = T32O_PTR(attrb); memcpy(sdb + 280, &w, 4); }
	for (i = 0; i < 8; i++) attrb[288 + i] = rnd();
	for (i = 0; i < 40; i++) { raw0[i] = rnd(); u0[i] = rnd(); }
	{ u32 st = rnd() % 6; st = st < 4 ? st : (st == 4 ? rnd() : 4); memcpy(u0 + 36, &st, 4); }
	if (rnd() & 1) raw0[31] = 2;
	ulimit = (rnd() & 3) ? 0 : 0xffffffffu;
	s_rem = (rnd() & 3) ? 0 : 1 + rnd() % 40;
	s_rel = rnd(); s_pr = rnd(); gp_a = rnd(); gp_b = rnd();
	memcpy(t32o_vic_ti + 24, &ulimit, 4);
	in_ioctl = 1;

	memset(&ol, 0, sizeof(ol)); cur = &ol;
	memcpy(&oraw, raw0, 40); memcpy(ubuf, u0, 40);
	ro = (u32)t32_vic_ctrl_ioctl(&ops, &oraw, fileb, cmd, T32O_PTR(ubuf));
	memcpy(uo, ubuf, 40);

	memset(&vl, 0, sizeof(vl)); cur = &vl;
	memcpy(t32_data_d941c, raw0, 40); memcpy(ubuf, u0, 40);
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_vic_ctrl_ioctl, T32O_PTR(fileb), cmd, T32O_PTR(ubuf), rnd(), 0);
	in_ioctl = 0;

	if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(t32_data_d941c, &oraw, 40) ||
	    memcmp(ubuf, uo, 40)) {
		printf("t32_vicdev_oracle: FAIL ioctl case %lu cmd %#x sd %#x limit %#x rem %u: ret %#x/%#x hooks %u/%u\n",
		       n, cmd, sd, ulimit, s_rem, rv, ro, vl.n, ol.n);
		for (i = 0; i < vl.n && i < LOGN; i++)
			printf("  %u: %u %#x %#x %#x %s / %u %#x %#x %#x %s\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2], vl.e[i][3],
			       (char *)&vl.e[i][9], ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3], (char *)&ol.e[i][9]);
		return 1;
	}
	cnt[r < 3 ? 0 : r < 6 ? 1 : 2]++;
	return 0;
}

int main(void)
{
	struct t32_vicdev_vma *vma = t32o_alloc(sizeof(*vma));
	unsigned long n, N = 10000, per[2] = { 0 }, ic[3] = { 0 };
	u8 *fileb = t32o_alloc(128), *devb = t32o_alloc(80), *sdb = t32o_alloc(288), *attrb = t32o_alloc(300);
	u32 i;

	ubuf = t32o_alloc(40);
	for (n = 0; n < 20000; n++)
		if (ioctl_case(n, fileb, devb, sdb, attrb, ic))
			return 1;

	for (n = 0; n < N; n++) {
		int fn = (int)(rnd() & 1);
		u8 raw0[40];
		u32 inode = rnd(), file = rnd(), rv, ro, w[10];

		for (i = 0; i < 40; i++) raw0[i] = rnd();
		for (i = 0; i < 10; i++) w[i] = rnd();
		memcpy(vma, w, sizeof(*vma));
		s_rel = rnd(); s_remap = (rnd() & 1) ? 0 : rnd(); s_pr = rnd(); gp_a = rnd(); gp_b = rnd();

		memset(&ol, 0, sizeof(ol)); cur = &ol;
		memcpy(&oraw, raw0, 40);
		ro = fn ? (u32)t32_vic_mmap(&ops, &oraw, T32O_HOST(file), vma)
			: t32_vic_ctrl_release(&ops, &oraw, T32O_HOST(inode), T32O_HOST(file));

		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_d941c, raw0, 40);
		rv = fn ? (u32)t32o_call((t32o_fn)(void (*)(void))tisp_vic_mmap, file, T32O_PTR(vma), rnd(), rnd(), 0)
			: (u32)t32o_call((t32o_fn)(void (*)(void))tisp_vic_ctrl_release, inode, file, rnd(), rnd(), 0);

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || memcmp(t32_data_d941c, &oraw, 40)) {
			printf("t32_vicdev_oracle: FAIL case %lu fn %d: ret %#x/%#x hooks %u/%u\n", n, fn, rv, ro, vl.n, ol.n);
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x %#x %#x / %u %#x %#x %#x %#x %#x\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2],
				       vl.e[i][3], vl.e[i][4], vl.e[i][5], ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3], ol.e[i][4], ol.e[i][5]);
			return 1;
		}
		per[fn]++;
	}
	printf("t32_vicdev_oracle: PASS %lu cases (release %lu, mmap %lu; ioctl GET %lu, SET %lu, other %lu)\n",
	       N + 20000, per[0], per[1], ic[0], ic[1], ic[2]);
	return 0;
}
