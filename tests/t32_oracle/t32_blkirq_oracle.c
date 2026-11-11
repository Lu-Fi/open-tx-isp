// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the LCE block interrupt paths of the driver (driver/t32/tx_isp_t32_blkirq.h) against the vendor
 * flow (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 *
 * Part A, after tisp_lce_init (vendor) / t32_lce_state_init + t32_blk_lce_init (ours), random sections and sizes as
 * t32_lceinit_oracle plus section 15 of every *.bin in T32_IQ_DIR: the buffer set e1db8 (tisp_lce_malloc_cfg) against
 * t32_lce_bufset on the vendor's buffers, the band table e188c against t32_blk.band, the registered interrupt bits
 * (system_irq_func_set: exactly 8 = tisp_lce_interrupt_static, 30 = tisp_lsf_interrupt_static) and event
 * (tisp_event_set_cb table dd6c0: exactly 14 = tisp_lce_par) against ours, the default curve buffer against the
 * driver's (two t32_lce_curve halves).
 * Part B, 1 .. 4 frames per case on both states: random statistics banks / histogram, bank register 0xc088, LCE mode
 * byte, interrupt status (bits 8 / 30 and decoy bits).  Vendor: tisp_lce_interrupt_static (bit 8), then
 * tisp_lsf_interrupt_static (bit 30), then tisp_lce_par for every event 14 it queued (the vendor event thread).
 * Ours: t32_blk_dispatch (the ported lib_tisp_intp_process on our table), the ported event queue, t32_event_process.
 * Compared: every register write (address, value, order), every byte of the state but word 0 (the section pointer),
 * the curve buffer, the statistics and histogram buffers, bank word e1d7c / channel byte e1d78, the events (8 words),
 * the cache sync count.  Direct register mode (d03a0 = 0, the driver's; the shadow mode of the ported functions is
 * covered by t32_lceirq_oracle).  Exit 0 = equal; 1 = mismatch.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_blkirq.h"

#define SECB	T32_LCE_SEC_BYTES
#define SEC_OFF	0x8494u
#define STB	T32_LCETG_STATE_BYTES

/* ---- vendor data of the soft paths (as t32_lcet41_common.inc / t32_lcev4_common.inc) ---- */
u8 oc_bf8a0[128] __asm__("t32_data_bf8a0") __attribute__((weak));
u8 oc_d6c66 __asm__("t32_data_d6c66") __attribute__((weak)) = 20;
u8 oc_d6c67 __asm__("t32_data_d6c67") __attribute__((weak)) = 20;
/* the vendor's scratch .bss objects e18a4 .. e1d74 (one blob so the arrays that run into the next symbol keep their layout) */
u8 oc_bss[0x4d0] __attribute__((aligned(16)));
__asm__(".globl t32_data_e18a4, t32_data_e18b4, t32_data_e1954, t32_data_e19f4, t32_data_e1a14, t32_data_e1a34, t32_data_e1a54, t32_data_e1a74, t32_data_e1a84, t32_data_e1aa4, t32_data_e1ac4, t32_data_e1ae4, t32_data_e1b04, t32_data_e1b14, t32_data_e1b24, t32_data_e1b34, t32_data_e1b44, t32_data_e1b54, t32_data_e1d54, t32_data_e1d64\n"
	".set t32_data_e18a4, oc_bss+0x0\n.set t32_data_e18b4, oc_bss+0x10\n.set t32_data_e1954, oc_bss+0xb0\n.set t32_data_e19f4, oc_bss+0x150\n"
	".set t32_data_e1a14, oc_bss+0x170\n.set t32_data_e1a34, oc_bss+0x190\n.set t32_data_e1a54, oc_bss+0x1b0\n.set t32_data_e1a74, oc_bss+0x1d0\n"
	".set t32_data_e1a84, oc_bss+0x1e0\n.set t32_data_e1aa4, oc_bss+0x200\n.set t32_data_e1ac4, oc_bss+0x220\n.set t32_data_e1ae4, oc_bss+0x240\n"
	".set t32_data_e1b04, oc_bss+0x260\n.set t32_data_e1b14, oc_bss+0x270\n.set t32_data_e1b24, oc_bss+0x280\n.set t32_data_e1b34, oc_bss+0x290\n"
	".set t32_data_e1b44, oc_bss+0x2a0\n.set t32_data_e1b54, oc_bss+0x2b0\n.set t32_data_e1d54, oc_bss+0x4b0\n.set t32_data_e1d64, oc_bss+0x4c0\n"
 );

/* data.c:16887 + 0xe0 (ce210): the u32 lane -> byte pack selector */
u8 oc_ce210[16] __asm__("t32_data_ce210") = { 0, 8, 16, 24, 1, 9, 17, 25, 2, 2, 2, 2, 2, 2, 2, 2 };
/* the vendor's scratch .bss e1e48 .. e20d8 (one blob, 16 byte objects) */
u8 oc_v4bss[0x290 + 16] __attribute__((aligned(16)));
__asm__(".globl t32_data_e1e48, t32_data_e1e58, t32_data_e1e68, t32_data_e1e78, t32_data_e1e88, t32_data_e1e98, t32_data_e1ea8, t32_data_e1eb8, t32_data_e1ec8, t32_data_e1ed8, t32_data_e1ee8, t32_data_e1ef8, t32_data_e1f08, t32_data_e1f18, t32_data_e1f28, t32_data_e1f38, t32_data_e1f48, t32_data_e1f58, t32_data_e1f68, t32_data_e1f78, t32_data_e1f88, t32_data_e1f98, t32_data_e1fa8, t32_data_e1fb8, t32_data_e1fc8, t32_data_e1fd8, t32_data_e1fe8, t32_data_e1ff8, t32_data_e2008, t32_data_e2018, t32_data_e2028, t32_data_e2038, t32_data_e2048, t32_data_e2058, t32_data_e2068, t32_data_e2078, t32_data_e2088, t32_data_e2098, t32_data_e20a8, t32_data_e20b8, t32_data_e20c8\n.set t32_data_e1e48, oc_v4bss+0x0\n.set t32_data_e1e58, oc_v4bss+0x10\n.set t32_data_e1e68, oc_v4bss+0x20\n.set t32_data_e1e78, oc_v4bss+0x30\n.set t32_data_e1e88, oc_v4bss+0x40\n.set t32_data_e1e98, oc_v4bss+0x50\n.set t32_data_e1ea8, oc_v4bss+0x60\n.set t32_data_e1eb8, oc_v4bss+0x70\n.set t32_data_e1ec8, oc_v4bss+0x80\n.set t32_data_e1ed8, oc_v4bss+0x90\n.set t32_data_e1ee8, oc_v4bss+0xa0\n.set t32_data_e1ef8, oc_v4bss+0xb0\n.set t32_data_e1f08, oc_v4bss+0xc0\n.set t32_data_e1f18, oc_v4bss+0xd0\n.set t32_data_e1f28, oc_v4bss+0xe0\n.set t32_data_e1f38, oc_v4bss+0xf0\n.set t32_data_e1f48, oc_v4bss+0x100\n.set t32_data_e1f58, oc_v4bss+0x110\n.set t32_data_e1f68, oc_v4bss+0x120\n.set t32_data_e1f78, oc_v4bss+0x130\n.set t32_data_e1f88, oc_v4bss+0x140\n.set t32_data_e1f98, oc_v4bss+0x150\n.set t32_data_e1fa8, oc_v4bss+0x160\n.set t32_data_e1fb8, oc_v4bss+0x170\n.set t32_data_e1fc8, oc_v4bss+0x180\n.set t32_data_e1fd8, oc_v4bss+0x190\n.set t32_data_e1fe8, oc_v4bss+0x1a0\n.set t32_data_e1ff8, oc_v4bss+0x1b0\n.set t32_data_e2008, oc_v4bss+0x1c0\n.set t32_data_e2018, oc_v4bss+0x1d0\n.set t32_data_e2028, oc_v4bss+0x1e0\n.set t32_data_e2038, oc_v4bss+0x1f0\n.set t32_data_e2048, oc_v4bss+0x200\n.set t32_data_e2058, oc_v4bss+0x210\n.set t32_data_e2068, oc_v4bss+0x220\n.set t32_data_e2078, oc_v4bss+0x230\n.set t32_data_e2088, oc_v4bss+0x240\n.set t32_data_e2098, oc_v4bss+0x250\n.set t32_data_e20a8, oc_v4bss+0x260\n.set t32_data_e20b8, oc_v4bss+0x270\n.set t32_data_e20c8, oc_v4bss+0x280\n");

/* data.c:2717..2733 (bf9e8 ..): the weight table bf9ec (1024 / n, Q15) and the halfword before it (index -1) */
u16 oc_bf9e8[34] __attribute__((aligned(4))) = { 0x0020, 0x0000, 0x7fff, 0x2aab, 0x1555, 0x0ccd, 0x0889, 0x0618, 0x0492,
	0x038e, 0x02d8, 0x0254, 0x01f0, 0x01a4, 0x0168, 0x0138, 0x0111, 0x00f1, 0x00d6, 0x00c0, 0x00ac, 0x009c, 0x008e,
	0x0082, 0x0077, 0x006d, 0x0065, 0x005d, 0x0057, 0x0051, 0x004b, 0x0046, 0x0042, 0x003e };
__asm__(".globl t32_data_bf9ec\n.set t32_data_bf9ec, oc_bf9e8+4\n");
/* data.c:25734 / 25766: the falling (32 .. 1) and rising (1 .. 32) ramp tables d6b64 / d6be4 */
u32 oc_d6b64[32] __asm__("t32_data_d6b64");
u32 oc_d6be4[32] __asm__("t32_data_d6be4");
/* rodata strings of the vendor's printk (not printed) */
u8 oc_bf994[64] __asm__("t32_data_bf994");
u8 oc_c9660[64] __asm__("t32_data_c9660");

extern u8 t32_data_e188c[64], t32_data_dd6c0[0x400];
extern u32 t32o_dma_n, t32_data_d6c64[4];
u32 t32_data_e1d7c[4];
u8 t32_data_e1d78[4];

extern u64 tisp_lce_init(u32, u32, u32, u32);
extern u64 t32o_ref_tisp_lce_interrupt_static(u32, u32, u32, u32);
extern u64 t32o_ref_tisp_lsf_interrupt_static(u32, u32, u32, u32);
extern u64 t32o_ref_tisp_lce_par(u32, u32, u32, u32);
extern void tisp_lce_interrupt_static(void), tisp_lsf_interrupt_static(void), tisp_lce_par(void);	/* env.c: the names tisp_lce_init registers */

static u32 rs = 0x6b1e5d3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

/* system_irq_func_set: the vendor's handler table t32_data_e3b50 (index = bit) */
static u32 virq[T32_INTP_BITS];
u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a3;
	if (a1 < T32_INTP_BITS)
		virq[a1] = a2;
	return 0;
}

