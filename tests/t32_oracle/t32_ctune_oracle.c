// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_ctune.h (tisp_code_create_tuning_node,
 * _destroy_tuning_node, tisp_code_tuning_open, _release, _mmap,
 * _poll_init) against the vendor functions (Matt Davis' recovered C, PR #15
 * @ 2e2e06bc).  Per case random state (module parameter 0 / not, major 0 /
 * random, class NULL / set, owner, buffer, record selector 0 / 1, record
 * contents, VMA words), scripted results of alloc_chrdev_region (devt),
 * class_create, remap_pfn_range (0 / error) and kmalloc.  Compared: return
 * values, every OS call in order with its arguments (strings by content,
 * the OS objects as tokens: each side has its own cdev / fops / wait queue),
 * the state words (major, class, buffer, poll flag) and the file operations
 * owner word.  kmalloc failure in open: ours skips the vendor's memset on
 * NULL (vendor UB), counted, not compared.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ctune.h"

#define WK __attribute__((weak))
WK u32 t32_data_d9008[1], t32_data_dadec[1], t32_data_dadac[1], t32_data_dada8[1], t32_data_ce22c[1];
WK u32 t32_data_d087c[16];
WK u8 t32_data_dadb0[64], t32_data_dad94[32], t32_data_dad90[4], t32_data_dada4[4];
WK char t32_data_c7cfc[] = "tisp";					/* data.c:11108 */
WK char t32_data_bf12c[] = "tisp_code_tuning_release";			/* data.c:2158 */
WK char t32_data_c7a58[] = "func is %s, lined is %d\n";			/* data.c:10939 */
WK char t32_data_c7b20[] = "Failed to remap PTE for shared memory\n";	/* data.c:10989 */
WK char t32_data_c7cf0[] = "&dumpQueue";				/* data.c:11105 */
extern u8 t32_small_bss[64];
extern u64 tisp_code_create_tuning_node(u32, u32, u32, u32), tisp_code_destroy_tuning_node(u32, u32, u32, u32),
	   tisp_code_tuning_open(u32, u32, u32, u32), tisp_code_tuning_release(u32, u32, u32, u32),
	   tisp_code_tuning_mmap(u32, u32, u32, u32), tisp_code_tuning_poll_init(u32, u32, u32, u32);

static u32 rs = 0x6a09e667u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 12
#define LW 14
struct hlog { unsigned int n; u32 e[LOGN][LW]; };
static struct hlog vl, ol, *cur;
static void hrec(u32 kind, const u32 *v, unsigned int nv, const char *str)
{
	if (cur->n < LOGN) {
		memset(cur->e[cur->n], 0, sizeof(cur->e[0]));
		cur->e[cur->n][0] = kind;
		memcpy(&cur->e[cur->n][1], v, nv * 4);
		if (str)
			strncpy((char *)&cur->e[cur->n][6], str, 4 * (LW - 6) - 1);
	}
	cur->n++;
}

/* our own OS objects, tokens in the log */
static u32 o_fops[16];
static u8 o_cdev[64], o_wq[32];
static u32 tok(uintptr_t p)
{
	if (p == (uintptr_t)t32_data_dadb0 || p == (uintptr_t)o_cdev) return 0x7001;
	if (p == (uintptr_t)t32_data_d087c || p == (uintptr_t)o_fops) return 0x7002;
	if (p == (uintptr_t)t32_data_dad94 || p == (uintptr_t)o_wq) return 0x7003;
	if (p == (uintptr_t)t32_small_bss) return 0x7004;
	return (u32)p;
}

static u32 s_devt, s_cls, s_remap, s_kbuf, s_ret;
enum { K_REG = 1, K_ALLOC, K_CINIT, K_CADD, K_CLASS, K_DEV, K_CDEL, K_DDEST, K_CDEST, K_UNREG, K_KMALLOC,
       K_KFREE, K_PRINT, K_REMAP, K_WQ };

