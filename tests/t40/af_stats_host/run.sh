#!/bin/sh
# T40 AF statistics unpack + metrics (driver/t40/tx_isp_t40_af.h) on the host.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d "${TMPDIR:-/tmp}/t40_af_stats.XXXXXX")
cc -O1 -Wall -Wextra -Werror -Wno-unused-function -o "$OUT/t" "$HERE/main.c"
rc=0
"$OUT/t" || rc=1
rm -rf "$OUT"
exit $rc