/* ---- register reads (both sides): the statistics bank register, else a pattern ---- */
static u32 bank_reg;
static u32 rd(u32 reg) { return reg == 0xc088 ? bank_reg : 0x5a000000u | reg; }

/* ---- our side: struct t32_blk_ops and the event queue's OS calls ---- */
static struct { u32 reg, val; } ow[T32O_MAX_WRITES];
static unsigned int onw, osync, olock, ounlock, oev_n;
static u32 oev[T32O_EVLOG][9];
static struct t32_event_q q;
static u32 done[T32_EV_CHANS];

static u32 o_rd(u32 reg) { return rd(reg); }
static void o_wr(u32 ch, u32 reg, u32 val)
{
	(void)ch;
	if (onw < T32O_MAX_WRITES) {
		ow[onw].reg = reg;
		ow[onw].val = val;
	}
	onw++;
}
static void o_sync(void *p, u32 len) { (void)p; (void)len; osync++; }
static u32 o_lock(void) { olock++; return 0x1234; }
static void o_unlock(u32 f) { if (f == 0x1234) ounlock++; }
static void o_push(u32 ch, const struct t32_event *ev)
{
	if (oev_n < T32O_EVLOG) {
		oev[oev_n][0] = ch;
		memcpy(&oev[oev_n][1], ev, 8 * sizeof(u32));
	}
	oev_n++;
	t32_event_push(&q, ch, ev);
}
static const struct t32_blk_ops oops = { o_rd, o_wr, o_sync, o_lock, o_unlock, o_push };