static u32 c_reg(u32 devt, u32 n, const char *name) { u32 v[2] = { devt, n }; hrec(K_REG, v, 2, name); return s_ret; }
static u32 c_alloc(u32 *devt, u32 first, u32 n, const char *name)
{
	u32 v[2] = { first, n };

	hrec(K_ALLOC, v, 2, name);
	*devt = s_devt;
	return s_ret;
}
static void c_cinit(uintptr_t cdev, uintptr_t fops) { u32 v[2] = { tok(cdev), tok(fops) }; hrec(K_CINIT, v, 2, NULL); }
static u32 c_cadd(uintptr_t cdev, u32 devt, u32 n) { u32 v[3] = { tok(cdev), devt, n }; hrec(K_CADD, v, 3, NULL); return s_ret; }
static u32 c_class(u32 owner, const char *name, uintptr_t key) { u32 v[2] = { owner, tok(key) }; hrec(K_CLASS, v, 2, name); return s_cls; }
static u32 c_dev(u32 cls, u32 parent, u32 devt, u32 drv, const char *fmt)
{
	u32 v[4] = { cls, parent, devt, drv };

	hrec(K_DEV, v, 4, fmt);
	return s_ret;
}
static void c_cdel(uintptr_t cdev) { u32 v[1] = { tok(cdev) }; hrec(K_CDEL, v, 1, NULL); }
static void c_ddest(u32 cls, u32 devt) { u32 v[2] = { cls, devt }; hrec(K_DDEST, v, 2, NULL); }
static void c_cdest(u32 cls) { hrec(K_CDEST, &cls, 1, NULL); }
static u32 c_unreg(u32 devt, u32 n) { u32 v[2] = { devt, n }; hrec(K_UNREG, v, 2, NULL); return s_ret; }
static u32 c_kmalloc(u32 size, u32 fl) { u32 v[2] = { size, fl }; hrec(K_KMALLOC, v, 2, NULL); return s_kbuf; }
static void c_kfree(u32 p) { hrec(K_KFREE, &p, 1, NULL); }
static u32 c_print(u32 level, const char *fmt, const char *s, u32 d)
{
	u32 v[2] = { level, 0 };
	char txt[40];

	snprintf(txt, sizeof(txt), "%s", fmt);
	if (strstr(fmt, "%s")) {	/* the remap error print has no arguments */
		v[1] = d;
		snprintf(txt, sizeof(txt), "%.14s|%s", fmt, s);
	}
	hrec(K_PRINT, v, 2, txt);
	return s_ret;
}
static u32 c_remap(u32 vma, u32 addr, u32 pfn, u32 size, u32 prot)
{
	u32 v[5] = { vma, addr, pfn, size, prot };

	hrec(K_REMAP, v, 5, NULL);
	return s_remap;
}
static void c_wq(uintptr_t wq, const char *name, uintptr_t key) { u32 v[2] = { tok(wq), tok(key) }; hrec(K_WQ, v, 2, name); }

/* vendor stubs */
#define H(a) ((const char *)T32O_HOST(a))
u64 register_chrdev_region(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return c_reg(a0, a1, H(a2)); }
u64 alloc_chrdev_region(u32 a0, u32 a1, u32 a2, u32 a3) { return c_alloc(T32O_HOST(a0), a1, a2, H(a3)); }
u64 cdev_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; c_cinit(a0, a1); return 0; }
u64 cdev_add(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return c_cadd(a0, a1, a2); }
u64 __class_create(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return c_class(a0, H(a1), a2); }
u64 device_create(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return c_dev(a0, a1, a2, a3, H(a4)); }
u64 cdev_del(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; c_cdel(a0); return 0; }
u64 device_destroy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; c_ddest(a0, a1); return 0; }
u64 class_destroy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; c_cdest(a0); return 0; }
u64 unregister_chrdev_region(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return c_unreg(a0, a1); }
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return c_kmalloc(a0, a1); }
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; c_kfree(a0); return 0; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { return c_print(a0, H(a1), H(a2), a3); }
u64 remap_pfn_range(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return c_remap(a0, a1, a2, a3, a4); }
u64 __init_waitqueue_head(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; c_wq(a0, H(a1), a2); return 0; }

