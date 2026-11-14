#!/bin/sh
# T33 binary oracle runner (openimp #99): run.sh <block>   e.g. run.sh leaf
# Builds tests/t33_binoracle/<block>.c (our driver C with the vendor calling convention) for mipsel, runs it and the
# vendor tx-isp-t33.ko functions of <block>.conf (FUNCS) in Unicorn (tools/ko_emu/t33_binoracle.py) and compares
# return values, pointer buffers and listed globals; then every one-token mutation of MUTATIONS must be detected.
# Needs T33_OEM_KO (vendor module, local only), T33_CC (else T40_CC: a mipsel-linux-gcc) and python3 unicorn +
# pyelftools; SKIP (exit 0) without them, an error with T40_ORACLE_STRICT=1.  N = cases per function (default 2000).
H=$(cd "$(dirname "$0")" && pwd); B=$1
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
[ -n "$B" ] && [ -r "$H/$B.conf" ] || { echo "usage: run.sh <block> (tests/t33_binoracle/<block>.conf)"; exit 2; }
if [ ! -r "$KO" ] || [ ! -x "$CC" ] || ! python3 -c "import unicorn, elftools" 2>/dev/null; then
	echo "t33_binoracle $B: SKIP (T33_OEM_KO / T33_CC / python3 unicorn+pyelftools missing)"; [ -z "$T40_ORACLE_STRICT" ] || exit 1; exit 0
fi
. "$H/$B.conf"
[ -z "$RUN" ] || exec "$H/$RUN"	# a block proven by another oracle script (lcepf.conf)
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
printf '%s\n' "$FUNCS" > "$W/spec"
PY="python3 $H/../../tools/ko_emu/t33_binoracle.py"
"$H/build_emu.sh" "$CC" "$H/$SRC" "$W/ok.o" || { echo "t33_binoracle $B: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" "$W/spec" --n "${N:-2000}" || { echo "t33_binoracle $B: FAIL"; exit 1; }
NM=0; NF=0; i=0
printf '%s\n' "$MUTATIONS" | grep . > "$W/muts" || true
while IFS= read -r m; do
	i=$((i + 1)); rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$m" "$W/d" <<'PY' || { echo "t33_binoracle $B: mutation $i text not found: $m"; exit 1; }
import sys
f, a, b = sys.argv[1].split('|', 2); p = sys.argv[2] + '/' + f; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/$SRC" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle $B: mutation $i does not build: $m"; exit 1; }
	if $PY "$KO" "$W/m.o" "$W/spec" --n "${MUT_N:-400}" > /dev/null; then echo "t33_binoracle $B: mutation $i NOT detected: $m"; NF=$((NF + 1)); else NM=$((NM + 1)); fi
done < "$W/muts"
echo "t33_binoracle $B: mutations $NM detected, $NF not detected"
[ $NF = 0 ] || exit 1
echo "t33_binoracle $B: PASS"
