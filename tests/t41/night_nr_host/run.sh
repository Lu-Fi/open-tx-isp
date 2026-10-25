#!/bin/sh
# T41 night NR/sharpen host check: runs the driver's MDNS/SDNS/YSP/DMSC interpolation + packing
# (driver/t41/tx_isp_t41_{mdns,sdns,ysp,dmsc}.h) on a tuning-bin bank and compares with ISP core dumps.
#   run.sh <bin> <bank 0|1> <log2 total gain q16> [dump.regs ...]   register table, '*' = differs
#   run.sh <bin> <bank 0|1> scan <dump.regs>                        gain ranges that reproduce the dump
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/t41_night_nr_host.XXXXXX")
cc -O1 -Wall -Wno-unused-function -o "$OUT/t" "$HERE/main.c"
rc=0
"$OUT/t" "$@" || rc=1
rm -rf "$OUT"
exit $rc
