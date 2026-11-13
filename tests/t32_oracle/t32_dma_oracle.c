// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_dma.h (tisp_func_isp_reset, tisp_dma_init,
 * tisp_func_ldc_dma_cfg, tisp_func_ldc_dma_stride, tisp_dma_read_file,
 * tisp_dma_rdma_isp_in_cfg) against the vendor functions (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc).  Random cases with fixed seed; per case
 * one function with random arguments, scripted register reads (0x204 done
 * after 0..6 polls, random 0x20), random LDC record of channel 0, kmalloc
 * failing in 1/4 of the cases, random fs / filp_close values.  Compared:
 * return value, every register write (address, value, order), every hook
 * call in order with its arguments (reads with the write count at that
 * point, IRQ table entries with the handler, kmalloc size / flags, the full
 * 128-byte path of filp_open, get_fs / set_fs, vfs_read file / buffer /
 * length / position, filp_close, printf level and text) and the state words
 * (t32_data_e3140, t32_data_e3154).  The vendor functions call
 * system_reg_write directly, so t32_data_d03a0 does not matter (not varied).
 *
 * t32_data_dc074 is only 8 words in env.c; the vendor reads +56 / +72 of it:
 * the oracle writes those words through the symbol and restores them after
 * the case.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_dma.h"

#define WK __attribute__((weak))
WK u32 t32_data_e3140[2];			/* .bss: dma_init config words */
WK u32 t32_data_e3154[1];			/* .bss: RDMA input buffer (physical) */
/* data.c:3047-3050 "/tmp/dmaInData" */
WK u32 t32_data_bff10[4] = { 0x706d742fu, 0x616d642fu, 0x61446e49u, 0x00006174u };
/* data.c:13566-13575 "not enough mem space for vic dma~~~\n" */
WK u32 t32_data_ca364[10] = { 0x20746f6eu, 0x756f6e65u, 0x6d206867u, 0x73206d65u, 0x65636170u,
			     0x726f6620u, 0x63697620u, 0x616d6420u, 0x0a7e7e7eu, 0 };
extern u8 t32o_dc074_raw[] __asm__("t32_data_dc074");
extern u64 system_reg_write(u32, u32, u32, u32), system_reg_read(u32, u32, u32, u32);

extern u64 tisp_func_isp_reset(u32, u32, u32, u32), tisp_dma_init(u32, u32, u32, u32),
	   tisp_func_ldc_dma_cfg(u32, u32, u32, u32), tisp_func_ldc_dma_stride(u32, u32, u32, u32),
	   tisp_dma_read_file(u32, u32, u32, u32), tisp_dma_rdma_isp_in_cfg(u32, u32, u32, u32);

static u32 rs = 0x7d3a11c5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* hook log, the same shape for both sides */
#define LOGN 64
#define LW 36
struct hlog { unsigned int n; u32 e[LOGN][LW]; };
static struct hlog vl, ol, *cur;
static void hrec(u32 kind, const u32 *v, unsigned int nv)
{
	if (cur->n < LOGN) {
		memset(cur->e[cur->n], 0, sizeof(cur->e[0]));
		cur->e[cur->n][0] = kind;
		memcpy(&cur->e[cur->n][1], v, nv * 4);
	}
	cur->n++;
}
enum { K_READ = 1, K_IRQ, K_KMALLOC, K_PRINT, K_OPEN, K_GETFS, K_SETFS, K_VREAD, K_CLOSE };

/* per-case script */
static u32 done_after, polls, reg20, kbuf, fsval, close_ret, print_ret, file_addr;

static u32 rd_script(u32 reg)
{
	u32 v = reg == T32_DMA_REG_START ? reg20 : (rnd() & ~1u);
	u32 rec[3] = { reg, t32o_log.n, 0 };

	if (reg == T32_DMA_REG_RESET_DONE && ++polls > done_after)
		v |= 1;
	rec[2] = v;
	hrec(K_READ, rec, 3);
	return v;
}

/* handlers the IRQ table gets */
u64 tisp_wdma_fra_down_interrupt(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 1; }
u64 tisp_rdma_fra_down_interrupt(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 2; }

