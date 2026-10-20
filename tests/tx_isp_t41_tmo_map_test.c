#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_tmo_map.h"

static unsigned char params[T41_TMO_PARAM_BYTES];
static unsigned int sums[3750], counts[3750], history[3750], output[3750];

static void run(int first)
{
	assert(!t41_tmo_map(params, sizeof(params), 8, sums, counts, history, first, output));
}

static unsigned int lcg_state = 12345;
static unsigned int lcg(void)
{
	lcg_state = lcg_state * 1103515245U + 12345U;
	return lcg_state >> 8;
}

/* t41_tmo_map_fast() must match the reference bit for bit, including the
 * 32-bit wrap of large luma products and both kernel modes. */
static void fast_equivalence(void)
{
	static struct t41_tmo_map_scratch ws;
	static unsigned int ref[3750], fast[3750];
	unsigned int round, i;

	for (round = 0; round < 4000; ++round) {
		unsigned int shift = lcg() % 10, big = lcg() % 4;
		int first = lcg() & 1, r1, r2;

		memset(params, 0, sizeof(params));
		params[0x1742] = lcg() % 5;
		params[0x1739] = lcg() & 1;
		params[0x173c] = lcg() & 1;
		params[0x173b] = lcg() % 11;
		params[0x173a] = lcg() % 15;
		for (i = 0; i < 375; ++i) {
			params[0x230 + i] = lcg() % 11;
			params[0x3a7 + i] = lcg() % 15;
		}
		for (i = 0; i < 3750; ++i) {
			unsigned int limit = big == 0 ? 65536 : big == 1 ? 4096 : 256;

			counts[i] = lcg() % limit;
			sums[i] = big == 3 ? 65535 : lcg() % limit;
			history[i] = lcg() % 4096;
		}
		r1 = t41_tmo_map(params, sizeof(params), shift, sums, counts, history, first, ref);
		r2 = t41_tmo_map_fast(params, sizeof(params), shift, sums, counts, history, first, fast, &ws);
		assert(r1 == r2);
		if (!r1)
			assert(!memcmp(ref, fast, sizeof(ref)));
	}
	puts("T41 local TMO: fast map matches the reference (4000 random frames)");
}

int main(void)
{
	unsigned int b, i;
	params[0x1739] = 1;
	params[0x173b] = 3;
	params[0x173a] = 2;
	params[0x1742] = 4;
	/* Missing bins are zero padded; the lower-coordinate clamp is optional. */
	run(1);
	for (i = 0; i < 3750; ++i) assert(output[i] == 0);
	params[0x173c] = 1;
	run(1);
	for (b = 0; b < 10; ++b)
		for (i = 0; i < 375; ++i)
			assert(output[b*375+i] == (b*4095+5)/10);
	for (i = 0; i < 3750; ++i) history[i] = 4095;
	run(0);
	assert(output[0] == (4095*15)/16);
	assert(output[9*375] == (4095*15+3686)/16);
	/* A single populated histogram bin changes a neighbourhood, not a
	 * hard-coded scene map. With radius zero it must not affect other tiles. */
	params[0x173c] = 0;
	params[0x1742] = 0;
	counts[187*10+5] = 4096;
	sums[187*10+5] = 4096;
	run(1);
	assert(output[5*375+187] > 2000);
	assert(output[5*375+186] == 0);
	assert(output[5*375+188] == 0);
	params[0x1742] = 4;
	run(1);
	assert(output[5*375+186] > 2000);
	assert(output[5*375+188] == output[5*375+186]);
	assert(output[5*375+162] == output[5*375+186]);
	/* Rejection is atomic, including invalid values late in each buffer. */
	memset(output, 0x5a, sizeof(output));
	params[0x1739] = 2;
	assert(t41_tmo_map(params, sizeof(params), 8, sums, counts, history, 0, output));
	params[0x1739] = 0;
	params[0x3a7+374] = 15;
	assert(t41_tmo_map(params, sizeof(params), 8, sums, counts, history, 0, output));
	params[0x3a7+374] = 0;
	counts[3749] = 65536;
	assert(t41_tmo_map(params, sizeof(params), 8, sums, counts, history, 0, output));
	counts[3749] = 0;
	history[3749] = 4096;
	assert(t41_tmo_map(params, sizeof(params), 8, sums, counts, history, 0, output));
	history[3749] = 0;
	assert(t41_tmo_map(params, sizeof(params)-1, 8, sums, counts, history, 0, output));
	assert(t41_tmo_map(params, sizeof(params), 10, sums, counts, history, 0, output));
	assert(t41_tmo_map(NULL, sizeof(params), 8, sums, counts, history, 0, output));
	for (i = 0; i < 3750; ++i) assert(output[i] == 0x5a5a5a5a);
	puts("T41 scalar local TMO: padding, spatial response, history and rejection passed");
	fast_equivalence();
	return 0;
}
