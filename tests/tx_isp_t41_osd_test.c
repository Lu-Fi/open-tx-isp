/* SPDX-License-Identifier: MIT */
/*
 * T41 ISP-drawn OSD (0x080000a1 OSD attribute, 0x080000a2 OSD block):
 * request decoding and the MSCA shadow-port register image per call,
 * against the stock libt41-firmware 1.2.6 tisp_s/g_osd_attr,
 * tisp_s/g_osd_block_attr and tisp_msca_set_osd.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_osd.h"

struct pair { unsigned int value, reg; };
struct log { struct pair p[64]; int n; int fail_at; };

static int rec(void *ctx, unsigned int value, unsigned int reg)
{
	struct log *l = ctx;

	if (l->n == l->fail_at)
		return -5;
	assert(l->n < 64);
	l->p[l->n].value = value;
	l->p[l->n].reg = reg;
	l->n++;
	return 0;
}

static void attr(unsigned char *r, int type, int order, int alpha)
{
	t41_osd_wr32(r, (unsigned int)type);
	t41_osd_wr32(r + 4, (unsigned int)order);
	t41_osd_wr32(r + 8, (unsigned int)alpha);
}

static void block(unsigned char *r, int n, int en, int l, int t, int w, int h,
		  unsigned int addr, int stride)
{
	memset(r, 0xee, T41_OSD_BLOCK_BYTES);	/* padding is ignored */
	r[0] = (unsigned char)n;
	r[1] = (unsigned char)en;
	t41_osd_wr16(r + 2, (unsigned int)l);
	t41_osd_wr16(r + 4, (unsigned int)t);
	t41_osd_wr16(r + 6, (unsigned int)w);
	t41_osd_wr16(r + 8, (unsigned int)h);
	t41_osd_wr32(r + 12, addr);
	t41_osd_wr16(r + 16, (unsigned int)stride);
}

static void expect(const struct log *l, int i, unsigned int value,
		   unsigned int reg)
{
	if (l->p[i].value != value || l->p[i].reg != reg) {
		fprintf(stderr, "pair %d: got %08x -> %05x, want %08x -> %05x\n",
			i, l->p[i].value, l->p[i].reg, value, reg);
		assert(0);
	}
}

static void test_attr(void)
{
	struct t41_osd_state s;
	unsigned char r[12], o[12];

	memset(&s, 0, sizeof(s));
	s.block[3].en = 1;
	/* same type (0): no reset, only the bytes are stored */
	attr(r, 0, 13, 1);
	t41_osd_attr_set(&s, r);
	assert(s.block[3].en == 1 && s.dirty == 0);
	assert(s.order == 13 && s.alpha_dis == 1);
	/* new type: every picture off, all eight dirty */
	attr(r, 1, 0x10d, 0);
	t41_osd_attr_set(&s, r);
	assert(s.block[3].en == 0 && s.dirty == 0xff);
	assert(s.type == 1 && s.order == 0x0d);	/* low byte only */
	t41_osd_attr_get(&s, o);
	assert(t41_osd_rd32(o) == 1 && t41_osd_rd32(o + 4) == 13 &&
	       t41_osd_rd32(o + 8) == 0);
}

static void test_block(void)
{
	struct t41_osd_state s;
	unsigned char r[20], o[20];

	memset(&s, 0, sizeof(s));
	block(r, 8, 1, 0, 0, 16, 16, 0x6000000, 64);
	assert(t41_osd_block_set(&s, r, 1920, 1080) == -T41_OSD_EINVAL);
	/* right / bottom edge exactly at the limit is fine, one more is not */
	block(r, 2, 1, 1920 - 64, 1080 - 32, 64, 32, 0x06100000, 256);
	assert(t41_osd_block_set(&s, r, 1920, 1080) == 0);
	assert(s.dirty == 4 && s.block[2].en == 1);
	block(r, 1, 1, 1920 - 63, 0, 64, 32, 0x06100000, 256);
	assert(t41_osd_block_set(&s, r, 1920, 1080) == -T41_OSD_ERANGE);
	block(r, 1, 1, 0, 1080 - 31, 64, 32, 0x06100000, 256);
	assert(t41_osd_block_set(&s, r, 1920, 1080) == -T41_OSD_ERANGE);
	assert(s.dirty == 4 && !s.block[1].en);

	/* get: the requested block (stock always reported block 0) */
	memset(o, 0x55, sizeof(o));
	o[0] = 2;
	assert(t41_osd_block_get(&s, o) == 0);
	assert(o[0] == 2 && o[1] == 1);
	assert(t41_osd_rd16(o + 2) == 1856 && t41_osd_rd16(o + 4) == 1048);
	assert(t41_osd_rd16(o + 6) == 64 && t41_osd_rd16(o + 8) == 32);
	assert(t41_osd_rd32(o + 12) == 0x86100000U);
	assert(t41_osd_rd16(o + 16) == 256 && o[18] == 0 && o[19] == 0);
	memset(o, 0x55, sizeof(o));
	o[0] = 5;
	assert(t41_osd_block_get(&s, o) == 0);
	for (int i = 1; i < 20; ++i)
		assert(o[i] == 0);

	/* disable clears the block and marks it dirty */
	block(r, 2, 0, 9, 9, 9, 9, 9, 9);
	assert(t41_osd_block_set(&s, r, 1920, 1080) == 0);
	assert(!s.block[2].en && !s.block[2].addr && !s.block[2].width);
}

