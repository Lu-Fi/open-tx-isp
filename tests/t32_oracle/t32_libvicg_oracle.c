// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our VIC glue (driver/t32/tx_isp_t32_libvicg.h: lib_tisp_vic_init / _deinit / _reset /
 * _dma_start, lib_tisp_raw_free) against Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every callee call in order (hook, arguments; pointers mapped to the same symbolic
 * address on both sides), every register write (address, value, order) and read (address, order), the whole
 * .bss block from t32_data_e3f00 (random start values), the vmalloc'd config, channel 0's tuning bytes, the
 * return value where the vendor has one (vic_init, vic_reset, vic_dma_start, raw_free mode 2).
 * Inputs: random block, VIC 0 / 1 (random upper bits), vmalloc failure, DMA flag word 0 / random, par bytes
 * 4 / 8 = 0..3 and random, raw mode 0..3 and random, raw pointer 0 / random.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libvicg.h"

u64 lib_tisp_vic_init(u32, u32, u32, u32);
u64 lib_tisp_vic_deinit(u32, u32, u32, u32);
u64 lib_tisp_vic_reset(u32, u32, u32, u32);
u64 lib_tisp_vic_dma_start(u32, u32, u32, u32);
u64 lib_tisp_raw_free(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

/* the vendor .bss block t32_data_e3f00 .. e3f60 */
u8 t32o_vicblk[0x60] __attribute__((aligned(16)));
__asm__(".globl t32_data_e3f00\n.set t32_data_e3f00, t32o_vicblk\n"
	".globl t32_data_e3f20\n.set t32_data_e3f20, t32o_vicblk + 0x20\n"
	".globl t32_data_e3f28\n.set t32_data_e3f28, t32o_vicblk + 0x28\n"
	".globl t32_data_e3f2c\n.set t32_data_e3f2c, t32o_vicblk + 0x2c\n"
	".globl t32_data_e3f30\n.set t32_data_e3f30, t32o_vicblk + 0x30\n"
	".globl t32_data_e3f34\n.set t32_data_e3f34, t32o_vicblk + 0x34\n"
	".globl t32_data_e3f39\n.set t32_data_e3f39, t32o_vicblk + 0x39\n"
	".globl t32_data_e3f48\n.set t32_data_e3f48, t32o_vicblk + 0x48\n"
	".globl t32_data_e3f4c\n.set t32_data_e3f4c, t32o_vicblk + 0x4c\n"
	".globl t32_data_e3f50\n.set t32_data_e3f50, t32o_vicblk + 0x50\n");
/* printk format / function name / wait queue name strings: only handed to isp_printf / __init_waitqueue_head */
char t32_data_c2ed8[4] = "f", t32_data_cafcc[4] = "f", t32_data_cb000[4] = "f", t32_data_cb018[4] = "f",
     t32_data_cb02c[4] = "f";

static u32 rs = 0x2545f491u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_RD = 1, C_WR, C_VMALLOC, C_VFREE, C_KFREE, C_FREEBUF, C_TOP, C_MDNS, C_MDMA, C_CFG, C_LOG, C_WQ };
struct call { u32 id, a[4], nw; };
#define NC 32
static struct call vc[NC], oc[NC];
static unsigned int vn, on, o_nw;
static u32 rdseed, ret_cfg, ret_mdma, ret_mdns, alloc_fail;
static u8 *pool_v, *pool_o, *attr_v, *attr_o;
static struct t32_vic_glob og;

#define TAG_BLK		0xb0000000u
#define TAG_POOL	0xa0000000u
#define TAG_ATTR	0xc0000000u

static u32 v_norm(u32 p)
{
	if (p >= T32O_PTR(t32o_vicblk) && p < T32O_PTR(t32o_vicblk) + sizeof(t32o_vicblk))
		return TAG_BLK + p - T32O_PTR(t32o_vicblk);
	if (pool_v && p >= T32O_PTR(pool_v) && p < T32O_PTR(pool_v) + 512)
		return TAG_POOL + p - T32O_PTR(pool_v);
	if (attr_v && p >= T32O_PTR(attr_v) && p < T32O_PTR(attr_v) + 256)
		return TAG_ATTR + p - T32O_PTR(attr_v);
	return p;
}

static u32 o_norm(const void *q)
{
	uintptr_t p = (uintptr_t)q;

	if (q == og.par)
		return TAG_BLK + 0x34;
	if (q == og.wq)
		return TAG_BLK + 0x50;
	if (p >= (uintptr_t)&og && p < (uintptr_t)&og + sizeof(og))
		return TAG_BLK + 0xfff;
	if (pool_o && p >= (uintptr_t)pool_o && p < (uintptr_t)pool_o + 512)
		return TAG_POOL + (u32)(p - (uintptr_t)pool_o);
	if (attr_o && p >= (uintptr_t)attr_o && p < (uintptr_t)attr_o + 256)
		return TAG_ATTR + (u32)(p - (uintptr_t)attr_o);
	return (u32)p;
}

