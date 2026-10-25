#!/bin/sh
# T40 day/night: BCSH day registers after day->night(saturation 0)->day x3 must equal the boot day ones.
# run.sh <sensor-t40.bin> [ev]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${TMPDIR:-/tmp}/t40_bcsh_dn_host.$$
mkdir -p "$OUT"
cc -O0 -no-pie -fno-pie -w -pthread -o "$OUT/t" "$HERE/main.c"
rc=0
"$OUT/t" "$@" || rc=1
rm -rf "$OUT"
exit $rc