/* our hooks */
static u32 o_reg(u32 d, u32 n, const char *s) { return c_reg(d, n, s); }
static void o_cinit(void *c, void *f) { c_cinit((uintptr_t)c, (uintptr_t)f); }
static u32 o_cadd(void *c, u32 d, u32 n) { return c_cadd((uintptr_t)c, d, n); }
static void *o_class(u32 ow, const char *s, void *k) { return T32O_HOST(c_class(ow, s, (uintptr_t)k)); }
static void *o_dev(void *cls, void *p, u32 d, void *drv, const char *f)
{
	return T32O_HOST(c_dev(T32O_PTR(cls), T32O_PTR(p), d, T32O_PTR(drv), f));
}
static void o_cdel(void *c) { c_cdel((uintptr_t)c); }
static void o_ddest(void *cls, u32 d) { c_ddest(T32O_PTR(cls), d); }
static void o_cdest(void *cls) { c_cdest(T32O_PTR(cls)); }
static void *o_kmalloc(u32 s, u32 f) { return T32O_HOST(c_kmalloc(s, f)); }
static void o_kfree(void *p) { c_kfree(T32O_PTR(p)); }
static u32 o_remap(struct t32_ctune_vma *v, u32 a, u32 pfn, u32 s, u32 prot) { return c_remap(T32O_PTR(v), a, pfn, s, prot); }
static void o_wq_init(void *wq, const char *n, void *k) { c_wq((uintptr_t)wq, n, (uintptr_t)k); }
static const struct t32_ctune_ops ops = {
	o_reg, c_alloc, o_cinit, o_cadd, o_class, o_dev, o_cdel, o_ddest, o_cdest, c_unreg,
	o_kmalloc, o_kfree, c_print, o_remap, o_wq_init,
};