static void test_emit(void)
{
	struct t41_osd_state s;
	struct log l;
	unsigned char r[20], a[12];

	memset(&s, 0, sizeof(s));
	memset(&l, 0, sizeof(l));
	l.fail_at = -1;
	assert(t41_osd_emit(&s, rec, &l) == 0 && l.n == 0);

	/* usual order: format first, then the picture */
	attr(a, 0, 13, 0);			/* ARGB8888, BGRA */
	t41_osd_attr_set(&s, a);
	block(r, 0, 1, 16, 32, 100, 40, 0x06200000, 400);
	assert(t41_osd_block_set(&s, r, 1920, 1080) == 0);
	block(r, 3, 1, 640, 360, 8, 8, 0x06300000, 32);
	assert(t41_osd_block_set(&s, r, 1920, 1080) == 0);
	assert(t41_osd_emit(&s, rec, &l) == 11);
	expect(&l, 0, 13, 0xf0064);
	expect(&l, 1, 16U << 16 | 32, 0xf0600);
	expect(&l, 2, 100U << 16 | 40, 0xf0604);
	expect(&l, 3, 0x06200000, 0xf0608);
	expect(&l, 4, 400, 0xf060c);
	expect(&l, 5, 640U << 16 | 360, 0xf0630);
	expect(&l, 6, 8U << 16 | 8, 0xf0634);
	expect(&l, 7, 0x06300000, 0xf0638);
	expect(&l, 8, 32, 0xf063c);
	expect(&l, 9, 0x09, 0xf0018);
	expect(&l, 10, 0x70, 0xf0060);
	assert(s.dirty == 0);
	l.n = 0;
	assert(t41_osd_emit(&s, rec, &l) == 0 && l.n == 0);

	/* hide block 3: its words are cleared and its enable bit drops */
	block(r, 3, 0, 0, 0, 0, 0, 0, 0);
	assert(t41_osd_block_set(&s, r, 1920, 1080) == 0);
	assert(t41_osd_emit(&s, rec, &l) == 7);
	expect(&l, 1, 0, 0xf0630);
	expect(&l, 2, 0, 0xf0634);
	expect(&l, 3, 0, 0xf0638);
	expect(&l, 4, 0, 0xf063c);
	expect(&l, 5, 0x01, 0xf0018);

	/* ARGB1555 with pixel alpha off: cfg word, every block rewritten */
	l.n = 0;
	attr(a, 1, 0, 1);
	t41_osd_attr_set(&s, a);
	assert(t41_osd_emit(&s, rec, &l) == 1 + 8 * 4 + 2);
	expect(&l, 33, 0x00, 0xf0018);
	expect(&l, 34, 0x171, 0xf0060);

	/* a failed write keeps the blocks dirty */
	block(r, 1, 1, 0, 0, 4, 4, 0x06400000, 16);
	assert(t41_osd_block_set(&s, r, 1920, 1080) == 0);
	l.n = 0;
	l.fail_at = 3;
	assert(t41_osd_emit(&s, rec, &l) == -5);
	assert(s.dirty == 2);
	l.n = 0;
	l.fail_at = -1;
	assert(t41_osd_emit(&s, rec, &l) == 7);

	/* restart: enabled blocks are written again */
	t41_osd_mark_enabled(&s);
	assert(s.dirty == 2);
}

int main(void)
{
	test_attr();
	test_block();
	test_emit();
	printf("tx_isp_t41_osd_test: ok\n");
	return 0;
}
