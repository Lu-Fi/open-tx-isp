#!/bin/sh
# t30_ae_init_host_test.sh [driver/t30]   (Lu-Fi/openimp#338)
# Extracts AE_fsm_clear, cmos_fsm_clear, adjust_exposure and ae_exposure_correction from the T30
# driver and compares them with t30_ae_init_oracle.h, generated from the SDK-built vendor
# tx-isp-t30.ko by tools/ko_emu/diff_t30_ae_init.py: every byte of the pattern-filled AE and CMOS
# contexts after the clear (the AE accumulator and pending value, ctx+740 = the ISP gain limit of
# the AE exposure), and per adjust_exposure case the eight frame exposure sets, the accumulator and
# the cmos_set_exposure_target / raise_event arguments.  Also checks that calibration 206 of the
# default tables is the exposure-correction LUT, not a function.
d=${1:-$(dirname "$0")/../driver/t30}
f=$d/tx_isp_t30_recovered.c
o=$(dirname "$0")/t30_ae_init_oracle.h
[ -f "$f" ] && [ -f "$o" ] || { echo "usage: $0 [driver/t30]" >&2; exit 2; }
grep -q '^[[:space:]]*\*(void \*\*)((char \*)arg1 + 0x338) = &calibration_ae_exposure_correction;' "$f" || {
	echo "t30_ae_init: calibration 206 (+0x338) is not &calibration_ae_exposure_correction"; exit 1; }
CC=${CC:-cc}
t=$(mktemp -d) || exit 2
trap 'rm -rf "$t"' EXIT
for fn in 'int32_t AE_fsm_clear(void *arg1)' \
	  'void* cmos_fsm_clear(uintptr_t a0)' \
	  'int32_t adjust_exposure(void *arg1, int32_t arg2)' \
	  'int32_t ae_exposure_correction(int32_t *arg1, int32_t arg2)'; do
	n=$(grep -cFx -- "$fn" "$f")
	[ "$n" = 1 ] || { echo "t30_ae_init: '$fn' found $n times"; exit 1; }
	awk -v p="$fn" '$0 == p {on=1} on {print} on && /^}/ {exit}' "$f" >> "$t/ae.c"
done
cp "$o" "$t/oracle.h"
cat > "$t/main.c" <<'EOC'
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "oracle.h"
static int32_t isp[0x1600 / 4], sets[8], cap[3], ev[2], capped, evd, mode;
static uint32_t lut[4];
static uint8_t blk[0x440];
static int32_t log2_fixed_to_fixed(uint32_t x, int32_t fin, char fout)
{
	int msb;
	if (!x)
		return 0;
	msb = 31 - __builtin_clz(x);
	return (int32_t)(((uint32_t)(msb - fin) << fout) +
			 ((((uint32_t)x << (31 - msb)) & 0x7fffffff) >> (31 - fout)));
}
static int32_t _GET_HDR_TABLE_INDEX(int32_t a, int32_t b) { return a * 16 + b; }
static int32_t _GET_UINT_PTR(int32_t i)
{
	if (i == 146 * 16 + mode) return (int32_t)(uintptr_t)lut;
	fprintf(stderr, "_GET_UINT_PTR(%d)\n", i); return 0;
}
static void *cmos_get_frame_exposure_set(void *a, int32_t i)
{
	if ((char *)a != (char *)isp + 280 || i < 0 || i > 7) { fprintf(stderr, "frame set %d\n", i); return sets; }
	return &sets[i];
}
static void *cmos_set_exposure_target(int32_t *a, int32_t e, int32_t r)
{
	cap[0] = (int32_t)((char *)a - (char *)isp); cap[1] = e; cap[2] = r; capped = 1; return 0;
}
static int32_t apical_fw_raise_event(void *a, int32_t e)
{
	ev[0] = (int32_t)((char *)a - (char *)isp); ev[1] = e; evd = 1; return 0;
}
int32_t ae_exposure_correction(int32_t *arg1, int32_t arg2);
#include "ae.c"
int main(void)
{
	unsigned i, k, n = sizeof(t30_aei_cases) / sizeof(t30_aei_cases[0]), fail = 0, total = n + 2;
	for (i = 0; i < sizeof(t30_aei_ae); i++)
		blk[i] = (uint8_t)(i * 7 + 0x5a);
	AE_fsm_clear(blk);
	for (i = 0; i < sizeof(t30_aei_ae); i++)
		if (blk[i] != t30_aei_ae[i]) {
			printf("AE_fsm_clear: byte +%u = %#x, OEM %#x\n", i, blk[i], t30_aei_ae[i]); fail++; break;
		}
	for (i = 0; i < sizeof(t30_aei_cmos); i++)
		blk[i] = (uint8_t)(i * 7 + 0x33);
	cmos_fsm_clear((uintptr_t)blk);
	for (i = 0; i < sizeof(t30_aei_cmos); i++)
		if (blk[i] != t30_aei_cmos[i]) {
			printf("cmos_fsm_clear: byte +%u = %#x, OEM %#x\n", i, blk[i], t30_aei_cmos[i]); fail++; break;
		}
	for (k = 0; k < n; k++) {
		const struct t30_aei_case *c = &t30_aei_cases[k];
		unsigned long long acc;
		memset(isp, 0, sizeof(isp));
		mode = c->mode;
		((uint8_t *)isp)[0x18 + 5412] = (uint8_t)c->mode;
		isp[0x320 / 4] = (int32_t)(uintptr_t)((char *)isp + 0x18);
		memcpy((char *)isp + 0x320 + 24, &c->acc, 8);
		isp[820 / 4] = c->w334; isp[840 / 4] = c->w348;
		memcpy(sets, c->sets, sizeof(sets));
		lut[0] = c->k; capped = evd = 0;
		adjust_exposure(isp, c->step);
		memcpy(&acc, (char *)isp + 0x320 + 24, 8);
		if (memcmp(sets, c->sets_out, sizeof(sets)) || acc != c->acc_out || !capped ||
		    memcmp(cap, c->cap, sizeof(cap)) || !evd || (uint32_t)ev[0] != c->ev[0] ||
		    (uint32_t)ev[1] != c->ev[1]) {
			if (fail++ < 8)
				printf("adjust_exposure case %u: acc %#llx OEM %#llx, set0 %d OEM %d, cmos %d %d %d OEM %d %d %d\n",
				       k, acc, c->acc_out, sets[0], c->sets_out[0], cap[0], cap[1], cap[2],
				       c->cap[0], c->cap[1], c->cap[2]);
		}
	}
	printf("t30_ae_init: %u of %u checks equal to the vendor ko\n", total - fail, total);
	return fail ? 1 : 0;
}
EOC
$CC -O1 -no-pie -fno-pie -fno-strict-aliasing -w -o "$t/run" "$t/main.c" || { echo "t30_ae_init: build failed"; exit 1; }
"$t/run"
