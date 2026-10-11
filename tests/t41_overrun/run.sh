#!/bin/sh
# T41 out-of-object access scan (needs a kernel-tree build, not part of
# "make check").
#
#   run.sh <built driver/t41 dir> <cross prefix> [baseline-layout.txt]
#
# The driver dir must contain tx-isp-t41.o, tx_isp_t41_recovered.o and
# tx_isp_t41_recovered.s (make ... M=<dir> tx_isp_t41_recovered.s).
# Prints every compiled load/store at &SYM + N beyond sizeof(SYM) (GCC
# verbose-asm MEM annotations), marked REACH when the function is reachable.
# Fails when a reachable store is not in allowed.txt.  With a baseline it
# also prints the .bss/.data layout difference of tx_isp_t41_recovered.o
# (informational: GCC reorders the recovered statics after almost any code
# change, so anchor-relative recovered accesses hit different neighbours
# per build); write a baseline with "run.sh <dir> <prefix> --save <file>".
set -eu
D=$1; X=$2; H=$(dirname "$0"); T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
"${X}objdump" -t "$D/tx-isp-t41.o" > "$T/t"
"${X}objdump" -r "$D/tx-isp-t41.o" > "$T/r"
python3 -I "$H/reach.py" "$T/t" "$T/r" > "$T/live"
"${X}nm" -S "$D/tx_isp_t41_recovered.o" > "$T/nm"
python3 -I "$H/asmscan.py" "$D/tx_isp_t41_recovered.s" "$T/nm" "$T/live" | tee "$T/scan"
rc=0
grep '^W    REACH' "$T/scan" | awk '{print $3, $4}' | sed 's/ (size.*//' | while read -r fn acc; do
	grep -qx "$fn $acc" "$H/allowed.txt" || { echo "NEW reachable out-of-object store: $fn $acc"; echo x >> "$T/fail"; }
done
[ -f "$T/fail" ] && rc=1
"${X}objdump" -t "$D/tx_isp_t41_recovered.o" | awk '$4==".bss"||$4==".data"{print $4,$1,$5,$6}' | sed -E 's/\.[0-9]+$//' | sort > "$T/layout"
if [ "${3:-}" = "--save" ]; then cp "$T/layout" "$4"; echo "layout saved to $4"
elif [ -n "${3:-}" ]; then
	if diff -u "$3" "$T/layout" > "$T/ldiff"; then echo "layout: identical"; else cat "$T/ldiff"; echo "layout: changed (informational)"; fi
fi
exit $rc