static void e_lock_init(void) { }
static void e_lock(unsigned long *f) { *f = 0; }
static void e_unlock(unsigned long f) { (void)f; }
static void e_comp_init(u32 ch) { done[ch] = 0; }
static void e_complete(u32 ch) { done[ch]++; }
static long e_wait(u32 ch, u32 t) { (void)t; if (!done[ch]) return 0; done[ch]--; return 1; }
static void e_restart(void) { }
static const struct t32_event_ops eops = { e_lock_init, e_lock, e_unlock, e_comp_init, e_complete, e_wait, e_restart };

static unsigned int compared, refused, nbin_cases, frames, pars, nv4, nt41;
static t32_intp_fn oirq[T32_INTP_BITS];
static struct t32_intp_glob og;

static int cur_frame = -1, cur_sel = -1, cur_mode = -1;
static u32 cur_sta;

static void fail(const char *what, u32 w, u32 h, const char *msg, unsigned int a, unsigned int b, unsigned int c)
{
	printf("(frame %d, path %s, mode %d, status %#x)\n", cur_frame, cur_sel < 0 ? "-" : cur_sel ? "t41" : "v4", cur_mode, cur_sta);
	printf("MISMATCH %s %ux%u: %s %#x vendor %#x ours %#x\n", what, w, h, msg, a, b, c);
	exit(1);
}

