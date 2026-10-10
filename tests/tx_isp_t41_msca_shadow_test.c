/* SPDX-License-Identifier: MIT */
/*
 * T41 MSCA staged 0xf002c word: model the register as measured on the
 * device (write = staged, read = active, 0xf0010 latches at the next frame)
 * and check that HVFLIP set + restore within one frame ends unflipped.
 */
#include <assert.h>
#include <stdio.h>
#include "../driver/t41/tx_isp_t41_msca_shadow.h"

struct reg {
	unsigned int active, staged, update;
};

static void reg_write(struct reg *r, unsigned int v) { r->staged = v; }
static void frame(struct reg *r)
{
	if (r->update)
		r->active = r->staged;
	r->update = 0;
}

/* driver tisp_msca_set_mirr_flip() with skip_noop = 1 */
static int set_flip(struct reg *r, struct t41_msca_staged *s, unsigned int bits)
{
	unsigned int value;

	if (!t41_msca_flip_word(s, r->active, bits, &value))
		return 0;
	reg_write(r, value);
	t41_msca_staged_store(s, value);
	r->update = 1;
	return 1;
}

/* the old comparison against the read-back */
static int set_flip_readback(struct reg *r, unsigned int bits)
{
	unsigned int v = (r->active & T41_MSCA_ALGO_KEEP_MASK) | bits;

	if (v == r->active)
		return 0;
	reg_write(r, v);
	r->update = 1;
	return 1;
}

int main(void)
{
	const unsigned int hflip = 0x08080000U, algo = 7U;
	struct reg r = { algo, algo, 0 };
	struct t41_msca_staged s = { 0, 0 };

	/* old logic: restore before the next frame is skipped -> stuck */
	assert(set_flip_readback(&r, hflip) == 1);
	assert(set_flip_readback(&r, 0) == 0);
	frame(&r);
	assert(r.active == (algo | hflip));

	/* staged logic: the restore is written and wins */
	r.active = r.staged = algo; r.update = 0;
	assert(set_flip(&r, &s, hflip) == 1);
	assert(set_flip(&r, &s, 0) == 1);
	frame(&r);
	assert(r.active == algo);

	/* unchanged requests stay no-ops (no update request) */
	assert(set_flip(&r, &s, 0) == 0);
	assert(r.update == 0);
	assert(set_flip(&r, &s, hflip) == 1);
	frame(&r);
	assert(set_flip(&r, &s, hflip) == 0);

	/* the algorithm RMW keeps a pending flip */
	assert(set_flip(&r, &s, 0) == 1);   /* pending: unflip */
	{
		unsigned int a = t41_msca_staged_base(&s, r.active) | 7U | (1U << 12);

		reg_write(&r, a);
		t41_msca_staged_store(&s, a);
		r.update = 1;
	}
	frame(&r);
	assert((r.active & hflip) == 0 && (r.active & (1U << 12)));

	/* after MSCA init the read-back is the base again */
	s.valid = 0;
	assert(t41_msca_staged_base(&s, 0x1234U) == 0x1234U);
	assert(t41_msca_staged_base(NULL, 5U) == 5U);
	t41_msca_staged_store(NULL, 1U);
	puts("tx_isp_t41_msca_shadow_test: ok");
	return 0;
}
