// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our raw capture by frame index (driver/t32/tx_isp_t32_libvicf.h:
 * lib_tisp_vic_save_frame_index_raw with lib_tisp_vic_set_par / _dma_start / lib_tisp_raw_free of
 * tx_isp_t32_libvicp.h / tx_isp_t32_libvicg.h) against Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15
 * @ 2e2e06bc).
 *
 * Compared bit-exact: every callee call in order (hook, arguments, pointers mapped to the same symbolic address,
 * printk source line and traced values, the formatted snapshot file name), every register write (address, value,
 * order against the calls), the whole .bss block from t32_data_e3f00 (random start values), the kmalloc pool
 * (cleared bytes), channel 0's tuning bytes, the return value.  REFFIX libvicp_fix.sh: recording printk,
 * formatting snprintf, full-size dc074.
 * Inputs: request by index 1 / 0 / random, frame index 0..11 / random, save mode 0..3 / random, VIC config frame
 * counter 0 / random, wait result -ERESTARTSYS / 0 / random, VIC 0 / 1 with random upper bits, raw mode 0..3 and
 * random, fmt 0..3 and random, b7 / b8 0..3 and random,
 * DMA flag 0 / 1 / random, small and random (wrapping) input sizes, kmalloc failure, MDNS buffer smaller / larger
 * than the buffer, fps denominator 0 (vendor divide by zero) and random.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libvicf.h"

u64 lib_tisp_vic_save_frame_index_raw(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define POOL	2048u

u8 t32o_vicblk[0x60] __attribute__((aligned(16)));
__asm__(".globl t32_data_e3f00\n.set t32_data_e3f00, t32o_vicblk\n"
	".globl t32_data_e3f20\n.set t32_data_e3f20, t32o_vicblk + 0x20\n"
	".globl t32_data_e3f28\n.set t32_data_e3f28, t32o_vicblk + 0x28\n"
	".globl t32_data_e3f2c\n.set t32_data_e3f2c, t32o_vicblk + 0x2c\n"
	".globl t32_data_e3f30\n.set t32_data_e3f30, t32o_vicblk + 0x30\n"
	".globl t32_data_e3f34\n.set t32_data_e3f34, t32o_vicblk + 0x34\n"
	".globl t32_data_e3f39\n.set t32_data_e3f39, t32o_vicblk + 0x39\n"
	".globl t32_data_e3f40\n.set t32_data_e3f40, t32o_vicblk + 0x40\n"
	".globl t32_data_e3f44\n.set t32_data_e3f44, t32o_vicblk + 0x44\n"
	".globl t32_data_e3f4c\n.set t32_data_e3f4c, t32o_vicblk + 0x4c\n"
	".globl t32_data_e3f48\n.set t32_data_e3f48, t32o_vicblk + 0x48\n");
u32 t32o7p_data_dc074[132];	/* vendor object: 528 bytes */
u32 t32_data_dd30c[1];
/* printk formats / function names: only handed to printk / isp_printf */
char t32_data_c2e78[4] = "f", t32_data_c2e90[4] = "f", t32_data_cb074[4] = "f", t32_data_cb09c[4] = "f",
     t32_data_cb0b8[4] = "f", t32_data_cb0d8[4] = "f", t32_data_cb0f8[4] = "f", t32_data_cb138[4] = "f",
     t32_data_cb15c[4] = "f", t32_data_cb1a8[4] = "f";
/* data.c:14494..14512: snapshot name formats and the "raw" suffix */
char t32_data_cb1e4[20] = "/tmp/snap%d_long.%s", t32_data_cb1f8[4] = "raw", t32_data_cb1fc[24] = "/tmp/snap%d_short.%s",
     t32_data_cb214[16] = "/tmp/snap%d.%s", t32_data_cb278[12] = "%s_short";
char t32_data_c2e54[4] = "f", t32_data_cb224[4] = "f", t32_data_cb25c[4] = "f", t32_data_cb284[4] = "f";

static u32 rs = 0x7a3c19e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_WR = 1, C_TRACE, C_LOG, C_KMALLOC, C_MALLOCBUF, C_TOP, C_SLEEP, C_MDMA, C_SAVE, C_WAIT, C_KFREE, C_FREEBUF,
       C_MDNS };
struct call { u32 id, a[4], nw; char s[32]; };
#define NC 32
static struct call vc[NC], oc[NC];
static unsigned int vn, on, o_nw;
static u32 ret_mdma, ret_buf, ret_save, alloc_fail, ret_wait, ret_pr, ret_mdns;
static u8 *pool_v, *pool_o;
static struct t32_vic_glob og;

#define TAG_POOL	0xa0000000u

