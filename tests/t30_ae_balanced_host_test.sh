#!/bin/sh
# t30_ae_balanced_host_test.sh [driver/t30]   (Lu-Fi/openimp#283)
# Extracts ae_calculate_target / ae_calculate_exposure_ratio / ae_calculate_exposure from the T30
# driver and runs them (target, then exposure = AE FSM states 4 and 5) on the inputs of
# t30_ae_balanced_oracle.h, generated from the SDK-built vendor tx-isp-t30.ko by
# tools/ko_emu/diff_t30_ae_balanced.py.  The math helpers and table getters are the same models
# as in the generator.  Compares AE context words 4..11, stab items 16/22 and the
# cmos_set_exposure_target arguments.
d=${1:-$(dirname "$0")/../driver/t30}
f=$d/tx_isp_t30_recovered.c
o=$(dirname "$0")/t30_ae_balanced_oracle.h
[ -f "$f" ] && [ -f "$o" ] || { echo "usage: $0 [driver/t30]" >&2; exit 2; }
CC=${CC:-cc}
t=$(mktemp -d) || exit 2
trap 'rm -rf "$t"' EXIT
for fn in 'void *ae_calculate_target(int32_t *arg1)' \
	  'uint32_t ae_calculate_exposure_ratio(int32_t *arg1)' \
	  'int32_t ae_calculate_exposure(uintptr_t a0)' \
	  'uint32_t ae_read_full_histogram_data(uintptr_t a0)'; do
	n=$(grep -cFx -- "$fn" "$f")
	[ "$n" = 1 ] || { echo "t30_ae_balanced: '$fn' found $n times"; exit 1; }
	awk -v p="$fn" '$0 == p {on=1} on {print} on && /^}/ {exit}' "$f" >> "$t/ae.c"
