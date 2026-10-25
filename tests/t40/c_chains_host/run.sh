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
lce) git -C "$REPO" show "$LIT_REF:driver/t40/tx_isp_t40_lce_lit.inc" | sed 's/regtrace_t40_lce_on/lit_t40_lce_on/g' > "$OUT/lit/lce_lit.inc" ;;
*) echo "unknown chain $CHAIN"; exit 2 ;;
esac
# count inputs where the OEM code is undefined: divisions by zero (MIPS result unpredictable),
# LCE histogram knots past bin 31 (the OEM writes past its stack array)
sed -i -E 's/if \((r_[a-z0-9]+)\) \{ mips_lo =/if (!\1) lit_ub++; if (\1) { mips_lo =/' "$OUT/lit/"*.inc
[ "$CHAIN" = lce ] && sed -i -E '/^static int32_t lce_hist_method_lit\(uint32_t a0_in.*\)$/{n;s/^\{$/{ if (((uint32_t *)(uintptr_t)a2_in)[2] > 31) lit_ub++;/}' "$OUT/lit/lce_lit.inc"

cc -O0 -g -no-pie -fno-pie -w -pthread -I"$OUT" -o "$OUT/t" "$HERE/$CHAIN.c"
rc=0
"$OUT/t" "$@" || rc=1
[ -n "$KEEP" ] && echo "kept $OUT" || rm -rf "$OUT"
exit $rc
