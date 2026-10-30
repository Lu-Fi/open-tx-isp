#!/bin/bash
# make check guard for the T40 bit-exact / alignment claims (review-week-1030 M5, #133):
#  1. c_chains_host adr: C ADR chain (aligned statistics, 3dc2b4be) vs the literal OEM chain, register writes
#     compared on T40_TUNING_BIN plus one fuzz seed;
#  2. t40_vawb_host LA_REF=316259cb FUZZL=1 / CTD_REF=f2c180f7 FUZZ=1: the C tisp_awb_long_alogrithm and
#     tisp_awb_ct_detect (tx_isp_t40_awb_long.inc / _ctd.inc) against the translated OEM code on every call
#     (needs T40_OEM_DATA = objcopy -O binary --only-section=.data of the OEM tx-isp-t40.ko);
#  3. optional: T40_AWB_DUMPS="<device dump>..." replayed with both references (0 differ, 0 B vs device).
# Parts whose inputs are absent SKIP (exit 0), like t40_vct_host.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${T40_TUNING_BIN:-}; DAT=${T40_OEM_DATA:-}; rc=0
if [ ! -r "$BIN" ]; then echo "t40_bitexact_host: SKIP (no tuning bin: set T40_TUNING_BIN [and T40_OEM_DATA])"; exit 0; fi
out=$("$HERE/t40/c_chains_host/run.sh" adr "$BIN" fuzz:1 2>&1) || rc=1
echo "$out" | tail -1
echo "$out" | grep -q " 0 differences" || { echo "t40_bitexact_host: FAIL adr"; rc=1; }
if [ -r "$DAT" ]; then
	for v in "LA_REF=316259cb FUZZL=1" "CTD_REF=f2c180f7 FUZZ=1"; do
		out=$(env $v "$HERE/t40_vawb_host.sh" "$BIN" "$DAT" 128 128 128 50 2>&1) || rc=1
		n=$(echo "$out" | grep -c "DIFF" || true); f=$(echo "$out" | grep -c "^f[0-9]* faults" || true)
		echo "vawb $v: $f frames, $n DIFF lines"
		[ "$n" = 0 ] && [ "$f" -gt 0 ] || { echo "t40_bitexact_host: FAIL vawb $v"; rc=1; }
	done
	for d in $T40_AWB_DUMPS; do
		out=$(LA_REF=316259cb CTD_REF=f2c180f7 "$HERE/t40_vawb_host.sh" replay "$d" 2>&1) || rc=1
		echo "$out" | grep -E "differ|diff vs device" | sed "s|^|$(basename "$d"): |"
		echo "$out" | grep -E "differ" | grep -vq " 0 differ" && rc=1
		echo "$out" | grep -q "inter 0 B" || rc=1
	done
else
	echo "t40_bitexact_host: vawb part SKIP (no T40_OEM_DATA)"
fi
[ $rc = 0 ] && echo "t40_bitexact_host: PASS" || echo "t40_bitexact_host: FAIL"
exit $rc