static void vrec(u32 id, u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (vn < NC)
		vc[vn] = (struct call){ id, { a0, a1, a2, a3 }, t32o_log.n };
	vn++;
}
static void orec(u32 id, u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (on < NC)
		oc[on] = (struct call){ id, { a0, a1, a2, a3 }, o_nw };
	on++;
}
static u32 rdval(u32 reg) { return (reg * 0x9e3779b1u) ^ rdseed; }

/* vendor side callees */
static u32 v_rd(u32 reg) { vrec(C_RD, reg, 0, 0, 0); return rdval(reg); }
u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	vrec(C_VMALLOC, a0, 0, 0, 0);
	return alloc_fail ? 0 : T32O_PTR(pool_v);
}
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_VFREE, v_norm(a0), 0, 0, 0); return rnd(); }
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_KFREE, v_norm(a0), 0, 0, 0); return rnd(); }
u64 isp_free_buffer(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_FREEBUF, a0, 0, 0, 0); return rnd(); }
u64 tisp_top_reg_cfg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_TOP, a0, 0, 0, 0); return rnd(); }
u64 tisp_mdns_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_MDNS, a0, 0, 0, 0); return ret_mdns; }
u64 tisp_vic_mdma_enable(u32 a0, u32 a1, u32 a2, u32 a3) { vrec(C_MDMA, v_norm(a0), a1, a2, a3); return ret_mdma; }
u64 lib_tisp_vic_cfg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; vrec(C_CFG, a0, v_norm(a1), 0, 0); return ret_cfg; }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; vrec(C_LOG, a0, a3, 0, 0); return 0; }
u64 __init_waitqueue_head(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vrec(C_WQ, v_norm(a0), 0, 0, 0); return 0; }

/* our hooks */
static u32 o_rd(void *c, u32 reg) { (void)c; orec(C_RD, reg, 0, 0, 0); return rdval(reg); }
static u32 o_wr(void *c, u32 reg, u32 val) { (void)c; orec(C_WR, reg, val, 0, 0); o_nw++; return 0; }
static void *o_vmalloc(void *c, u32 size) { (void)c; orec(C_VMALLOC, size, 0, 0, 0); return alloc_fail ? NULL : pool_o; }
static void o_vfree(void *c, void *p) { (void)c; orec(C_VFREE, o_norm(p), 0, 0, 0); }
static void o_kfree(void *c, void *p) { (void)c; orec(C_KFREE, o_norm(p), 0, 0, 0); }
static void o_freebuf(void *c, u32 d) { (void)c; orec(C_FREEBUF, d, 0, 0, 0); }
static u32 o_top(void *c, u32 a) { (void)c; orec(C_TOP, a, 0, 0, 0); return 0; }
static u32 o_mdns(void *c, u32 a) { (void)c; orec(C_MDNS, a, 0, 0, 0); return ret_mdns; }
static u32 o_mdma(void *c, u8 *p, u32 a1, u32 en, u32 d) { (void)c; orec(C_MDMA, o_norm(p), a1, en, d); return ret_mdma; }
static u32 o_cfg(void *c, u32 ch, void *attr) { (void)c; orec(C_CFG, ch, o_norm(attr), 0, 0); return ret_cfg; }
static void o_log(void *c, u32 level, u32 line) { (void)c; orec(C_LOG, level, line, 0, 0); }
static void o_wq(void *c, void *wq) { (void)c; orec(C_WQ, o_norm(wq), 0, 0, 0); }

/* every recorded call carries the number of register writes before it (vendor: t32o_log.n), so the order of
 * the writes against the reads and other callees is compared too */
static unsigned int compared, failed;