int main(void)
{
	struct t32_ctune_vma *vma = t32o_alloc(sizeof(*vma));
	u8 *kbuf = t32o_alloc(T32_CTUNE_BUF_SIZE + 64), *kref = malloc(T32_CTUNE_BUF_SIZE + 64);
	unsigned long n, N = 20000, per[6] = { 0 }, refused = 0;

	for (n = 0; n < N; n++) {
		struct t32_ctune t;
		int fn = (int)(rnd() % 6);
		u32 mparam = (rnd() & 1) ? 0 : rnd(), major = (rnd() & 1) ? 0 : rnd() & 0xfff;
		u32 cls = (rnd() & 1) ? 0 : rnd(), owner = rnd(), buf = rnd(), sel = rnd() & 1;
		u32 poll = rnd() & 0xff, rv = 0, ro = 0, i;
		u8 mem[704];

		s_devt = rnd(); s_cls = rnd(); s_remap = (rnd() & 1) ? 0 : rnd(); s_ret = rnd();
		s_kbuf = (rnd() & 7) ? T32O_PTR(kbuf) : 0;
		for (i = 0; i < 704; i++) mem[i] = rnd();
		{ u32 w[10]; for (i = 0; i < 10; i++) w[i] = rnd(); memcpy(vma, w, sizeof(*vma)); }
		for (i = 0; i < T32_CTUNE_BUF_SIZE + 64; i++) kbuf[i] = rnd();
		memcpy(kref, kbuf, T32_CTUNE_BUF_SIZE + 64);
		for (i = 0; i < 16; i++) o_fops[i] = t32_data_d087c[i] = rnd();
		if (fn == 2 && !s_kbuf) {	/* vendor memset(NULL): not run */
			refused++;
			continue;
		}

		t32o_reset();
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		memcpy(t32_data_da850, mem, 704);	/* t32o_reset clears it */
		t.ops = &ops; t.major_param = mparam; t.major = major; t.cls = T32O_HOST(cls); t.cdev = o_cdev;
		t.fops = o_fops; t.owner = owner; t.lock_key = t32_small_bss; t.buf = T32O_HOST(buf);
		t.mem_sel = sel; t.mem = t32_data_da850; t.poll_ready = poll; t.dump_wq = o_wq;
		switch (fn) {
		case 0: ro = t32_code_create_tuning_node(&t); break;
		case 1: ro = t32_code_destroy_tuning_node(&t); break;
		case 2: ro = t32_code_tuning_open(&t); break;
		case 3: ro = t32_code_tuning_release(&t); break;
		case 4: ro = (u32)t32_code_tuning_mmap(&t, vma); break;
		default: t32_code_tuning_poll_init(&t); break;
		}
		if (fn == 2) {	/* our memset result, then the vendor gets the original bytes back */
			u8 *tmp = malloc(T32_CTUNE_BUF_SIZE + 64);

			memcpy(tmp, kbuf, T32_CTUNE_BUF_SIZE + 64);
			memcpy(kbuf, kref, T32_CTUNE_BUF_SIZE + 64);
			memcpy(kref, tmp, T32_CTUNE_BUF_SIZE + 64);
			free(tmp);
		}

		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		memcpy(t32_data_da850, mem, 704);
		t32_data_d9008[0] = mparam; t32_data_dadec[0] = major; t32_data_dadac[0] = cls; t32_data_ce22c[0] = owner;
		t32_data_dada8[0] = buf; t32_data_dada4[0] = sel; t32_data_dad90[0] = poll;
		{
			t32o_fn f = fn == 0 ? (t32o_fn)(void (*)(void))tisp_code_create_tuning_node :
				    fn == 1 ? (t32o_fn)(void (*)(void))tisp_code_destroy_tuning_node :
				    fn == 2 ? (t32o_fn)(void (*)(void))tisp_code_tuning_open :
				    fn == 3 ? (t32o_fn)(void (*)(void))tisp_code_tuning_release :
				    fn == 4 ? (t32o_fn)(void (*)(void))tisp_code_tuning_mmap :
					      (t32o_fn)(void (*)(void))tisp_code_tuning_poll_init;
			u64 r = t32o_call(f, rnd(), T32O_PTR(vma), rnd(), rnd(), 0);

			if (fn != 5)
				rv = (u32)r;
		}

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || t32_data_dadec[0] != t.major ||
		    t32_data_dadac[0] != T32O_PTR(t.cls) || t32_data_dada8[0] != T32O_PTR(t.buf) ||
		    t32_data_dad90[0] != t.poll_ready || t32_data_d087c[0] != o_fops[0] ||
		    (fn == 2 && memcmp(kbuf, kref, T32_CTUNE_BUF_SIZE + 64))) {
			printf("t32_ctune_oracle: FAIL case %lu fn %d: ret %#x/%#x hooks %u/%u major %#x/%#x cls %#x/%#x buf %#x/%#x\n",
			       n, fn, rv, ro, vl.n, ol.n, t32_data_dadec[0], t.major, t32_data_dadac[0], T32O_PTR(t.cls),
			       t32_data_dada8[0], T32O_PTR(t.buf));
			for (i = 0; i < vl.n && i < LOGN; i++)
				printf("  %u: %u %#x %#x %#x %s / %u %#x %#x %#x %s\n", i, vl.e[i][0], vl.e[i][1], vl.e[i][2],
				       vl.e[i][3], (char *)&vl.e[i][6], ol.e[i][0], ol.e[i][1], ol.e[i][2], ol.e[i][3],
				       (char *)&ol.e[i][6]);
			return 1;
		}
		per[fn]++;
	}
	printf("t32_ctune_oracle: PASS %lu cases (create %lu, destroy %lu, open %lu, release %lu, mmap %lu, poll_init %lu; open with failed kmalloc refused %lu)\n",
	       N - refused, per[0], per[1], per[2], per[3], per[4], per[5], refused);
	return 0;
}
