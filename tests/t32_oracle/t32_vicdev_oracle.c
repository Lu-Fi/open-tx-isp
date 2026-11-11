// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_vicdev.h (tisp_vic_ctrl_release,
 * tisp_vic_mmap) against the vendor functions (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc).  Per case random raw state (40 B), VMA words, inode /
 * file values, scripted results (single_release, remap 0 / error) and a
 * get_par stub that rewrites address / memMode deterministically.
 * Compared: return values, every call in order with its arguments (raw
 * state pointers as tokens, print texts by content), the raw state bytes.
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
extern u64 tisp_vic_ctrl_release(u32, u32, u32, u32), tisp_vic_mmap(u32, u32, u32, u32);

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
static u32 s_rel, s_remap, s_pr, gp_a, gp_b;

static void c_free(u32 a, u32 b) { u32 v[2] = { a, b }; hrec(1, v, 2, NULL); }
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

	snprintf(t, sizeof(t), "%.30s", fmt);
	if (level == 1) {
		v[1] = line; v[2] = p; v[3] = va; v[4] = mm;
		snprintf(t, sizeof(t), "%.30s|%s", fmt, fn);
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
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6) { return c_print(a0, H(a1), H(a2), a3, a4, a5, a6); }
u64 remap_pfn_range(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return c_remap(a0, a1, a2, a3, a4); }

static void o_free(void *a, void *b) { c_free(T32O_PTR(a), T32O_PTR(b)); }
static u32 o_rel(void *a, void *b) { return c_rel(T32O_PTR(a), T32O_PTR(b)); }
static u32 o_remap(struct t32_vicdev_vma *v, u32 a, u32 pfn, u32 sz, u32 prot) { return c_remap(T32O_PTR(v), a, pfn, sz, prot); }
static const struct t32_vicdev_ops ops = { o_free, o_rel, c_set, c_get, c_print, o_remap };

int main(void)
{
	struct t32_vicdev_vma *vma = t32o_alloc(sizeof(*vma));
	unsigned long n, N = 10000, per[2] = { 0 };
	u32 i;

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
	printf("t32_vicdev_oracle: PASS %lu cases (release %lu, mmap %lu)\n", N, per[0], per[1]);
	return 0;
}
