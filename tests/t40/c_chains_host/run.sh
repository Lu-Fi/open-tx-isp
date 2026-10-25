#!/bin/sh
# T40 C chains vs the literal OEM chains on the host.
#   run.sh <chain> <sensor-t40.bin>...      chain: dpc
# The literal reference is taken from git (LIT_REF, the last commit that built it into the driver).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
LIT_REF=${LIT_REF:-6d63667b}
CHAIN=$1; shift
OUT=$(mktemp -d "${TMPDIR:-/tmp}/t40_c_chains.XXXXXX")
mkdir -p "$OUT/lit"
case "$CHAIN" in
dpc) git -C "$REPO" show "$LIT_REF:driver/t40/tx_isp_t40_dpc_lit.inc" | sed 's/regtrace_t40_dpc_on/lit_t40_dpc_on/g' > "$OUT/lit/dpc_lit.inc" ;;
*) echo "unknown chain $CHAIN"; exit 2 ;;
esac

cc -O0 -g -no-pie -fno-pie -w -pthread -I"$OUT" -o "$OUT/t" "$HERE/$CHAIN.c"
rc=0
"$OUT/t" "$@" || rc=1
rm -rf "$OUT"
exit $rc
