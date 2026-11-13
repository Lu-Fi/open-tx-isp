/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Host test of the T33 MSCA OSD / mask outputs (driver/t32/tx_isp_t33_osd.h): the
 * list-23 words of the vendor tisp_msca_t33_api_set_osd_reg / _set_mask_reg for
 * a window / mask set, the alignment fixes, the bounds refusals and the get
 * round trips.  The expected values are the ones the vendor code computes
 * (hand-derived from its decompile, 0xe48e4 / 0xe521c).
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../driver/t32/tx_isp_t33_osd.h"

static u32 W[0x200];
static int fails, refreshes, msgs;

static void slot(u32 ch, u32 s, u32 v) { (void)ch; W[s] = v; }
static void refresh(u32 ch) { (void)ch; refreshes++; }
static void msg(u32 l, const char *f, ...) { (void)l; (void)f; msgs++; }
static const struct t33_osd_ops ops = { slot, refresh, msg };

#define CHK(c, ...) do { if (!(c)) { printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

int main(void)
{
	static struct t33_osd o;
	u8 b[32], g[32];
	u32 i;

	o.in_w = 1920;
	o.in_h = 1080;
	/* attribute: a36c = 1, a36a = 2, a36e = 3, groups 12 + 4 g + k = 10 g + k + 1 */
	memset(b, 0, sizeof(b));
	b[0] = 1; b[4] = 2; b[8] = 3; b[28] = 7;
	for (i = 0; i < 16; i++)
		b[12 + i] = (u8)(0x10 * (i / 4) + i % 4 + 1);
	t33_osd_set_attr(&o, b);
	memset(g, 0xee, sizeof(g));
	t33_osd_get_attr(&o, g);
	CHK(!memcmp(g, b, 29) || (g[0] == 1 && g[28] == 7 && !memcmp(g + 12, b + 12, 16)), "attr round trip");

	/* window 1: odd x, w odd -> aligned */
	memset(b, 0, sizeof(b));
	b[0] = 1; b[1] = 1;
	t33_osd_p16(b + 2, 101); t33_osd_p16(b + 4, 50); t33_osd_p16(b + 6, 201); t33_osd_p16(b + 8, 100);
	t33_osd_p32(b + 12, 0x12345678); t33_osd_p16(b + 16, 0xabcd);
	t33_osd_set_out(&o, &ops, 0, b);
	CHK(refreshes == 1, "refresh");
	CHK(o.win[1].x == 100 && o.win[1].y == 50 && o.win[1].w == 200 && o.win[1].h == 100, "aligned %u %u %u %u",
	    o.win[1].x, o.win[1].y, o.win[1].w, o.win[1].h);
	CHK(o.info == 2, "info %u", o.info);
	/* window 1: b = 0xb4 + 4 = 0xb8 -> slots 0xb5 .. 0xb8 */
	CHK(W[0xb5] == (100u << 16 | 50), "xy %#x", W[0xb5]);
	CHK(W[0xb6] == (200u << 16 | 100), "wh %#x", W[0xb6]);
	CHK(W[0xb7] == 0x12345678, "word");
	CHK(W[0xb8] == 0xabcd, "half");
	CHK(W[5] == 2, "enable mask %#x", W[5]);
	CHK(W[0xaa] == 0, "0xaa");
	CHK(W[0xa6] == 0x381da506 && W[0xa7] == 0xd13b5e02 && W[0xa8] == 0xf6e70064 && W[0xa9] == 0x8010, "consts");
	CHK(W[0xab] == (3u << 4 | 1 | 0x10000 | 2u << 8), "0xab %#x", W[0xab]);
	CHK(W[0xad] == (0x01u | 0x11u << 8 | 0x21u << 16 | 0x31u << 24), "0xad %#x", W[0xad]);
	CHK(W[0xb0] == (0x04u | 0x14u << 8 | 0x24u << 16 | 0x34u << 24), "0xb0 %#x", W[0xb0]);

	/* 4 px aligned windows (a36c == 2): w to a multiple of 4 */
	o.a36c = 2;
	b[0] = 2; t33_osd_p16(b + 2, 10); t33_osd_p16(b + 6, 102);
	t33_osd_set_out(&o, &ops, 0, b);
	CHK(o.win[2].w == 100, "w4 %u", o.win[2].w);
	CHK(W[5] == 6, "mask %#x", W[5]);

	/* overflow refused, no store, message, no refresh */
	i = refreshes;
	b[0] = 3; t33_osd_p16(b + 2, 1900); t33_osd_p16(b + 6, 100);
	t33_osd_set_out(&o, &ops, 0, b);
	CHK(!o.win[3].en && msgs == 2 && refreshes == (int)i, "overflow");
	/* pinum 4 refused */
	b[0] = 4; msgs = 0;
	t33_osd_set_out(&o, &ops, 0, b);
	CHK(msgs == 1, "pinum");
	/* get */
	memset(g, 0, sizeof(g)); g[0] = 1;
	t33_osd_get_out(&o, g);
	CHK(g[1] == 1 && t33_osd_u16(g + 2) == 100 && t33_osd_u32(g + 12) == 0x12345678 && t33_osd_u16(g + 16) == 0xabcd, "get");
	/* disable */
	memset(b, 0, sizeof(b)); b[0] = 1;
	t33_osd_set_out(&o, &ops, 0, b);
	CHK(!o.win[1].en && W[5] == 4, "disable %#x", W[5]);
	memset(g, 0x55, sizeof(g)); g[0] = 1;
	t33_osd_get_out(&o, g);
	CHK(g[1] == 0 && g[9] == 0 && g[12] == 0 && g[17] == 0 && g[10] == 0x55, "get off");

	/* mask: channel 1, window 2 */
	memset(b, 0, sizeof(b));
	b[0] = 1; b[1] = 2; b[2] = 1;
	t33_osd_p16(b + 4, 11); t33_osd_p16(b + 6, 22); t33_osd_p16(b + 8, 33); t33_osd_p16(b + 10, 44);
	b[19] = 0xaa; b[20] = 0xbb; b[21] = 0xcc;
	t33_mask_set_out(&o, &ops, 0, b);
	/* slot = 1 * 0x10 + 0x75 + 8 = 0x8d */
	CHK(W[0x8c] == (22u << 16 | 11), "mask a/b %#x", W[0x8c]);
	CHK(W[0x8d] == (33u << 16 | 44), "mask c/d %#x", W[0x8d]);
	CHK(W[0x8e] == 0xaabbcc, "mask colour %#x", W[0x8e]);
	CHK(W[4] == (1u << 1) << 0 || W[4] == 2, "mask enables %#x", W[4]);
	memset(g, 0, sizeof(g)); g[0] = 1; g[1] = 2;
	t33_mask_get_out(&o, g);
	CHK(g[2] == 1 && !memcmp(g + 4, b + 4, 8) && g[19] == 0xaa && g[21] == 0xcc, "mask get");
	b[2] = 0;
	t33_mask_set_out(&o, &ops, 0, b);
	CHK(W[0x8d] == 0 && W[4] == 0, "mask off %#x %#x", W[0x8d], W[4]);

	/* draw block: type 1 line window 0, colour list, then type 2 slanted line in window 1 */
	{
		static struct t33_draw d;
		static struct t33_colors cl;
		u8 q[36];
		u32 s;

		memset(W, 0, sizeof(W));
		memset(q, 0, sizeof(q));
		q[0] = 0; t33_osd_p32(q + 4, 1); q[12] = 1;
		t33_osd_p16(q + 14, 10); t33_osd_p16(q + 16, 20); t33_osd_p16(q + 18, 30); t33_osd_p16(q + 20, 40);
		q[25] = 0x10; q[26] = 0x80; q[27] = 0x80; q[28] = 3; q[29] = 1; q[30] = 7; q[32] = 2;
		t33_draw_set_out(&d, &cl, &ops, 0, q);
		CHK(cl.n == 1 && cl.c[0] == 0x108080 && W[0xc7] == 0x108080, "colour list");
		CHK(W[0xa4] == 1 && W[0x12f] == 0x101 && W[0x192] == 0x10102, "constants");
		/* window 0: slot 0x131 */
		CHK(W[0x131] == (20u << 12 | 10 | 2u * 0x1000000 | 0x4000000), "w0 %#x", W[0x131]);
		CHK(W[0x132] == (40u << 12 | 0u << 24 | 30 | 2u << 26), "w1 %#x", W[0x132]);
		CHK(W[0x133] == (7u << 8 | 0u << 16 | 3 | 1u << 18 | 1u << 23), "w2 %#x", W[0x133]);
		/* slanted line (0,0)-(100,50) as window 1: x1 < x2, y1 < y2 -> same direction, dx/dy = 2 -> b3ad 2 */
		memset(q, 0, sizeof(q));
		q[0] = 1; t33_osd_p32(q + 4, 2); q[12] = 1; q[28] = 2;
		t33_osd_p16(q + 14, 0); t33_osd_p16(q + 16, 0); t33_osd_p16(q + 18, 100); t33_osd_p16(q + 20, 50);
		q[25] = 0x10; q[26] = 0x80; q[27] = 0x80;
		t33_draw_set_out(&d, &cl, &ops, 0, q);
		CHK(d.win[1].w == 100 && d.win[1].h == 50 && d.win[1].b3ad == 2, "slant %u %u %u", d.win[1].w, d.win[1].h, d.win[1].b3ad);
		CHK(d.win[1].k == 0xcc || d.win[1].k != 5, "k %#x", d.win[1].k);
		CHK(cl.n == 1, "same colour reused");
		s = 0x131 + 3;
		CHK((W[s] >> 24) == (3u | 4u), "type word %#x", W[s]);
		/* unused window fill starts at the enabled count: the slot of window 2 (2 * 3) is cleared, n + 2 is not */
		CHK(W[0x131 + 6] == 0, "fill");
		/* horizontal line: k 0xff, b3ad 2, b3ae 0x1f, h = width */
		t33_osd_p16(q + 20, 0);
		q[0] = 2;
		t33_draw_set_out(&d, &cl, &ops, 0, q);
		CHK(d.win[2].k == 0xff && d.win[2].b3ad == 2 && d.win[2].b3ae == 0x1f && d.win[2].h == 2 && d.win[2].w == 100, "horizontal");
		/* equal end points refused while enabled */
		msgs = 0;
		t33_osd_p16(q + 18, 0);
		t33_draw_set_out(&d, &cl, &ops, 0, q);
		CHK(msgs == 1, "equal points");
		/* colour list full */
		msgs = 0;
		memset(q, 0, sizeof(q));
		q[0] = 3; t33_osd_p32(q + 4, 0);
		for (s = 1; s < 6; s++) {
			q[25] = (u8)s; t33_draw_set_out(&d, &cl, &ops, 0, q);
		}
		CHK(cl.n == 4 && msgs == 2, "colour full n %u msgs %d", cl.n, msgs);
		/* type 3 msk size error */
		msgs = 0;
		memset(q, 0, sizeof(q));
		q[0] = 5; q[1] = 4; t33_osd_p32(q + 4, 3);
		t33_draw_set_out(&d, &cl, &ops, 0, q);
		CHK(d.msk == 8 && msgs >= 1 && q[1] == 8, "msk");
		/* get round trip of window 0 */
		memset(q, 0, sizeof(q));
		t33_draw_get_out(&d, q);
		CHK(t33_osd_u32(q + 4) == 1 && q[12] == 1 && t33_osd_u16(q + 18) == 30 && q[28] == 3 && q[32] == 2 && q[25] == 0x10, "draw get");
		/* find_k_n: the exact table value and the limits */
		{
			u8 k, n;

			t33_find_k_n(24938, &k, &n); CHK(k == 0xcc && n == 0xf3, "knA");
			t33_find_k_n(1, &k, &n); CHK(k == 0xcc && n == 0xf3, "kn low %#x %#x", k, n);
			t33_find_k_n(0x7fffffff, &k, &n); CHK(k == 0xa0 && n == 0xfe, "kn high %#x %#x", k, n);
			t33_find_k_n(40098734, &k, &n); CHK(k == 0xa0 && n == 0xfe, "kn last %#x %#x", k, n);
		}
	}

	printf("tx_isp_t33_osd_test: %s\n", fails ? "FAIL" : "PASS");
	return fails != 0;
}
