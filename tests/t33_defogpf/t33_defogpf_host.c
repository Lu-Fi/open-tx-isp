// SPDX-License-Identifier: GPL-2.0
/*
 * T33 per-frame defog (openimp #878, driver/t32/tx_isp_t33_defogpf.h) against the
 * T32 port it is built on (tx_isp_t32_defog.h / _defogs.h, oracle-proven, #854).
 * The vendor T33 ko's Tiziano_defog_soft / filters are instruction-equal to the
 * T32 ones; its interp_trans_k, update_trans_k, update_reg_para2, par and
 * ev_update read the IQ section 4 bytes further on from T32 byte 190 on.  So a
 * T33 section made from a T32 one (bytes 0..189 equal, 190.. moved by 4) must
 * give the same parameter block, the same trans-k bytes (T33 +260 = T32 +256),
 * the same EV word and the same 180 LUT words, frame by frame, with random
 * statistics, EV updates in both trans-k modes (section +191 / +195) and the
 * strength / smoothing switches.  The vendor oracle of the whole path is the
 * QEMU run (audit issue878: vendor tx-isp-t33.ko with defog bit 10 raised).
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_defogs.h"
#include "tx_isp_t33_defogpf.h"

static u32 rng = 0x878u;
static u32 rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static u8 sa[T32_DEFOG_SEC_BYTES], sb[T32_DEFOG_SEC_BYTES + 4];
static struct t32_defog_par p32, p33;
static struct t32_defog_p2 p2;
static struct t32_defog_ctx x;
static struct t32_defog_chan c;
static u32 info[14];
static u8 stat[2048];
static u32 slots[0x14a];

static void h_soft(struct t32_defog_par *p) { t32_defog_soft(p); }
static u32 h_rd(u32 r) { (void)r; return 0x1000u; }	/* the bank = the buffer's address */
static void h_sync(const u8 *b, u32 l) { (void)b; (void)l; }
static void h_push(u32 ch, const u32 *e) { (void)ch; (void)e; }
static const struct t32_defog_ops ops = { .soft = h_soft, .reg_read = h_rd, .dma_sync = h_sync, .event_push = h_push };
static void wr33(u32 slot, u32 v) { slots[slot] = v; }

static void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }

static int run(int mode)
{
	struct t32_front_write w[256];
	unsigned int n, f, j, k;
	int bad = 0;

	for (k = 0; k < sizeof(sa); k++)
		sa[k] = (u8)rnd();
	for (k = 0; k < 9; k++) {		/* ascending EV thresholds +8, descending +44 */
		put32(sa + 8 + 4 * k, 20000u + 90000u * k + (rnd() & 0xfff));
		put32(sa + 44 + 4 * k, 900000u - 90000u * k + (rnd() & 0xfff));
	}
	sa[191] = (u8)(mode & 1);		/* trans-k on the second EV word */
	sa[590] = 0;
	sa[190] = (u8)((mode >> 1) & 1);	/* strength curve off */
	sa[192] = (u8)((mode >> 2) & 1);	/* luma-only strength */
	sa[195] = (u8)!(mode & 1);		/* activity mask (irq strength byte), never equal to +191 */
	sa[199] = 1;				/* smoothing */
	for (k = 0; k < 9; k++)
		sa[217 + k] = (u8)(rnd() & 0x3f);
	memcpy(sb, sa, 190);
	memcpy(sb + 194, sa + 190, sizeof(sa) - 190);
	put32(sb + 190, rnd());
	memset(&p32, 0, sizeof(p32));
	memset(&p33, 0, sizeof(p33));
	for (k = 0; k < 4; k++)
		put32(p32.b + 2160 + 4 * k, 0x2dcc2u - 1000u * k);
	p32.b[2455] = 4;
	p32.b[2456] = 9;
	p32.b[2457] = 1;
	memcpy(p33.b, p32.b, sizeof(p32.b));
	memset(&x, 0, sizeof(x));
	memset(&c, 0, sizeof(c));
	memset(info, 0, sizeof(info));
	x.sec = sa;
	x.par = &p32;
	x.p2 = &p2;
	x.buf = stat;
	x.buf_phys = 0x1000u;
	x.ev = info[T33_DPF_I_EV] = 300000u;
	c.ctx = &x;
	c.ops = &ops;
	info[T33_DPF_I_PHYS] = 0x1000u;
	/* init: interp (EV word of the section unless held), update_reg_para2 */
	t32_defog_trans_refresh(&x);
	if (!sb[594])
		put32(sb + 4, info[T33_DPF_I_EV]);
	t33_dpf_interp_trans_k(sb, t33_dpf_le32(sb + 4), info[T33_DPF_I_EV2], sb + 8, sb + 614, sb + 623,
			       sb + 632, sb + 641, sb + 650, sb + 260);
	t32_defog_update_reg_para2(&c, 0);
	t33_dpf_update_reg_para2(sb, &p33);
	for (f = 0; f < 24; f++) {
		for (k = 0; k < sizeof(stat); k++)
			stat[k] = (u8)rnd();
		t32_defog_interrupt_static(&c, 0);
		t33_dpf_interrupt_static(info, &p33, 0x1000u, stat, h_sync);
		if (f % 3 == 1) {
			u32 lo = rnd() << 4, x5 = 100000u + (rnd() & 0xfffff), y = f & 4;

			t32_defog_ev_update(&c, 0, lo, 0, x5, y);
			t33_dpf_ev_update(sb, info, &p33, lo, 0, x5, y);
		}
		n = 0;
		t32_defog_par(&c, 0, w, &n);
		t33_dpf_par(sb, info, &p33);
		t33_dpf_write_reg_para3(&p33, wr33);
		if (n != 184) {
			printf("mode %d frame %u: T32 writes %u\n", mode, f, n);
			return 1;
		}
		for (j = 0; j < T33_DPF_GRID; j++)
			if (w[2 + j].val != slots[0x92 + j])
				bad++;
		if (slots[0x8e] != 0 || slots[0x90] != 0x101 || slots[0x147] != 0xb30102 || slots[0x149] != 1)
			bad++;
		if (memcmp(p32.b, p33.b, sizeof(p32.b)) || memcmp(sa + 256, sb + 260, 5) ||
		    memcmp(sa + 4, sb + 4, 4) || x.irq_cnt != info[T33_DPF_I_IRQ] || x.ev != info[T33_DPF_I_EV] ||
		    x.ev2 != info[T33_DPF_I_EV2])
			bad++;
		if (bad) {
			printf("mode %d frame %u: T33 differs from the T32 port (%d)\n", mode, f, bad);
			return 1;
		}
	}
	return 0;
}

int main(void)
{
	int mode, fail = 0, lutvar = 0;

	for (mode = 0; mode < 8; mode++) {
		fail |= run(mode);
		lutvar += slots[0x92] != slots[0x92 + 90];
	}
	if (!lutvar) {
		printf("t33_defogpf_host: FAIL (flat LUTs: the test does not reach the soft path)\n");
		return 1;
	}
	printf("t33_defogpf_host: %s (8 modes x 24 frames, LUT / parameter block / trans-k / EV equal)\n",
	       fail ? "FAIL" : "PASS");
	return fail;
}