static void cmp_bytes(const char *what, u32 w, u32 h, const char *msg, const u8 *v, const u8 *o, unsigned int from, unsigned int n)
{
	unsigned int i;

	for (i = from; i < n; i++)
		if (v[i] != o[i]) {
			if (getenv("BLK_DEBUG")) {
				unsigned int j, d = 0;

				for (j = from; j < n; j++)
					if (v[j] != o[j] && d++ < 40)
						printf(" %#x:%02x/%02x", j, v[j], o[j]);
				printf("\n%u bytes differ\n", d);
			}
			fail(what, w, h, msg, i, v[i], o[i]);
		}
}

static void run_case(const char *what, const u8 *sec, u32 width, u32 height)
{
	u8 *base, *size, *vst, *ost, *osec, *ob[3], *vb[3];
	struct t32_lci_buf *vrec, mine;
	u32 va[3], pa[3], rv;
	static const u32 bytes[3] = { T32_LCE_BUF_CURVE, T32_LCE_BUF_STAT, T32_LCE_BUF_HIST };
	unsigned int i, k, nf;

	if (width < 64 || height < 36) {
		refused++;
		return;
	}
	t32o_reset();
	memset(virq, 0, sizeof(virq));
	t32_data_d03a0[0] = 0;
	base = t32o_alloc(SEC_OFF + SECB + 64);
	size = t32o_alloc(16);
	memcpy(base + SEC_OFF, sec, SECB);
	put32(size, width);
	put32(size + 4, height);
	t32_data_dd0a4[0] = T32O_PTR(base);
	rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_lce_init, 0, T32O_PTR(size), 0, 0, 0);
	vst = T32O_HOST(t32_data_e1da8[0]);
	vrec = (struct t32_lci_buf *)t32_data_e1db8;
	if (rv || !vst)
		fail(what, width, height, "vendor init rv", 0, rv, 0);
	/* ours: the driver's state, buffers (curve with the default curve twice, as tx_isp_t32_hw.c) and t32_blk_lce_init */
	osec = t32o_alloc(SECB);
	memcpy(osec, sec, SECB);
	ost = t32o_alloc(STB);
	t32_lce_state_init(ost, osec, width, height);
	t32_lcetg_wr32(ost, T32O_PTR(osec));
	for (k = 0; k < 3; k++) {
		ob[k] = t32o_alloc(bytes[k] + 0x900);
		va[k] = T32O_PTR(ob[k]);
	}
	/* the physical addresses only go to registers: ours are the vendor's, so the writes compare 1:1 */
	pa[0] = vrec->curve_phys;
	pa[1] = vrec->stat_phys;
	pa[2] = vrec->hist_phys;
	vb[0] = T32O_HOST(vrec->curve);
	vb[1] = T32O_HOST(vrec->stat);
	vb[2] = T32O_HOST(vrec->hist);
	t32_lce_curve(ob[0]);
	t32_lce_curve(ob[0] + T32_LCI_CURVE_LEN);
	memset(&t32_blk, 0, sizeof(t32_blk));
	memset(oirq, 0, sizeof(oirq));
	memset(&q, 0, sizeof(q));
	q.ops = &eops;
	t32_event_init(&q, 0);
	t32_blk.bank = t32_data_e1d7c[0];	/* .bss of both: kept from the last case */
	t32_blk.last_ch = t32_data_e1d78[0];
	t32_blk.ops = &oops;
	t32_blk.shadow = t32_data_d03a0;
	t32_blk_lce_init(0, ost, width, height, va, pa, oirq, q.handler);
	/* part A */
	t32_lce_bufset(&mine, vrec->curve, vrec->curve + 0x80000000u, vrec->stat, vrec->stat + 0x80000000u, vrec->hist,
		       vrec->hist + 0x80000000u);
	cmp_bytes(what, width, height, "buffer set byte", (const u8 *)vrec, (const u8 *)&mine, 0, sizeof(mine));
	cmp_bytes(what, width, height, "band table byte", t32_data_e188c, (const u8 *)t32_blk.band, 0, 24);
	for (i = 0; i < T32_INTP_BITS; i++) {
		u32 want = i == T32_BLK_IRQ_LCE ? T32O_PTR(tisp_lce_interrupt_static) :
			   i == T32_BLK_IRQ_LSF ? T32O_PTR(tisp_lsf_interrupt_static) : 0;
		u32 ours = !oirq[i] ? 0 : oirq[i] == t32_blk_irq_lce ? T32O_PTR(tisp_lce_interrupt_static) :
			   oirq[i] == t32_blk_irq_lsf ? T32O_PTR(tisp_lsf_interrupt_static) : 1;

		if (virq[i] != want || ours != want)
			fail(what, width, height, "interrupt bit", i, virq[i], ours);
	}
	for (i = 0; i < T32_EV_CHANS * T32_EV_TYPES; i++) {
		u32 v = ((u32 *)t32_data_dd6c0)[i];
		u32 ours = !q.handler[i] ? 0 : q.handler[i] == t32_blk_ev_lce_par ? T32O_PTR(tisp_lce_par) : 1;

		if (v != (i == T32_BLK_EV_LCE_PAR ? T32O_PTR(tisp_lce_par) : 0) || ours != v)
			fail(what, width, height, "event handler", i, v, ours);
	}
	{
		u8 vd[4] = { ((u8 *)t32_data_d6c64)[0], ((u8 *)t32_data_d6c64)[1], oc_d6c66, ((u8 *)t32_data_d6c64)[3] };

		cmp_bytes(what, width, height, "d6c64 .. d6c67 byte", vd, t32_blk_d6c, 0, 4);
	}
	cmp_bytes(what, width, height, "default curve byte", vb[0], ob[0], 0, T32_LCE_BUF_CURVE);
	cmp_bytes(what, width, height, "state byte", vst, ost, 4, STB);
	compared++;
	/* part B */
	nf = 1 + rnd() % 4;
	for (k = 0; k < nf; k++) {
		u32 sta = rnd() & ~((1u << T32_BLK_IRQ_LCE) | (1u << T32_BLK_IRQ_LSF)), mode = rnd() % 6, vsync, e;

		if (rnd() % 4)
			sta |= 1u << T32_BLK_IRQ_LCE;
		if (rnd() % 4)
			sta |= 1u << T32_BLK_IRQ_LSF;
		bank_reg = rnd() % 3 ? (rnd() & 3) : rnd();
		for (i = 0; i < T32_LCE_BUF_STAT; i += 4)
			put32(vb[1] + i, rnd() % 3 ? rnd() & 0xffffff : rnd());
		for (i = 0; i < T32_LCE_BUF_HIST; i += 4)
			put32(vb[2] + i, rnd() % 4 ? (rnd() & 0x1fff) * 0x10001u : rnd());
		memcpy(ob[1], vb[1], T32_LCE_BUF_STAT);
		memcpy(ob[2], vb[2], T32_LCE_BUF_HIST);
		vst[T32_LCP_UPDATE] = ost[T32_LCP_UPDATE] = (u8)mode;
		cur_frame = (int)k; cur_sel = ost[T32_LCP_SEL]; cur_mode = (int)mode; cur_sta = sta;
		/* vendor */
		t32o_log.n = 0;
		t32o_ev_n = 0;
		t32o_dma_n = 0;
		t32o_rd_hook = rd;
		if (sta & (1u << T32_BLK_IRQ_LCE))
			t32o_call((t32o_fn)(void (*)(void))t32o_ref_tisp_lce_interrupt_static, 0, 0, 0, 0, 0);
		if (sta & (1u << T32_BLK_IRQ_LSF))
			t32o_call((t32o_fn)(void (*)(void))t32o_ref_tisp_lsf_interrupt_static, 0, 0, 0, 0, 0);
		vsync = t32o_dma_n;
		for (e = 0; e < t32o_ev_n && e < T32O_EVLOG; e++)
			if (t32o_evlog[e][0] == 0 && t32o_evlog[e][3] == T32_BLK_EV_LCE_PAR) {
				t32o_call((t32o_fn)(void (*)(void))t32o_ref_tisp_lce_par, 0, 0, 0, 0, 0);
				pars++;
			}
		t32o_rd_hook = NULL;
		/* ours */
		onw = osync = oev_n = 0;
		t32_blk_dispatch(&og, oirq, sta, 0);
		while (!t32_event_work_empty(&q, 0))
			t32_event_process(&q, 0);
		if (ost[T32_LCP_SEL])
			nt41++;
		else
			nv4++;
		/* compare */
		if (onw != t32o_log.n)
			fail(what, width, height, "register write count, frame", k, t32o_log.n, onw);
		for (i = 0; i < onw && i < T32O_MAX_WRITES; i++)
			if (ow[i].reg != t32o_log.w[i].reg || ow[i].val != t32o_log.w[i].val) {
				printf("write %u: vendor %#x = %#x ours %#x = %#x\n", i, t32o_log.w[i].reg, t32o_log.w[i].val,
				       ow[i].reg, ow[i].val);
				fail(what, width, height, "register write", i, t32o_log.w[i].val, ow[i].val);
			}
		if (oev_n != t32o_ev_n)
			fail(what, width, height, "event count", k, t32o_ev_n, oev_n);
		for (e = 0; e < oev_n && e < T32O_EVLOG; e++)
			for (i = 0; i < 9; i++)
				if (oev[e][i] != t32o_evlog[e][i])
					fail(what, width, height, "event word", i, t32o_evlog[e][i], oev[e][i]);
		if (osync != vsync)
			fail(what, width, height, "cache sync count", k, vsync, osync);
		if (t32_blk.bank != t32_data_e1d7c[0] || t32_blk.last_ch != t32_data_e1d78[0])
			fail(what, width, height, "bank / channel", k, t32_data_e1d7c[0], t32_blk.bank);
		if (olock != ounlock || t32_blk.v4_refused)
			fail(what, width, height, "lock pairs / v4 refused", k, olock, ounlock);
		cmp_bytes(what, width, height, "state byte", vst, ost, 4, STB);
		for (i = 0; i < 3; i++)
			cmp_bytes(what, width, height, i ? "statistics / histogram byte" : "curve byte", vb[i], ob[i], 0, bytes[i]);
		frames++;
	}
}