static u32 v_norm(u32 p)
{
	if (pool_v && p >= T32O_PTR(pool_v) && p < T32O_PTR(pool_v) + POOL)
		return TAG_POOL + p - T32O_PTR(pool_v);
	if (p >= T32O_PTR(t32o_vicblk) && p < T32O_PTR(t32o_vicblk) + sizeof(t32o_vicblk))
		return 0xb0000000u + p - T32O_PTR(t32o_vicblk);
	return p;
}
static u32 o_norm(const void *q)
{
	uintptr_t p = (uintptr_t)q;

	if (pool_o && p >= (uintptr_t)pool_o && p < (uintptr_t)pool_o + POOL)
		return TAG_POOL + (u32)(p - (uintptr_t)pool_o);
	if (q == og.par)
		return 0xb0000034u;
	return (u32)p;
}

static void vrec(u32 id, u32 a0, u32 a1, u32 a2, u32 a3, const char *s)
{
	if (vn < NC) {
		vc[vn] = (struct call){ id, { a0, a1, a2, a3 }, t32o_log.n, { 0 } };
		if (s)
			strncpy(vc[vn].s, s, sizeof(vc[vn].s) - 1);
	}
	vn++;
}
static void orec(u32 id, u32 a0, u32 a1, u32 a2, u32 a3, const char *s)
{
	if (on < NC) {
		oc[on] = (struct call){ id, { a0, a1, a2, a3 }, o_nw, { 0 } };
		if (s)
			strncpy(oc[on].s, s, sizeof(oc[on].s) - 1);
	}
	on++;
}

/* vendor side callees (the source line is the third printk argument) */
u64 t32o7_printk(u32 a0, u32 a1, u32 a2, u32 a3, u32 s16, u32 s20)
{
	(void)a0; (void)a1;
	vrec(C_TRACE, a2, a2 == 884 ? 0 : a3, a2 == 694 || a2 == 706 ? s16 : 0, a2 == 706 ? s20 : 0, NULL);
	return ret_pr ^ a2;
}
u64 t32o7_snprintf(u32 a0, u32 a1, u32 a2, u32 a3, u32 s16)
{
	return (u32)snprintf((char *)(uintptr_t)a0, a1, (const char *)(uintptr_t)a2, a3, (const char *)(uintptr_t)s16);
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; vrec(C_LOG, a0, a3, 0, 0, NULL); return 0; }
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	vrec(C_KMALLOC, a0, a1, 0, 0, NULL);
	return alloc_fail || a0 > POOL ? 0 : T32O_PTR(pool_v);
}
u64 isp_malloc_buffer(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_MALLOCBUF, a0, 0, 0, 0, NULL); return ret_buf; }
u64 tisp_top_reg_cfg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_TOP, a0, 0, 0, 0, NULL); return rnd(); }
u64 private_msleep(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_SLEEP, a0, 0, 0, 0, NULL); return rnd(); }
u64 tisp_vic_mdma_enable(u32 a0, u32 a1, u32 a2, u32 a3) { vrec(C_MDMA, v_norm(a0), a1, a2, a3, NULL); return ret_mdma; }
u64 private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	vrec(C_WAIT, a0 == T32O_PTR(t32o_vicblk + 0x4c), a1, 0, 0, NULL);
	return ret_wait;
}
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_KFREE, v_norm(a0), 0, 0, 0, NULL); return rnd(); }
u64 isp_free_buffer(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_FREEBUF, a0, 0, 0, 0, NULL); return rnd(); }
u64 tisp_mdns_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_MDNS, a0, 0, 0, 0, NULL); return ret_mdns; }
u64 lib_tisp_save_file(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	vrec(C_SAVE, v_norm(a0), a1, 0, 0, (const char *)(uintptr_t)a2);
	return ret_save ^ (u32)strlen((const char *)(uintptr_t)a2) ^ a1;
}

