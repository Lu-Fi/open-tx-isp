#!/bin/sh
# T31 binary oracle runner (openimp #533; harness of tests/t33_binoracle): run.sh <block>   e.g. run.sh leaf
# Builds tests/t31_binoracle/<block>.c (our driver C with the vendor calling convention) for mipsel, runs it and the
# vendor tx-isp-t31.ko functions of <block>.conf (FUNCS) in Unicorn (tools/ko_emu/t33_binoracle.py) and compares
# return values, pointer buffers and listed globals; then every one-token mutation of MUTATIONS must be detected.
# Needs T31_OEM_KO (vendor module, local only), T31_CC (else T33_CC, T40_CC: a mipsel-linux-gcc) and python3 unicorn +
# pyelftools; SKIP (exit 0) without them, an error with T40_ORACLE_STRICT=1.  N = cases per function (default 2000).
H=$(cd "$(dirname "$0")" && pwd); B=$1
KO=${T31_OEM_KO:-}; CC=${T31_CC:-${T33_CC:-${T40_CC:-}}}
[ -n "$B" ] && [ -r "$H/$B.conf" ] || { echo "usage: run.sh <block> (tests/t31_binoracle/<block>.conf)"; exit 2; }
if [ ! -r "$KO" ] || [ ! -x "$CC" ] || ! python3 -c "import unicorn, elftools" 2>/dev/null; then
	echo "t31_binoracle $B: SKIP (T31_OEM_KO / T31_CC / python3 unicorn+pyelftools missing)"; [ -z "$T40_ORACLE_STRICT" ] || exit 1; exit 0
fi
python3 "$H/../t33_binoracle/proof_audit.py" --mutation-conf "$H/$B.conf" >&2 || { echo "t31_binoracle $B: FAIL mutation guard (0 mutations or a stray quote in MUTATIONS)"; exit 1; }
. "$H/$B.conf"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
printf '%s\n' "$FUNCS" > "$W/spec"
PY="python3 $H/../../tools/ko_emu/t33_binoracle.py"
# EXTRACT='file.c FUNC ..' (optional): the static FUNCs of driver/t31/file.c are copied into t31_extract.h of the build
# directory (tests/t31_binoracle/extract.py), so code that lives in a .c is tested; MUTATIONS may then name file.c
ext() { [ -z "${EXTRACT:-}" ] || python3 "$H/extract.py" "$1/${EXTRACT%% *}" "$1/t31_extract.h" ${EXTRACT#* }; }
DIR=$H/../../driver/t31
if [ -n "${EXTRACT:-}" ]; then
	DIR=$W/x; mkdir -p "$DIR"; cp "$H"/../../driver/t31/*.h "$H/../../driver/t31/${EXTRACT%% *}" "$DIR/"; ext "$DIR" || exit 1
fi
"$H/../t33_binoracle/build_emu.sh" "$CC" "$H/$SRC" "$W/ok.o" "$DIR" || { echo "t31_binoracle $B: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" "$W/spec" --n "${N:-2000}" || { echo "t31_binoracle $B: FAIL"; exit 1; }
NM=0; NF=0; i=0
printf '%s\n' "$MUTATIONS" | grep . > "$W/muts" || true
while IFS= read -r m; do
	i=$((i + 1)); rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t31/*.h "$W/d/"
	[ -z "${EXTRACT:-}" ] || cp "$H/../../driver/t31/${EXTRACT%% *}" "$W/d/"
	python3 - "$m" "$W/d" <<'PY' || { echo "t31_binoracle $B: mutation $i text not found: $m"; exit 1; }
import sys
f, a, b = sys.argv[1].split('|', 2); p = sys.argv[2] + '/' + f; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	ext "$W/d" || exit 1
	"$H/../t33_binoracle/build_emu.sh" "$CC" "$H/$SRC" "$W/m.o" "$W/d" 2>/dev/null || { echo "t31_binoracle $B: mutation $i does not build: $m"; exit 1; }
	if $PY "$KO" "$W/m.o" "$W/spec" --n "${MUT_N:-400}" > /dev/null; then echo "t31_binoracle $B: mutation $i NOT detected: $m"; NF=$((NF + 1)); else NM=$((NM + 1)); fi
done < "$W/muts"
echo "t31_binoracle $B: mutations $NM detected, $NF not detected"
[ $((NM + NF)) -gt 0 ] || { echo "t31_binoracle $B: FAIL 0 mutations ran (a block with no mutation proves nothing; check MUTATIONS quoting, openimp #99)"; exit 1; }
[ $NF = 0 ] || exit 1
echo "t31_binoracle $B: PASS"