static void random_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (rnd() % 3) ? (u8)rnd() : (u8)(rnd() & 7);
	put32(sec, T32_LCE_ID);
	sec[507] = 0;
	if (rnd() % 4 == 0)
		sec[507] = (u8)rnd();
	if (rnd() % 8)
		sec[0x1b0] = (u8)(rnd() % 40);
	if (rnd() % 10 == 0)
		sec[436] = sec[439] = 0;
	sec[216] = rnd() % 2 ? 0 : (u8)(1 + rnd() % 255);	/* state 0x22737: v4 / t41 path of tisp_lce_par */
	/* the tile and the thresholds of the soft paths inside their tables (the vendor overruns its stack beyond) */
	sec[146] = (u8)(rnd() % 18);
	sec[147] = (u8)(rnd() % 32);
	sec[514] = (u8)(rnd() % 32);
	sec[579] = (u8)(rnd() % 32);
}

static const u32 sizes[][2] = { { 3840, 2160 }, { 2560, 1440 }, { 1920, 1080 }, { 1280, 720 }, { 640, 480 }, { 2688, 1520 },
				{ 1600, 1200 }, { 800, 600 }, { 320, 240 }, { 704, 576 }, { 128, 64 }, { 64, 36 }, { 1920, 1088 },
				{ 2304, 1296 }, { 32, 16 }, { 4096, 2304 },
				{ 4096, 1152 }, { 4096, 576 }, { 2048, 576 }, { 2048, 1152 }, { 2048, 1150 } };	/* zone counts 8192 / 4096 / 2048 */