/* vendor OS stubs (strong: the env.c ones are weak) */
u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[3] = { a0, a1, a2 };

	(void)a3;
	hrec(K_IRQ, v, 3);
	return 0;
}
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[2] = { a0, a1 };

	(void)a2; (void)a3;
	hrec(K_KMALLOC, v, 2);
	return kbuf;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[17] = { a0 };

	(void)a2; (void)a3;
	strncpy((char *)&v[1], (const char *)T32O_HOST(a1), 63);
	hrec(K_PRINT, v, 17);
	return print_ret;
}
u64 private_filp_open(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[34] = { a1, a2 };

	(void)a3;
	memcpy(&v[2], T32O_HOST(a0), T32_DMA_PATH_LEN);
	hrec(K_OPEN, v, 34);
	return file_addr;
}
u64 private_get_fs(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	hrec(K_GETFS, NULL, 0);
	*(u32 *)T32O_HOST(a0) = fsval;	/* mm_segment_t returned through the hidden pointer */
	return a0;
}
u64 private_set_fs(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	hrec(K_SETFS, &a0, 1);
	return 0;
}
u64 private_vfs_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[4] = { a0, a1, a2, a3 };

	hrec(K_VREAD, v, 4);
	return a2;
}
u64 private_filp_close(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 v[2] = { a0, a1 };

	(void)a2; (void)a3;
	hrec(K_CLOSE, v, 2);
	return close_ret;
}

/* our hooks */
static u32 o_write(u32 reg, u32 val) { return (u32)system_reg_write(reg, val, 0, 0); }
static u32 o_read(u32 reg) { return (u32)system_reg_read(reg, 0, 0, 0); }
static void o_irq(u32 ch, u32 irq, void *h) { system_irq_func_set(ch, irq, T32O_PTR(h), 0); }
static void *o_kmalloc(u32 size, u32 flags) { return T32O_HOST(private_kmalloc(size, flags, 0, 0)); }
static u32 o_printf(u32 level, const char *msg) { return (u32)isp_printf(level, T32O_PTR(msg), 0, 0); }
static void *o_open(const char *p, u32 fl, u32 mode)	/* p is on the host stack: logged here */
{
	u32 v[34] = { fl, mode };

	memcpy(&v[2], p, T32_DMA_PATH_LEN);
	hrec(K_OPEN, v, 34);
	return T32O_HOST(file_addr);
}
static u32 *fsbox;
static u32 o_get_fs(void) { private_get_fs(T32O_PTR(fsbox), 0, 0, 0); return *fsbox; }
static void o_set_fs(u32 fs) { private_set_fs(fs, 0, 0, 0); }
static void o_vread(void *f, void *b, u32 len, void *pos) { private_vfs_read(T32O_PTR(f), T32O_PTR(b), len, T32O_PTR(pos)); }
static u32 o_close(void *f, u32 id) { return (u32)private_filp_close(T32O_PTR(f), id, 0, 0); }

static const struct t32_dma_ops ops = {
	o_write, o_read, o_irq, o_kmalloc, o_printf, o_open, o_get_fs, o_set_fs, o_vread, o_close,
};

static unsigned long cases, kfail, fails;

static int cmp_case(int fn, u32 rv, u32 ro, const u32 *wsave, unsigned int wn, const struct t32_dma_dev *dev)
{
	unsigned int i;

	if (rv != ro) { printf("fn %d: return vendor %#x ours %#x\n", fn, rv, ro); return 1; }
	if (wn != t32o_log.n) { printf("fn %d: writes vendor %u ours %u\n", fn, t32o_log.n, wn); return 1; }
	for (i = 0; i < wn && i < T32O_MAX_WRITES; i++)
		if (t32o_log.w[i].reg != wsave[2 * i] || t32o_log.w[i].val != wsave[2 * i + 1]) {
			printf("fn %d: write %u vendor %#x=%#x ours %#x=%#x\n", fn, i, t32o_log.w[i].reg,
			       t32o_log.w[i].val, wsave[2 * i], wsave[2 * i + 1]);
			return 1;
		}
	if (vl.n != ol.n) { printf("fn %d: hooks vendor %u ours %u\n", fn, vl.n, ol.n); return 1; }
	for (i = 0; i < vl.n && i < LOGN; i++)
		if (memcmp(vl.e[i], ol.e[i], sizeof(vl.e[0]))) {
			printf("fn %d: hook %u kind vendor %u ours %u (%#x/%#x %#x/%#x %#x/%#x)\n", fn, i,
			       vl.e[i][0], ol.e[i][0], vl.e[i][1], ol.e[i][1], vl.e[i][2], ol.e[i][2],
			       vl.e[i][3], ol.e[i][3]);
			return 1;
		}
	if (t32_data_e3140[0] != dev->cfg[0] || t32_data_e3140[1] != dev->cfg[1] ||
	    t32_data_e3154[0] != dev->rdma_phys) {
		printf("fn %d: state vendor %#x %#x %#x ours %#x %#x %#x\n", fn, t32_data_e3140[0],
		       t32_data_e3140[1], t32_data_e3154[0], dev->cfg[0], dev->cfg[1], dev->rdma_phys);
		return 1;
	}
	return 0;
}

static u32 wsave[2 * 64];
static u8 dcsave[20];

