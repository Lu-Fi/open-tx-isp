#!/bin/sh
# check_t30_awb_preset.sh [driver/t30]  (Lu-Fi/openimp#202)
# Host test of the AWB scene presets against the vendor ko oracle, with a mutation check: the test must
# fail for the old lift (inverted get_awb_idx range test, 4 u16 per row) and for an off-by-one table;
# and awb_mode in the driver must use the helpers.
d=${1:-$(dirname "$0")/../driver/t30}; t=$(cd "$(dirname "$0")" && pwd)
h=$d/tx_isp_t30_awb_preset.h; src=$t/t30_awb_preset_host_test.c
[ -f "$h" ] || { echo "usage: $0 [driver/t30]" >&2; exit 2; }
h=$(cd "$(dirname "$h")" && pwd)/$(basename "$h")
CC=${CC:-cc}; W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
bld() { $CC -std=c99 -Wall -Wextra -Werror -I"$t" -DAWB_HDR="\"$1\"" -o "$W/t" "$src"; }
run() { bld "$1" && "$W/t" >"$W/out" 2>&1; }
run "$h" || { echo "check_t30_awb_preset: FAIL"; cat "$W/out"; exit 1; }
cat "$W/out"
fail=0; n=0
mut() {
	n=$((n + 1))
	sed "$2" "$h" > "$W/m.h"
	cmp -s "$h" "$W/m.h" && { echo "check_t30_awb_preset: mutation $1 did not apply"; fail=1; return; }
	bld "$W/m.h" || { echo "check_t30_awb_preset: mutation $1 does not compile"; fail=1; return; }
	if "$W/t" >"$W/out" 2>&1; then echo "check_t30_awb_preset: mutation $1 NOT detected"; fail=1; else echo "check_t30_awb_preset: mutation $1 detected"; fi
}
mut old-lift-row0 's/return t30_awb_idx_table\[idx\];/return 0 * t30_awb_idx_table[idx];/'
mut old-lift-stride 's/lut + 2 \* t30_get_awb_idx/lut + 4 * t30_get_awb_idx/'
mut table-from-0 's/{ 1, 2, 3, 4, 5, 6 }/{ 0, 1, 2, 3, 4, 5 }/'
mut no-shift 's/(row\[1\] >> 1)/(row[1])/'
f=$d/tx_isp_t30_recovered.c
for c in 'return t30_get_awb_idx(arg1);' 't30_awb_preset_gains(lut, arg2, &red, &blue);'; do
	grep -qF -- "$c" "$f" || { echo "check_t30_awb_preset: missing in driver: $c"; fail=1; }
done
[ $fail = 0 ] && echo "check_t30_awb_preset: ok ($n/$n mutations detected, awb_mode uses the helpers)"
exit $fail