#define NSZ (sizeof(sizes) / sizeof(sizes[0]))

static void bin_cases(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char path[1024];
		static u8 bin[1u << 20];
		FILE *f;
		size_t bytes, l = strlen(e->d_name);
		u32 off, len = 0;

		if (l < 4 || strcmp(e->d_name + l - 4, ".bin"))
			continue;
		snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
		f = fopen(path, "rb");
		if (!f)
			continue;
		bytes = fread(bin, 1, sizeof(bin), f);
		fclose(f);
		off = t32_iq_section(bin, (u32)bytes, T32_SEC_LCE, &len);
		if (!off || len < SECB)
			continue;
		run_case(e->d_name, bin + off, sizes[nbin_cases % NSZ][0], sizes[nbin_cases % NSZ][1]);
		nbin_cases++;
	}
	closedir(d);
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 120;
	static u8 sec[SECB];
	unsigned int c;

	/* data.c:25734 / 25766: the falling (32 .. 1) and rising (1 .. 32) ramps d6b64 / d6be4 */
	for (c = 0; c < 32; c++) {
		oc_d6b64[c] = 32 - c;
		oc_d6be4[c] = c + 1;
	}
	for (c = 0; c < n; c++) {
		u32 w, h;

		random_section(sec);
		if (c < 2) {	/* first 1920 x 1080: no size_para_calc, the data.c shift bytes */
			w = 1920;
			h = 1080;
		} else if (rnd() % 3) {
			w = sizes[rnd() % NSZ][0];
			h = sizes[rnd() % NSZ][1];
		} else {
			w = 64 + rnd() % 4000;
			h = 36 + rnd() % 3000;
		}
		run_case("random", sec, w, h);
	}
	if (getenv("T32_IQ_DIR"))
		bin_cases(getenv("T32_IQ_DIR"));
	printf("t32_blkirq_oracle: %u cases (%u refused, %u from IQ bins), %u frames (%u v4 / %u t41), %u lce_par, 0 mismatches\n",
	       compared, refused, nbin_cases, frames, nv4, nt41, pars);
	return 0;
}