/* our hooks */
static u32 o_printk(void *c, u32 line, u32 a) { (void)c; orec(C_TRACE, line, a, 0, 0, NULL); return ret_pr ^ line; }
static u32 o_wait(void *c, void *x, u32 t) { (void)c; orec(C_WAIT, x == (void *)&og.frame_cnt, t, 0, 0, NULL); return ret_wait; }
static void o_kfree(void *c, void *p) { (void)c; orec(C_KFREE, o_norm(p), 0, 0, 0, NULL); }
static void o_freebuf(void *c, u32 d) { (void)c; orec(C_FREEBUF, d, 0, 0, 0, NULL); }
static u32 o_mdns(void *c, u32 a) { (void)c; orec(C_MDNS, a, 0, 0, 0, NULL); return ret_mdns; }
static u32 o_wr(void *c, u32 reg, u32 val) { (void)c; orec(C_WR, reg, val, 0, 0, NULL); o_nw++; return 0; }
static void o_log(void *c, u32 level, u32 line) { (void)c; orec(C_LOG, level, line, 0, 0, NULL); }
static u32 o_top(void *c, u32 a) { (void)c; orec(C_TOP, a, 0, 0, 0, NULL); return 0; }
static u32 o_mdma(void *c, u8 *p, u32 a1, u32 en, u32 d) { (void)c; orec(C_MDMA, o_norm(p), a1, en, d, NULL); return ret_mdma; }
static void o_trace(void *c, u32 line, u32 a, u32 b, u32 d) { (void)c; orec(C_TRACE, line, a, b, d, NULL); }
static void *o_kmalloc(void *c, u32 size, u32 fl)
{
	(void)c;
	orec(C_KMALLOC, size, fl, 0, 0, NULL);
	return alloc_fail || size > POOL ? NULL : pool_o;
}
static u32 o_mallocbuf(void *c, u32 size) { (void)c; orec(C_MALLOCBUF, size, 0, 0, 0, NULL); return ret_buf; }
static void o_sleep(void *c, u32 ms) { (void)c; orec(C_SLEEP, ms, 0, 0, 0, NULL); }
static void *o_to_virt(void *c, u32 dma) { (void)c; return (void *)(uintptr_t)(dma + T32_VIC_KSEG0); }
/* our pool stands for the vendor's: the DMA address the vendor computes from its pool address */
static u32 o_to_dma(void *c, void *p)
{
	(void)c;
	return T32O_PTR(pool_v) + (u32)((u8 *)p - pool_o) + T32_VIC_KSEG0;
}
static u32 o_save(void *c, void *buf, u32 size, const char *name)
{
	(void)c;
	orec(C_SAVE, o_norm(buf), size, 0, 0, name);
	return ret_save ^ (u32)strlen(name) ^ size;
}

static int blk_equal(void)
{
	u32 w[4];

	memcpy(w, t32o_vicblk + 0x28, 12);
	return !memcmp(t32o_vicblk, og.size, 32) && w[0] == og.mdma_a1 && w[1] == og.raw_dma &&
	       (v_norm(w[2]) == o_norm(og.raw_buf) || w[2] == (u32)(uintptr_t)og.raw_buf) && !memcmp(t32o_vicblk + 0x34, og.par, 20) &&
	       t32o_vicblk[0x48] == og.b48;
}

