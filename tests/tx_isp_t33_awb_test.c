/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host test of the T33 per-frame AWB (driver/t32/tx_isp_t33_awbalg.h).
 * Without arguments: the flat statistics load (T32 group places), the tag
 * check and the list 29 slot map.  With T33_IQ_BIN (a cv2005 PRJ008 bin):
 * tisp_awb_init's state on the AWB section, then T33_AWB_FRAMES (default
 * 200) frames of the statistics buffer T33_AWB_STAT (a 0x7400 byte dump of
 * one engine buffer; default: uniform grey zones); prints the AWBG list 7
 * words per frame with T33_AWB_PRINT=1.  With T33_AWBG_REF (lines "r b" in
 * hex: the vendor's list 7 words per run, consecutive repeats dropped) the
 * de-duplicated sequence must equal it.  With T33_IQ_BIN also the
 * vendor-layout frame (t33_awb_process_v: the proven tisp_t33_awb_long_alogrithm
 * on the T33 state layout) against the T32-phase reference path, frame by
 * frame on changing statistics: gains, ct, spec words, the history block and
 * the state words both write must be equal.
 */
#define TX_ISP_T33
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
typedef uint64_t u64;	/* host: the AWB headers take the kernel type */
#include "../driver/t32/tx_isp_t32_rcfg33.h"
#include "../driver/t32/tx_isp_t32_iq.h"
#include "../driver/t32/tx_isp_t33_awbalg.h"
#include "../driver/t32/tx_isp_t33_awb.h"

#define T33_SEC_AWB_ALG	2	/* IQ bin AWB section */

static int bad;
#define CHECK(c) do { if (!(c)) { bad++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static uint8_t bin[0x40000], stat[T33_STAT_HEAD + 32 * 32 * T33_AWB_ZONE_BYTES + 64];

/* below 4 GiB: the vendor-layout blocks keep 32 bit addresses (pst_awb_ct_detect) */
static void *h_alloc(void *p, u32 size)
{
	void *m = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

	(void)p;
	return m == MAP_FAILED ? NULL : m;
}
static void h_nop(void *p, u32 ch) { (void)p; (void)ch; }
static void h_nop2(void *p, u32 ch, u32 v) { (void)p; (void)ch; (void)v; }
static void h_print(void *p, int ok) { (void)p; (void)ok; }
static const struct t32_awb_init_ops ops = { NULL, h_alloc, h_nop, h_nop, h_nop2, h_nop2, h_print };
static u32 h_rd(void *p, u32 reg) { (void)p; (void)reg; return 0; }

static void put32(uint8_t *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(uint8_t *p, u32 v) { p[0] = v; p[1] = v >> 8; }

/* rows x cols zones of the same sums, tag t at the head and the end */
static void synth(u32 rows, u32 cols, u32 t)
{
	u32 r, c;

	memset(stat, 0, sizeof(stat));
	put16(stat, t);
	for (r = 0; r < rows; r++)
		for (c = 0; c < cols; c++) {
			uint8_t *z = stat + T33_STAT_HEAD + r * t33_awb_row_bytes(cols) + c * T33_AWB_ZONE_BYTES;

			put32(z, 90000); put32(z + 4, 100000); put32(z + 8, 80000);
			put32(z + 12, 45000 + c * 10); put32(z + 16, 50000); put32(z + 20, 41000 + r * 10);
			put16(z + 24, 1200); put16(z + 26, 600);
		}
	put16(stat + t33_awb_tag_off(rows, cols), t);
}

static void unit(void)
{
	struct t32_awb_state *st = calloc(1, sizeof(*st));

	synth(15, 15, 7);
	t33_awb_load(st, stat, 15, 15);
	CHECK(st->g0_n_r[3 * 32 + 4] == 45040 && st->g0_n_b[3 * 32 + 4] == 41030);
	CHECK(st->g0_den[0] == 50000 && st->g0_w[0] == 600 && st->g1_w[14 * 32 + 14] == 1200);
	CHECK(st->g1_n_r[5] == 90000 && st->g1_den[5] == 100000 && st->g1_n_b[5] == 80000);
	CHECK(t33_awb_tag_off(15, 15) == 8 + 15 * 424);	/* 15 * 28 = 420 + 4 pad */
	CHECK(t33_awb_slot(0x16078) == 0xb && t33_awb_slot(0x160b8) == 0x1b && t33_awb_slot(0x16090) == 0x11);
	CHECK(t33_awb_slot(0x16040) == 3 && t33_awb_slot(0x16064) == 6 && t33_awb_slot(0x6040) == -1);
	free(st);
}


/* the two paths from the same init on frames whose zone sums move (frame f, buffer f & 3) */
static void cmp_paths(const u8 *sec, u32 frames)
{
	struct t32_stats_write w[T32_AWB_MAX_WRITES];
	struct t32_awb_init ai[2];
	struct t33_awb_out o[2];
	struct t33_awbv v;
	struct t32_awb_ctd *d0 = calloc(1, sizeof(*d0)), *d1 = calloc(1, sizeof(*d1));
	u8 *par[2], *bufs = h_alloc(NULL, 4 * T33_AWB_STAT_ONE);
	u32 drops0 = 0, drops1 = 0, f, k, rows, cols, diff = 0, changes = 0, lr = 0, lb = 0;
	u16 c0 = 0, c1 = 0;
	static const u32 st_words[][2] = { { 79036, 1 }, { 79044, 1 }, { 79048, 180 }, { 107608, 12 }, { 107620, 8 } };

	for (k = 0; k < 2; k++) {
		par[k] = h_alloc(NULL, sizeof(struct t32_awb_par));
		memcpy(par[k], sec, sizeof(struct t32_awb_par));
		CHECK(t32_awb_init(&ai[k], 0, par[k], 1920, 1080, 0, &ops, w) == 6);
		ai[k].a.st->hist[0] = ai[k].a.hw->inv_r;
		ai[k].a.st->hist[1] = ai[k].a.hw->inv_b;
	}
	t33_awb_tbl_set(ai[1].tbl, ai[1].a.st);
	v.a = &ai[1].a;
	v.ctd = d1;
	v.rd = h_rd;
	v.rdp = NULL;
	v.ctd_bad = 0;
	rows = ai[0].a.par->zones_h;
	cols = ai[0].a.par->zones_w;
	for (f = 0; f < frames; f++) {
		u8 *b = bufs + (f & 3) * T33_AWB_STAT_ONE;
		u32 r, c;

		/* a light change every 10 frames, a dropped (bad tag) buffer every 37 */
		synth(rows, cols, f + 1);
		for (r = 0; r < rows; r++)
			for (c = 0; c < cols; c++) {
				uint8_t *z = stat + T33_STAT_HEAD + r * t33_awb_row_bytes(cols) + c * T33_AWB_ZONE_BYTES;
				u32 g = 30000 + 900 * ((f / 10) % 7) + 37 * ((r * 7 + c * 3 + f / 10) % 11);

				put32(z + 12, g + (f / 10 % 3) * 4000); put32(z + 16, 50000 + 100 * (r ^ c));
				put32(z + 20, 70000 - g / 2); put32(z + 8, 60000 + 50 * c);
				put16(z + 26, 600 + 13 * ((r + c + f / 10) % 9));
			}
		if (f % 37 == 36)
			put16(stat + t33_awb_tag_off(rows, cols), 0x7777);
		memcpy(b, stat, sizeof(stat) < T33_AWB_STAT_ONE ? sizeof(stat) : T33_AWB_STAT_ONE);
		t33_awb_load(ai[0].a.st, b, rows, cols);
		CHECK(!t33_awb_process(&ai[0].a, d0, h_rd, NULL, b, &c0, &drops0, &o[0]));
		CHECK(!t33_awb_process_v(&v, ai[1].tbl, bufs, f & 3, (u8 *)&drops1, &c1, &o[1]));
		if (o[0].gain_set != o[1].gain_set || o[0].gain_r != o[1].gain_r || o[0].gain_b != o[1].gain_b ||
		    o[0].ct != o[1].ct || o[0].nspec != o[1].nspec || c0 != c1 || drops0 != drops1 ||
		    memcmp(o[0].spec, o[1].spec, o[0].nspec * sizeof(o[0].spec[0])) ||
		    memcmp(ai[0].a.hw->raw, ai[1].a.hw->raw, sizeof(ai[0].a.hw->raw)) || memcmp(par[0], par[1], 64)) {
			if (!diff)
				for (k = 0; k < sizeof(ai[0].a.hw->raw); k += 2)
					if (memcmp(ai[0].a.hw->raw + k, ai[1].a.hw->raw + k, 2) && k < 4400)
						printf("  frame %u hw +%u: %u / %u\n", f, k, ai[0].a.hw->raw[k] | ai[0].a.hw->raw[k + 1] << 8,
						       ai[1].a.hw->raw[k] | ai[1].a.hw->raw[k + 1] << 8);
			if (!diff)
				for (k = 0; k < 64; k += 4)
					if (memcmp(par[0] + k, par[1] + k, 4))
						printf("  frame %u par +%u: %u / %u\n", f, k, t33_aL_r32(par[0], k), t33_aL_r32(par[1], k));
			diff++;
		}
		for (k = 0; k < sizeof(st_words) / sizeof(st_words[0]); k++)
			if (memcmp(ai[0].a.st->raw + T32_AWB_SO(st_words[k][0]), ai[1].a.st->raw + T32_AWB_SO(st_words[k][0]),
				   st_words[k][1])) {
				if (diff < 3)
					printf("  frame %u st %u differs\n", f, st_words[k][0]);
				diff++;
			}
		if (diff && diff < 4)
			printf("  path diff at frame %u: r 0x%x/0x%x b 0x%x/0x%x ct %u/%u drops %u/%u\n", f, o[0].gain_r,
			       o[1].gain_r, o[0].gain_b, o[1].gain_b, o[0].ct, o[1].ct, drops0, drops1);
		if (o[1].gain_r != lr || o[1].gain_b != lb)
			changes++;
		lr = o[1].gain_r;
		lb = o[1].gain_b;
	}
	printf("vendor-layout path vs reference: %u frames, %u gain changes, %u drops, %u differences, ctd_bad %u\n",
	       frames, changes, drops1, diff, v.ctd_bad);
	CHECK(!diff && drops1 == frames / 37);
}

int main(void)
{
	const char *bp = getenv("T33_IQ_BIN"), *sp = getenv("T33_AWB_STAT"), *rp = getenv("T33_AWBG_REF");
	const char *fe = getenv("T33_AWB_FRAMES"), *pe = getenv("T33_AWB_PRINT");
	struct t32_stats_write w[T32_AWB_MAX_WRITES];
	struct t32_awb_init ai;
	struct t32_awb_ctd *d;
	struct t33_awb_out o;
	u32 off, len, drops = 0, seq[2][512], ns = 0, i, frames = fe ? (u32)atoi(fe) : 200;
	u16 counter = 0;
	size_t bytes;
	FILE *f;
	int n;

	unit();
	if (!bp)
		goto done;
	f = fopen(bp, "rb");
	if (!f) {
		perror(bp);
		return 2;
	}
	bytes = fread(bin, 1, sizeof(bin), f);
	fclose(f);
	off = t32_iq_section(bin, bytes, T33_SEC_AWB_ALG, &len);
	CHECK(off == 0xde4 && len == sizeof(struct t32_awb_par));
	if (bad)
		goto done;
	n = t32_awb_init(&ai, 0, bin + off, 1920, 1080, 0, &ops, w);
	CHECK(n == 6 && w[0].reg == 0x6040);
	ai.a.st->hist[0] = ai.a.hw->inv_r;
	ai.a.st->hist[1] = ai.a.hw->inv_b;
	{
		/* tgain_update (AE event 4): list 29 words = the init list at that interpolation point */
		static const u32 gains[] = { 0x400, 0x18000, 0x2c3a1, 0x70000 };
		static u8 sec0[sizeof(struct t32_awb_par)];
		static u32 l[64];
		u8 slot[T33_AWB_TG_SLOTS];
		u32 val[T33_AWB_TG_SLOTS], g, k;

		memcpy(sec0, bin + off, sizeof(sec0));
		for (g = 0; g < 4; g++) {
			memset(l, 0, sizeof(l));
			CHECK(t33_awb_init(sec0, 1920, 1080, gains[g], 0x1000000, l, 64) > 0);
			t33_awb_tgain_update(&ai.a, gains[g]);
			t33_awb_tg_slots(&ai.a, slot, val);
			for (k = 0; k < T33_AWB_TG_SLOTS; k++)
				CHECK(l[slot[k] + 1] == val[k]);
		}
		t33_awb_tgain_update(&ai.a, 0x400);	/* the driver's init state (hw_param hook) */
		CHECK(ai.a.st->lim_a[0] == t32_tp_intp_u16(0, 0x400, sec0 + 0x147a));
		t33_awb_ev_update(&ai.a, 0x110400);
		CHECK(ai.a.st->ev == 0x441);
	}
	printf("par: grid %ux%u fp %u os %u sat_mode %u sat_thr %u ev %u..%u win %u start r 0x%x b 0x%x\n",
	       ai.a.par->zones_w, ai.a.par->zones_h, ai.a.par->fp_shift, ai.a.par->out_shift, ai.a.par->sat_mode,
	       ai.a.par->sat_thr, ai.a.par->ev_lo, ai.a.par->ev_hi, ai.a.par->lg_win, w[0].val, w[1].val & 0xffff);
	if (sp) {
		f = fopen(sp, "rb");
		if (!f) {
			perror(sp);
			return 2;
		}
		memset(stat, 0, sizeof(stat));
		if (fread(stat, 1, sizeof(stat), f) < 8)
			bad++;
		fclose(f);
	} else {
		synth(ai.a.par->zones_h, ai.a.par->zones_w, 1);
	}
	d = calloc(1, sizeof(*d));
	seq[0][ns] = w[0].val;
	seq[1][ns++] = w[1].val & 0xffff;
	for (i = 0; i < frames; i++) {
		t33_awb_load(ai.a.st, stat, ai.a.par->zones_h, ai.a.par->zones_w);
		CHECK(!t33_awb_process(&ai.a, d, h_rd, NULL, stat, &counter, &drops, &o));
		if (pe)
			printf("frame %3u rc %d path %d r 0x%x b 0x%x ct %u\n", i, o.rc, o.path, o.gain_r, o.gain_b, o.ct);
		if (o.gain_set && (o.gain_r != seq[0][ns - 1] || o.gain_b != seq[1][ns - 1]) && ns < 512) {
			seq[0][ns] = o.gain_r;
			seq[1][ns++] = o.gain_b;
		}
	}
	cmp_paths(bin + off, 400);
	printf("awbg sequence: %u distinct steps, last r 0x%x b 0x%x, drops %u\n", ns, seq[0][ns - 1], seq[1][ns - 1], drops);
	if (rp) {
		u32 r, b, k = 0, miss = 0;

		f = fopen(rp, "r");
		if (!f) {
			perror(rp);
			return 2;
		}
		while (fscanf(f, "%x %x", &r, &b) == 2) {
			if (k >= ns || seq[0][k] != r || seq[1][k] != (b & 0xffff)) {
				if (miss++ < 5)
					printf("  step %u: vendor 0x%x 0x%x ours 0x%x 0x%x\n", k, r, b,
					       k < ns ? seq[0][k] : 0, k < ns ? seq[1][k] : 0);
			}
			k++;
		}
		fclose(f);
		CHECK(k == ns && !miss);
	}
done:
	printf("tx_isp_t33_awb_test: %s\n", bad ? "FAIL" : "PASS");
	return bad ? 1 : 0;
}
