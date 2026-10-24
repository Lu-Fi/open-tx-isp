#!/bin/sh
# T40-TEMPER-RATIO host check: the driver's t40_mdns_ratio_scale (extracted from the source) against an
# instruction-level transcription of OEM tisp_s_mdns_ratio (SDK 1.3.1 0x5f540) for all v, ratio and both caps.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../driver/t40/tx_isp_t40_recovered.c
OUT=${TMPDIR:-/tmp}/t40_mdns_ratio_host.$$
mkdir -p "$OUT"
sed -n '/^static inline uint8_t t40_mdns_ratio_scale(/,/^}/p' "$SRC" > "$OUT/scale.inc"
[ -s "$OUT/scale.inc" ] || { echo "FAIL: helper not found"; exit 1; }
cc -O2 -Wall -I"$OUT" -o "$OUT/t" "$HERE/oracle.c"
rc=0
"$OUT/t" || rc=1
python3 -I "$HERE/layout.py" "$SRC" || rc=1
rm -rf "$OUT"
exit $rc