static int blk_equal(void)
{
	u32 w[4];
	unsigned int i;

	memcpy(w, t32o_vicblk + 0x20, 16);
	if (memcmp(t32o_vicblk, og.size, 32))
		return 0;
	for (i = 0; i < 2; i++)
		if (v_norm(w[i]) != o_norm(og.cfg[i]))
			return 0;
	return w[2] == og.mdma_a1 && w[3] == og.raw_dma && v_norm(*(u32 *)(t32o_vicblk + 0x30)) == o_norm(og.raw_buf) &&
	       !memcmp(t32o_vicblk + 0x34, og.par, 20) && t32o_vicblk[0x48] == og.b48 &&
	       *(u32 *)(t32o_vicblk + 0x4c) == og.frame_cnt && !memcmp(t32o_vicblk + 0x50, og.wq, 16);
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_vic_hooks h = { NULL, o_rd, o_wr, o_vmalloc, o_vfree, o_kfree, o_freebuf, o_top, o_mdns, o_mdma,
				   o_cfg, o_log, o_wq };
	static u8 tun_o[16];

	t32o_rd_hook = v_rd;
	for (c = 0; c < n; c++) {
		u32 fn = c % 5, r = 0, o = 0, ch = (rnd() & ~0xffu) | (rnd() & 1), sz[4], w, k, bad;
		u8 *tun_v, *szv;
		int has_ret = 1;
		unsigned int vi, oi;

		t32o_reset();
		vn = on = o_nw = 0;
		memset(vc, 0, sizeof(vc));
		memset(oc, 0, sizeof(oc));
		pool_v = t32o_alloc(512);
		pool_o = t32o_alloc(512);
		attr_v = t32o_alloc(256);
		attr_o = t32o_alloc(256);
		tun_v = t32o_alloc(16);
		szv = t32o_alloc(16);
		for (i = 0; i < 512; i++)
			pool_v[i] = pool_o[i] = (u8)rnd();
		for (i = 0; i < 256; i++)
			attr_v[i] = attr_o[i] = (u8)rnd();
		if (c % 3 == 0)
			memset(attr_v + 148, 0, 4), memset(attr_o + 148, 0, 4);
		for (i = 0; i < 16; i++)
			tun_v[i] = tun_o[i] = (u8)rnd();
		for (i = 0; i < 4; i++)
			sz[i] = rnd();
		memcpy(szv, sz, 16);
		t32_data_e1484[0] = T32O_PTR(tun_v);
		for (i = 0; i < sizeof(t32o_vicblk); i++)
			t32o_vicblk[i] = (u8)rnd();
		/* config pointers: 0 or pool blocks; raw pointer 0 or random */
		for (i = 0; i < 2; i++) {
			k = rnd() % 3;
			w = k == 0 ? 0 : T32O_PTR(pool_v) + 128 * (i + 1);
			memcpy(t32o_vicblk + 0x20 + 4 * i, &w, 4);
			og.cfg[i] = k == 0 ? NULL : pool_o + 128 * (i + 1);
		}
		w = c % 4 == 0 ? 0 : (rnd() | 1);
		memcpy(t32o_vicblk + 0x30, &w, 4);
		og.raw_buf = (void *)(uintptr_t)w;
		t32o_vicblk[0x34 + 4] = c % 7 == 6 ? (u8)rnd() : (u8)(rnd() % 3);
		t32o_vicblk[0x34 + 8] = c % 7 == 5 ? (u8)rnd() : (u8)(rnd() % 4);
		t32o_vicblk[0x34 + 5] = c % 11 == 10 ? (u8)rnd() : (u8)(rnd() % 4);
		t32o_vicblk[0x34 + 6] = (u8)(rnd() & 1);
		memcpy(og.size, t32o_vicblk, 32);
		memcpy(&og.mdma_a1, t32o_vicblk + 0x28, 4);
		memcpy(&og.raw_dma, t32o_vicblk + 0x2c, 4);
		memcpy(og.par, t32o_vicblk + 0x34, 20);
		og.b48 = t32o_vicblk[0x48];
		memcpy(&og.frame_cnt, t32o_vicblk + 0x4c, 4);
		memcpy(og.wq, t32o_vicblk + 0x50, 16);
		rdseed = rnd();
		ret_cfg = rnd();
		ret_mdma = rnd();
		ret_mdns = rnd();
		alloc_fail = c % 13 == 0;

		switch (fn) {
		case 0:
			r = (u32)t32o_call(F(lib_tisp_vic_init), ch, T32O_PTR(attr_v), T32O_PTR(szv), rnd(), 0);
			o = t32_lib_vic_init(&h, &og, ch, attr_o, sz);
			break;
		case 1:
			t32o_call(F(lib_tisp_vic_deinit), rnd(), 0, 0, 0, 0);
			t32_lib_vic_deinit(&h, &og);
			has_ret = 0;
			break;
		case 2:
			r = (u32)t32o_call(F(lib_tisp_vic_reset), rnd(), 0, 0, 0, 0);
			o = t32_lib_vic_reset(&h);
			break;
		case 3:
			r = (u32)t32o_call(F(lib_tisp_vic_dma_start), rnd(), 0, 0, 0, 0);
			o = t32_lib_vic_dma_start(&h, &og);
			break;
		default:
			r = (u32)t32o_call(F(lib_tisp_raw_free), rnd(), 0, 0, 0, 0);
			o = t32_lib_raw_free(&h, &og, tun_o);
			has_ret = og.par[T32_VIC_PAR_RAW_MODE] == 2;
			break;
		}
		/* ours: skip our C_WR entries (the vendor writes are in t32o_log, compared below in order) */
		bad = has_ret && r != o;
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
		bad |= k != t32o_log.n || !blk_equal() || memcmp(pool_v, pool_o, 512) || memcmp(tun_v, tun_o, 16) ||
		       memcmp(attr_v, attr_o, 256) || vn > NC || on > NC;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH fn %u case %u ch %x: ret %x/%x calls %u/%u writes %u\n", fn, c, ch, r, o, vn, on,
			       t32o_log.n);
	}
	printf("t32_libvicg_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