int main(void)
{
	u32 *cfgw = t32o_alloc(8), *kmem = t32o_alloc(64), *fmem = t32o_alloc(64), *ubuf = t32o_alloc(16);
	unsigned long n, N = 40000, per[6] = { 0 };

	fsbox = t32o_alloc(4);
	memcpy(dcsave, t32o_dc074_raw + 56, sizeof(dcsave));
	for (n = 0; n < N; n++) {
		struct t32_dma_dev dev;
		int fn = (int)(rnd() % 6);
		u32 a0 = rnd(), a1 = rnd(), a2 = rnd(), a3 = rnd(), rv, ro = 0, st[3], dbuf, dtot;
		unsigned int wn, i;

		if (n < 64) {	/* boundaries: zero / full 16-bit sizes */
			u32 e = (n & 1) ? 0xffffffffu : 0;
			a0 = (n & 2) ? e : a0; a1 = (n & 4) ? e : a1; a2 = (n & 8) ? e : a2; a3 = (n & 16) ? e : a3;
		}
		done_after = rnd() % 7;
		reg20 = rnd();
		kbuf = (rnd() & 3) ? T32O_PTR(kmem) : 0;
		fsval = rnd();
		close_ret = rnd();
		print_ret = rnd();
		file_addr = (rnd() & 7) ? T32O_PTR(fmem) : (u32)-2;	/* an error pointer too (not checked) */
		dbuf = rnd(); dtot = rnd();
		st[0] = rnd(); st[1] = rnd(); st[2] = rnd();
		cfgw[0] = rnd(); cfgw[1] = rnd();

		/* ours */
		t32o_reset();
		t32o_rd_hook = rd_script;
		memset(&ol, 0, sizeof(ol)); cur = &ol; polls = 0;
		memcpy(t32o_dc074_raw + 56, &dbuf, 4);
		memcpy(t32o_dc074_raw + 72, &dtot, 4);
		dev.ops = &ops;
		dev.wdma_irq = (void *)tisp_wdma_fra_down_interrupt;
		dev.rdma_irq = (void *)tisp_rdma_fra_down_interrupt;
		dev.ldc0 = (const struct t32_dma_ldc_rec *)t32o_dc074_raw;
		dev.cfg[0] = st[0]; dev.cfg[1] = st[1]; dev.rdma_phys = st[2];
		{ u32 s = rs;	/* same random register reads on both sides */
		switch (fn) {
		case 0: ro = t32_func_isp_reset(&dev); break;
		case 1: ro = t32_dma_init(&dev, a0, cfgw); break;
		case 2: ro = t32_func_ldc_dma_cfg(&dev, a0, a1, a2, a3); break;
		case 3: ro = t32_func_ldc_dma_stride(&dev, a0); break;
		case 4: ro = t32_dma_read_file(&dev, ubuf, a1); break;
		default: ro = t32_dma_rdma_isp_in_cfg(&dev, a0, a1, a2); break;
		}
		wn = t32o_log.n;
		for (i = 0; i < wn && i < 64; i++) { wsave[2 * i] = t32o_log.w[i].reg; wsave[2 * i + 1] = t32o_log.w[i].val; }

		/* vendor */
		t32o_reset();
		t32o_rd_hook = rd_script;
		memset(&vl, 0, sizeof(vl)); cur = &vl; polls = 0; rs = s;
		t32_data_e3140[0] = st[0]; t32_data_e3140[1] = st[1]; t32_data_e3154[0] = st[2];
		memcpy(t32o_dc074_raw + 56, &dbuf, 4);
		memcpy(t32o_dc074_raw + 72, &dtot, 4);
		switch (fn) {
		case 0: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_func_isp_reset, a0, a1, a2, a3, 0); break;
		case 1: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_dma_init, a0, T32O_PTR(cfgw), a2, a3, 0); break;
		case 2: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_func_ldc_dma_cfg, a0, a1, a2, a3, 0); break;
		case 3: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_func_ldc_dma_stride, a0, a1, a2, a3, 0); break;
		case 4: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_dma_read_file, T32O_PTR(ubuf), a1, a2, a3, 0); break;
		default: rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_dma_rdma_isp_in_cfg, a0, a1, a2, a3, 0); break;
		}
		}
		t32o_rd_hook = NULL;
		memcpy(t32o_dc074_raw + 56, dcsave, sizeof(dcsave));
		if (fn == 5 && !kbuf)
			kfail++;
		if (cmp_case(fn, rv, ro, wsave, wn, &dev)) {
			printf("t32_dma_oracle: FAIL case %lu fn %d args %#x %#x %#x %#x\n", n, fn, a0, a1, a2, a3);
			fails++;
			return 1;
		}
		per[fn]++;
		cases++;
	}
	printf("t32_dma_oracle: PASS %lu cases (reset %lu, init %lu, ldc_cfg %lu, ldc_stride %lu, read_file %lu, rdma_in %lu, kmalloc fail %lu)\n",
	       cases, per[0], per[1], per[2], per[3], per[4], per[5], kfail);
	return 0;
}
