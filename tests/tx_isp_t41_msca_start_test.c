/* SPDX-License-Identifier: MIT */
/*
 * T41 MSCA output start with the latch taken while the output is off
 * (t41_msca_cfg_update=2).  Models the staged words as measured on the
 * device (write = staged, read = active, 0xf0010 applies them at the next
 * input frame) and checks the driver sequence of tisp_msca_chx_cfg_load()
 * plus t41_msca_finish_enable():
 *  - a changed geometry is never latched while its output is enabled;
 *  - the output is enabled only once the read-back shows the new words;
 *  - an unchanged geometry is enabled at once, without an update request.
 */
#include <assert.h>
#include <stdio.h>
#include "../driver/t41/tx_isp_t41_msca_shadow.h"

#define N 6
struct msca {
	unsigned int active[N], staged[N];
	unsigned int enable, pending, update;
	unsigned int latched_while_enabled;
};

static struct t41_msca_word words[N];

static void frame(struct msca *m)
{
	unsigned int i;

	if (!m->update)
		return;
	if (m->enable & 2U)
		for (i = 0; i < N; ++i)
			if ((m->active[i] ^ m->staged[i]) & words[i].mask)
				m->latched_while_enabled++;
	for (i = 0; i < N; ++i)
		m->active[i] = m->staged[i];
	m->update = 0;
}

static void set_words(unsigned int size, unsigned int ratio)
{
	words[0] = (struct t41_msca_word){ 0xf0200, size, ~0U };
	words[1] = (struct t41_msca_word){ 0xf0228, 0, ~0U };
	words[2] = (struct t41_msca_word){ 0xf022c, size, ~0U };
	words[3] = (struct t41_msca_word){ 0xf072c, ratio, 0x7ffffU };
	words[4] = (struct t41_msca_word){ 0xf0720, ratio, 0x7ffffU };
	words[5] = (struct t41_msca_word){ 0xf002c, 7U, ~0U };
}

/* cfg_load for output 1 with cfg_update=2: off first, write, plan */
static void start(struct msca *m)
{
	unsigned int i;

	m->pending &= ~2U;
	m->enable &= ~2U;
	for (i = 0; i < N; ++i)
		m->staged[i] = words[i].value;
	if (t41_msca_start_plan(words, N, m->active) == T41_MSCA_START_LATCH) {
		m->update = 1;
		m->pending |= 2U;
	} else {
		m->enable |= 2U;
	}
}

/* t41_msca_finish_enable(): poll the read-back per frame */
static int finish(struct msca *m, int frames)
{
	int f;

	for (f = 0; (m->pending & 2U) && f <= frames; ++f) {
		if (t41_msca_words_active(words, N, m->active)) {
			m->pending &= ~2U;
			m->enable |= 2U;
			return f;
		}
		frame(m);
	}
	return -1;
}

int main(void)
{
	static struct msca m;
	unsigned int i;

	/* reset geometry 1280x720, ratio 1:1; output 1 runs */
	set_words(0x050002d0U, 0x4000U);
	for (i = 0; i < N; ++i)
		m.active[i] = m.staged[i] = words[i].value;
	m.enable = 3U;

	/* new 640x360 at 4.5:1: switched off, latched off, enabled after */
	set_words(0x02800168U, 0x12000U);
	start(&m);
	assert(!(m.enable & 2U) && m.update && (m.pending & 2U));
	assert(finish(&m, 3) == 1);
	assert(m.enable & 2U);
	assert(m.latched_while_enabled == 0);

	/* restart with unchanged geometry: no update request, enabled now */
	start(&m);
	assert(!m.update && !(m.pending & 2U) && (m.enable & 2U));

	/* ratio bits above 19 are not compared */
	m.active[3] |= 0x80000U;
	assert(t41_msca_words_active(words, N, m.active));
	m.active[0] ^= 1U;
	assert(!t41_msca_words_active(words, N, m.active));
	m.active[0] ^= 1U;

	/* 960x540 restart: again only enabled after the latch */
	set_words(0x03c0021cU, 0xc000U);
	start(&m);
	assert(finish(&m, 0) == -1 || !(m.enable & 2U));
	assert(finish(&m, 3) >= 0 && (m.enable & 2U));
	assert(m.latched_while_enabled == 0);

	/* the old order (enable, then request) latches an enabled output */
	set_words(0x02800168U, 0x12000U);
	for (i = 0; i < N; ++i)
		m.staged[i] = words[i].value;
	m.enable |= 2U;
	m.update = 1;
	frame(&m);
	assert(m.latched_while_enabled > 0);
	puts("tx_isp_t41_msca_start_test: ok");
	return 0;
}
