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

static u32 W[0x100];
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

	printf("tx_isp_t33_osd_test: %s\n", fails ? "FAIL" : "PASS");
	return fails != 0;
}