static u32 pick(u32 c, u32 mod, u32 n)
{
	return c % mod == 0 ? rnd() : rnd() % n;
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;
	unsigned int compared = 0, failed = 0;
	struct t32_vicf_hooks h = { { { NULL, NULL, o_wr, NULL, NULL, o_kfree, o_freebuf, o_top, o_mdns, o_mdma, NULL, o_log,
				      NULL }, o_trace, o_kmalloc, o_mallocbuf, o_sleep, o_to_virt, o_to_dma, o_save },
				    o_printk, o_wait, NULL };
	static u8 tun_o[16], cfg_o[2][120];

	for (c = 0; c < n; c++) {
		u32 r, o, cw, vic = (rnd() & ~0xffu) | (rnd() & 1), mode, fmt, b7, b8, dma, sz[8], k, bad;
		struct t32_vicp_env e;
		u8 *tun_v, *cfg_v[2], *req;
		struct t32_vic_raw_req q;
		unsigned int vi, oi;

		t32o_reset();
		vn = on = o_nw = 0;
		memset(vc, 0, sizeof(vc));
		memset(oc, 0, sizeof(oc));
		pool_v = t32o_alloc(POOL);
		pool_o = t32o_alloc(POOL);
		tun_v = t32o_alloc(16);
		for (i = 0; i < POOL; i++)
			pool_v[i] = pool_o[i] = (u8)rnd();
		for (i = 0; i < 16; i++)
			tun_v[i] = tun_o[i] = (u8)rnd();
		t32_data_e1484[0] = T32O_PTR(tun_v);
		h.tuning0 = tun_o;
		for (i = 0; i < sizeof(t32o_vicblk); i++)
			t32o_vicblk[i] = (u8)rnd();
		/* input sizes (width, height per VIC): mostly small so that mode 0 fits the pool, sometimes wrapping */
		for (i = 0; i < 8; i++)
			sz[i] = c % 9 == 0 ? rnd() : rnd() % (i & 1 ? 24 : 80);
		memcpy(t32o_vicblk, sz, 32);
		t32o_vicblk[0x34 + 4] = (u8)(c % 3 == 0 ? rnd() : rnd() % 2);
		{
			u32 w = c % 4 == 0 ? 0 : rnd();
			memcpy(t32o_vicblk + 0x30, &w, 4);
		}
		for (i = 0; i < 132; i++)
			t32o7p_data_dc074[i] = rnd();
		t32o7p_data_dc074[8] = c % 3 == 0 ? rnd() % 4096 : (c % 3 == 1 ? rnd() : 0);
		t32_data_dd30c[0] = c % 10 == 0 ? rnd() & 0xffff : rnd();
		for (i = 0; i < 2; i++) {
			cfg_v[i] = t32o_alloc(120);
			for (k = 0; k < 120; k++)
				cfg_v[i][k] = cfg_o[i][k] = (u8)rnd();
			cw = rnd() % 3 ? 0 : rnd();
			memcpy(cfg_v[i] + 8, &cw, 4);
			memcpy(cfg_o[i] + 8, &cw, 4);
			cw = T32O_PTR(cfg_v[i]);
			memcpy(t32o_vicblk + 0x20 + 4 * i, &cw, 4);
			og.cfg[i] = cfg_o[i];
		}
		memcpy(og.size, t32o_vicblk, 32);
		memcpy(&og.mdma_a1, t32o_vicblk + 0x28, 4);
		memcpy(&og.raw_dma, t32o_vicblk + 0x2c, 4);
		{
			u32 w;
			memcpy(&w, t32o_vicblk + 0x30, 4);
			og.raw_buf = (void *)(uintptr_t)w;
		}
		memcpy(og.par, t32o_vicblk + 0x34, 20);
		og.b48 = t32o_vicblk[0x48];
		og.b49 = t32o_vicblk[0x49];
		memcpy(&og.frame_cnt, t32o_vicblk + 0x4c, 4);
		memcpy(og.wq, t32o_vicblk + 0x50, 16);
		e.tuning0 = tun_o;
		e.mdns_dma = t32o7p_data_dc074[4];
		e.mdns_size = t32o7p_data_dc074[8];
		e.fps = t32_data_dd30c[0];
		ret_mdma = rnd();
		ret_buf = rnd();
		ret_save = rnd();
		alloc_fail = c % 17 == 0;
		mode = (rnd() & ~0xffu) | pick(c, 13, 5);
		fmt = (rnd() & ~0xffu) | pick(c, 11, 4);
		b7 = (rnd() & ~0xffu) | pick(c, 7, 4);
		b8 = (rnd() & ~0xffu) | pick(c, 6, 4);
		dma = rnd();
		(void)fmt; (void)dma;
		ret_wait = c % 4 == 0 ? (u32)-512 : (c % 4 == 1 ? 0 : rnd());
		ret_pr = rnd();
		ret_mdns = rnd();
		req = t32o_alloc(sizeof(q));
		for (i = 0; i < sizeof(q); i++)
			req[i] = (u8)rnd();
		req[(rnd() % 120) + 4] = 0;	/* the request name */
		req[127] = 0;
		req[128] = (u8)pick(c, 11, 4);
		memcpy(req + 132, (u32[]){ c % 9 == 0 ? rnd() : rnd() % 4 }, 4);
		memcpy(req + 136, (u32[]){ c % 7 == 0 ? rnd() : (c % 7 < 4 ? 1 : 0) }, 4);
		req[141] = (u8)mode;
		req[142] = (u8)b7;
		req[143] = (u8)b8;
		req[144] = (u8)(c % 10 == 0 ? rnd() : rnd() % 12);
		memcpy(&q, req, sizeof(q));

		r = (u32)t32o_call(F(lib_tisp_vic_save_frame_index_raw), vic, T32O_PTR(req), rnd(), rnd(), 0);
		o = t32_lib_vic_save_frame_index_raw(&h, &og, &e, vic, &q);
		bad = r != o;
		for (vi = oi = 0; !bad && (vi < vn || oi < on);) {
			if (oi < on && oc[oi].id == C_WR) {
				oi++;
				continue;
			}
			if (vi >= vn || oi >= on || memcmp(&vc[vi], &oc[oi], sizeof(vc[0])))
				bad = 1;
			vi++;
			oi++;
		}
		for (i = 0, k = 0; !bad && i < on && i < NC; i++)
			if (oc[i].id == C_WR) {
				bad = k >= t32o_log.n || t32o_log.w[k].reg != oc[i].a[0] || t32o_log.w[k].val != oc[i].a[1];
				k++;
			}
		bad |= k != t32o_log.n || !blk_equal() || memcmp(cfg_v[0], cfg_o[0], 120) || memcmp(cfg_v[1], cfg_o[1], 120) ||
		       t32o_vicblk[0x49] != og.b49 || memcmp(t32o_vicblk + 0x4c, &og.frame_cnt, 4) || memcmp(pool_v, pool_o, POOL) || memcmp(tun_v, tun_o, 16) ||
		       vn > NC || on > NC;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u vic %x mode %x: ret %x/%x calls %u/%u writes %u\n", c, vic, mode & 0xff, r, o,
			       vn, on, t32o_log.n);
	}
	printf("t32_libvicf_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
