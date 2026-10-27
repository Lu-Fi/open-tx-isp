/*
 * Host test of driver/t23/tx_isp_t23_osd.h: the MSCA register writes of
 * the T23 ISP OSD for pictures stored with the SetOSDAttr / SetOSDBlock
 * helpers of tx_isp_t23_tuning_ext.h.  Expected pairs follow the stock
 * libt23-firmware 1.3.0 tisp_msca_api_set_osd (bit-exact against the stock
 * code in the emulator: driver/t23/audit/osd_commit_emu.py).
 */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_tuning_ext.h"
#include "../driver/t23/tx_isp_t23_osd.h"

struct log {
	unsigned int n;
	unsigned int v[2 * T23_OSD_MAX_PAIRS];
};

static int rec(void *ctx, unsigned int value, unsigned int reg)
{
	struct log *l = ctx;

	assert(l->n + 2 <= 2 * T23_OSD_MAX_PAIRS);
	l->v[l->n++] = value;
	l->v[l->n++] = reg;
	return 0;
}

static void put16(uint8_t *p, unsigned int v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, unsigned int v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

static void attr(uint8_t *m, unsigned int type, unsigned int order,
		 unsigned int alpha)
{
	uint8_t in[T23X_OSD_ATTR_BYTES];

	put32(in, type);
	put32(in + 4, order);
	put32(in + 8, alpha);
	t23x_osd_attr_set(m, in);
}

static int block(uint8_t *m, const uint8_t *msca, unsigned int n,
		 unsigned int en, unsigned int x, unsigned int y,
		 unsigned int w, unsigned int h, unsigned int addr,
		 unsigned int stride)
{
	uint8_t in[T23X_OSD_BLOCK_BYTES];
	int quirk;

	memset(in, 0, sizeof(in));
	in[0] = (uint8_t)n;
	in[1] = (uint8_t)en;
	put16(in + 2, x);
	put16(in + 4, y);
	put16(in + 6, w);
	put16(in + 8, h);
	put32(in + 12, addr);
	put16(in + 16, stride);
	return t23x_osd_block_set(m, msca, in, &quirk);
}

static int find(const struct log *l, unsigned int reg, unsigned int *value)
{
	unsigned int i;

	for (i = 0; i + 1 < l->n; i += 2)
		if (l->v[i + 1] == reg) {
			*value = l->v[i];
			return 1;
		}
	return 0;
}

int main(void)
{
	static uint8_t m[T23X_MSCA_BYTES], msca[168];
	struct log l;
	unsigned int v;

	put32(msca + T23X_MSCA_LIMIT_W_OFF, 1280);
	put32(msca + T23X_MSCA_LIMIT_H_OFF, 720);

	/* nothing stored: nothing written */
	memset(&l, 0, sizeof(l));
	assert(t23_osd_emit(m, rec, &l) == 0 && l.n == 0);

	/* the apitest case: 128x64 ARGB8888 at (64,64), BGRA order 5 */
	attr(m, 0, 5, 0);
	/* the type did not change (0): no reset of the blocks */
	assert(t23_osd_dirty(m) == 0);
	assert(block(m, msca, 0, 1, 64, 64, 128, 64, 0x03a00000, 512) == 0);
	assert(t23_osd_dirty(m) == 1);
	memset(&l, 0, sizeof(l));
	assert(t23_osd_emit(m, rec, &l) == 7);
	{
		static const unsigned int want[] = {
			5, 0x754,
			64 << 16 | 64, 0x760,
			128 << 16 | 64, 0x764,
			0x03a00000, 0x768,
			512, 0x76c,
			1, 0x048,
			0x70, 0x750,
		};
		assert(l.n == sizeof(want) / sizeof(want[0]));
		assert(!memcmp(l.v, want, sizeof(want)));
	}
	assert(t23_osd_dirty(m) == 0);
	memset(&l, 0, sizeof(l));
	assert(t23_osd_emit(m, rec, &l) == 0);

	/* second picture in slot 3: only slot 3 is written, both enabled */
	assert(block(m, msca, 3, 1, 0, 0, 16, 16, 0x03b00000, 64) == 0);
	memset(&l, 0, sizeof(l));
	assert(t23_osd_emit(m, rec, &l) == 7);
	assert(find(&l, 0x790, &v) && v == 0);
	assert(!find(&l, 0x760, &v));
	assert(find(&l, 0x048, &v) && v == 0x09);

	/* hide slot 0: geometry and address zeroed, its enable bit goes off
	 * (stock keeps the bit: osd_en is only ever ORed) */
	assert(block(m, msca, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
	memset(&l, 0, sizeof(l));
	assert(t23_osd_emit(m, rec, &l) == 7);
	assert(find(&l, 0x760, &v) && v == 0);
	assert(find(&l, 0x768, &v) && v == 0);
	assert(find(&l, 0x048, &v) && v == 0x08);

	/* beyond the main stream: refused, nothing marked */
	assert(block(m, msca, 1, 1, 1200, 0, 128, 16, 0x03c00000, 512) == -ERANGE);
	assert(t23_osd_dirty(m) == 0);

	/* output restart: the shown pictures go out again */
	t23_osd_mark_enabled(m);
	assert(t23_osd_dirty(m) == 0x08);
	memset(&l, 0, sizeof(l));
	assert(t23_osd_emit(m, rec, &l) == 7);

	/* format change to ARGB1555 with alpha disabled: every picture is
	 * switched off and all eight slots are rewritten */
	attr(m, 1, 2, 1);
	assert(t23_osd_dirty(m) == 0xff);
	memset(&l, 0, sizeof(l));
	assert(t23_osd_emit(m, rec, &l) == (int)T23_OSD_MAX_PAIRS);
	assert(find(&l, 0x754, &v) && v == 2);
	assert(find(&l, 0x048, &v) && v == 0);
	assert(find(&l, 0x750, &v) && v == (0x71 | 1 << 8));
	assert(t23_osd_enable_mask(m) == 0);

	puts("tx_isp_t23_osd_test: ok");
	return 0;
}