done
cp "$o" "$t/oracle.h"
cat > "$t/main.c" <<'EOC'
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "oracle.h"
typedef uint32_t u32;
static unsigned char stab[60] __attribute__((aligned(4)));
#define T30_STAB32(off) (*(u32 *)(stab + (off)))
static int32_t ctxbuf[0x1600 / 4], aebuf[0x440 / 4];
static uint32_t lut[6], pts[8];
static uint8_t offs[8];
static int mode, n_pts;
static uint32_t rd;
static int32_t cap[3], capped;
static int32_t log2_fixed_to_fixed(uint32_t x, int32_t fin, char fout)
{
	int msb;
	if (!x)
		return 0;
	msb = 31 - __builtin_clz(x);
	return (int32_t)(((uint32_t)(msb - fin) << fout) +
			 ((((uint32_t)x << (31 - msb)) & 0x7fffffff) >> (31 - fout)));
}
static uint32_t math_exp2(int32_t v, char fin, char fout)
{
	int32_t ip = v >> fin, sh = ip + fout - fin;
	uint64_t m = (1u << fin) + ((uint32_t)v & ((1u << fin) - 1));
	if (sh >= 40 || sh <= -40)
		return 0;
	return (uint32_t)(sh >= 0 ? m << sh : m >> -sh);
}
static int32_t sqrt32(int32_t a)
{
	uint64_t x = (uint32_t)a, r = 0;
	while ((r + 1) * (r + 1) <= x)
		r++;
	return (int32_t)r;
}
static uint64_t div64_u64(uint64_t n, uint64_t d) { return d ? n / d : 0; }
static int64_t div64_s64(int64_t n, int64_t d) { return d ? n / d : 0; }
static int32_t _GET_HDR_TABLE_INDEX(int32_t a, int32_t b) { return a * 16 + b; }
static int32_t _GET_UINT_PTR(int32_t i)
{
	if (i == 146 * 16 + mode) return (int32_t)(uintptr_t)lut;
	if (i == 206) return (int32_t)(uintptr_t)pts;
	fprintf(stderr, "_GET_UINT_PTR(%d)\n", i); return 0;
}
static int32_t _GET_UCHAR_PTR(int32_t i)
{
	if (i == 41 * 16 + mode) return (int32_t)(uintptr_t)offs;
	fprintf(stderr, "_GET_UCHAR_PTR(%d)\n", i); return 0;
}
static int32_t _GET_LEN(int32_t i) { return i == 206 ? n_pts : 0; }
static uint32_t regs[256], apibuf[256];
static int hist_mode;
static uintptr_t apical_api_buffer_data_size;
static int32_t get_apical_api_buffer(void) { return (int32_t)(uintptr_t)apibuf; }
static int32_t APICAL_READ_32(int32_t a)
{
	if (hist_mode)
		return (int32_t)regs[((uint32_t)a - 0x10000) >> 2 & 255];
	return a == 0x40114 ? (int32_t)rd : 0xdead;
}
static void *cmos_set_exposure_target(int32_t *a, int32_t e, int32_t r)
{
	cap[0] = (int32_t)((uintptr_t)a - (uintptr_t)ctxbuf); cap[1] = e; cap[2] = r; capped = 1;
	return 0;
}
void *ae_calculate_target(int32_t *arg1);
uint32_t ae_calculate_exposure_ratio(int32_t *arg1);
int32_t ae_calculate_exposure(uintptr_t a0);
uint32_t ae_read_full_histogram_data(uintptr_t a0);
#include "ae.c"
static uint32_t xorshift(uint32_t s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
int main(void)
{
	unsigned i, k, n = sizeof(t30_aeb_cases) / sizeof(t30_aeb_cases[0]), fail = 0;
	for (k = 0; k < n; k++) {
		const struct t30_aeb_case *c = &t30_aeb_cases[k];
		uint32_t s = c->seed, sum = 0, *h = (uint32_t *)&aebuf[0x30 / 4];
		int64_t q[2] = { c->acc, c->pend };
		int32_t got[8];
		memset(stab, 0, sizeof(stab)); memset(ctxbuf, 0, sizeof(ctxbuf)); memset(aebuf, 0, sizeof(aebuf));
		stab[1] = c->man_exp; stab[2] = c->man_ratio; T30_STAB32(16) = c->item16;
		stab[22] = c->item22; stab[23] = c->item23; stab[53] = c->item53;
		lut[0] = c->k; lut[1] = c->tgt; lut[2] = c->t4; lut[3] = c->pct; lut[4] = c->frames; lut[5] = 0;
		memcpy(pts, c->pts, sizeof(pts)); memcpy(offs, c->offs, sizeof(offs)); n_pts = c->n;
		mode = c->mode; rd = c->read; capped = 0;
		((uint8_t *)ctxbuf)[5412] = c->mode;
		ctxbuf[100 / 4] = c->c100; ctxbuf[104 / 4] = c->c104; ctxbuf[740 / 4] = c->c740;
		aebuf[0] = (int32_t)(uintptr_t)ctxbuf;
		aebuf[4] = c->step; aebuf[5] = c->exp;
		memcpy(&aebuf[6], q, sizeof(q));
		aebuf[0x2c / 4] = c->filt;
		for (i = 0; i < 256; i++) { s = xorshift(s); h[i] = (s >> 8) % (c->maxbin + 1); sum += h[i]; }
		if (!sum) { h[128] = 1; sum = 1; }
		aebuf[0x430 / 4] = sum;
		ae_calculate_target(aebuf);
		ae_calculate_exposure((uintptr_t)aebuf);
		for (i = 0; i < 8; i++)
			got[i] = aebuf[4 + i];
		if (memcmp(got, c->out, sizeof(got)) || T30_STAB32(16) != c->item16_out ||
		    stab[22] != c->item22_out || !capped || memcmp(cap, c->cmos, sizeof(cap))) {
			if (fail++ < 8) {
				printf("case %u mode %d man %d/%d:\n  ours", k, c->mode, c->man_exp, c->man_ratio);
				for (i = 0; i < 8; i++) printf(" %d", got[i]);
				printf(" | %#x %u | %d %d %d\n  oem ", T30_STAB32(16), stab[22], cap[0], cap[1], cap[2]);
				for (i = 0; i < 8; i++) printf(" %d", c->out[i]);
				printf(" | %#x %u | %d %d %d\n", c->item16_out, c->item22_out, c->cmos[0], c->cmos[1], c->cmos[2]);
			}
		}
	}
	hist_mode = 1;
	for (k = 0; k < sizeof(t30_aeb_hist) / sizeof(t30_aeb_hist[0]); k++, n++) {
		const unsigned *c = t30_aeb_hist[k];
		uint32_t s = c[0], hs = 0, bs = 0, *h = (uint32_t *)&aebuf[0x30 / 4];
		for (i = 0; i < 256; i++) { s = xorshift(s); regs[i] = s & (c[1] ? 0xffff : 0xfff); }
		memset(aebuf, 0, sizeof(aebuf)); memset(apibuf, 0, sizeof(apibuf));
		apical_api_buffer_data_size = 0;
		((uint8_t *)aebuf)[0x438] = c[2];
		ae_read_full_histogram_data((uintptr_t)aebuf);
		for (i = 0; i < 256; i++) { hs += h[i] * (i + 1); bs += apibuf[i] * (i + 1); }
		if ((uint32_t)aebuf[0x430 / 4] != c[3] || hs != c[4] || bs != c[5] ||
		    ((uint8_t *)aebuf)[0x438] != c[6] || apical_api_buffer_data_size != c[7]) {
			if (fail++ < 8)
				printf("hist case %u: ours %#x %#x %#x %u %lu oem %#x %#x %#x %u %u\n", k,
				       aebuf[0x430 / 4], hs, bs, ((uint8_t *)aebuf)[0x438],
				       (unsigned long)apical_api_buffer_data_size, c[3], c[4], c[5], c[6], c[7]);
		}
	}
	printf("t30_ae_balanced: %u of %u cases equal to the vendor ko\n", n - fail, n);
	return fail ? 1 : 0;
}
EOC
$CC -O1 -no-pie -fno-pie -fno-strict-aliasing -w -o "$t/run" "$t/main.c" || { echo "t30_ae_balanced: build failed"; exit 1; }
"$t/run"
