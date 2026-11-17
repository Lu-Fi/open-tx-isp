#!/bin/sh
# T10/T20 binary oracle runner (openimp #539 vendor proof): run.sh <block>   e.g. run.sh helpers
# Builds our shared T10/T20 firmware object (tests/t20_fw make build/mips-ours-O0.o, the object vdiff uses), runs it and the
# vendor tx-isp-t10.ko functions of <block>.conf (FUNCS) in Unicorn (tools/ko_emu/t33_binoracle.py) and compares return
# values, pointer buffers, recorded calls and listed globals on N random cases; then every one-token mutation of MUTATIONS
# (applied to a copy of driver/t20/tx_isp_t20_firmware.c in a temporary tree; '@func:TEXT|NEW' = the first TEXT after the
# definition of func) must be detected.
# Needs T10_OEM_KO (vendor tx-isp-t10.ko, local only), T10_CC (mipsel-linux-gcc; T20FW_MIPS_CC is used too), the SDK
# submodule (external/ingenic-sdk) and python3 unicorn + pyelftools; SKIP (exit 0) without them, error with T40_ORACLE_STRICT=1.
H=$(cd "$(dirname "$0")" && pwd); B=$1; R=$(cd "$H/../.." && pwd)
KO=${T10_OEM_KO:-}; CC=${T10_CC:-${T20FW_MIPS_CC:-}}
SDK=$R/external/ingenic-sdk/3.10.14/isp/t20
[ -n "$B" ] && [ -r "$H/$B.conf" ] || { echo "usage: run.sh <block> (tests/t10_binoracle/<block>.conf)"; exit 2; }
if [ ! -r "$KO" ] || [ ! -x "$CC" ] || [ ! -d "$SDK" ] || ! python3 -c "import unicorn, elftools" 2>/dev/null; then
	echo "t10_binoracle $B: SKIP (T10_OEM_KO / T10_CC / SDK submodule / python3 unicorn+pyelftools missing)"; [ -z "$T40_ORACLE_STRICT" ] || exit 1; exit 0
fi
python3 "$H/../t33_binoracle/proof_audit.py" --mutation-conf "$H/$B.conf" >&2 || { echo "t10_binoracle $B: FAIL mutation guard (0 mutations or a stray quote in MUTATIONS)"; exit 1; }
. "$H/$B.conf"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
printf '%s\n' "$FUNCS" > "$W/spec"
PY="python3 $H/../../tools/ko_emu/t33_binoracle.py"
mkfw() {  # tree-root out.o: build the firmware object of that tree
	# -mlong-calls: calls to undefined symbols (_GET_*, memcpy, register access) reach the emulator stubs (a jal cannot)
	make -s -C "$1/tests/t20_fw" OUT="$W/b$$" T20FW_MIPS_CC="$CC" CROSS_COMPILE="${CC%gcc}" SDK="$SDK" \
		FLAGS_O0="-O0 -fno-tree-dce -fno-ipa-pure-const -fno-ipa-reference -mlong-calls" "$W/b$$/mips-ours-O0.o" >"$W/build.log" 2>&1 &&
		cp "$W/b$$/mips-ours-O0.o" "$2" && rm -rf "$W/b$$"
}
"$CC" -c -O1 -G0 -mno-abicalls -fno-pic -msoft-float -o "$W/glob.o" "$H/globals.c" || { echo "t10_binoracle $B: FAIL globals"; exit 1; }
mk() {  # tree-root out.o: firmware object of that tree + the globals our object only references
	mkfw "$1" "$W/fw.o" && "${CC%gcc}ld" -r -o "$2" "$W/fw.o" "$W/glob.o"
}
mk "$R" "$W/ok.o" || { echo "t10_binoracle $B: FAIL build"; cat "$W/build.log" | tail -5; exit 1; }
$PY "$KO" "$W/ok.o" "$W/spec" --n "${N:-2000}" || { echo "t10_binoracle $B: FAIL"; exit 1; }
NM=0; NF=0; i=0
printf '%s\n' "$MUTATIONS" | grep . > "$W/muts" || true
mkdir -p "$W/t/tests" "$W/t/driver"
cp -r "$R/tests/t20_fw" "$W/t/tests/"; rm -rf "$W/t/tests/t20_fw/build"
for d in "$R"/driver/*; do [ "$(basename "$d")" = t20 ] || ln -s "$d" "$W/t/driver/"; done
ln -s "$R/external" "$W/t/external"
while IFS= read -r m; do
	i=$((i + 1)); rm -rf "$W/t/driver/t20"; cp -r "$R/driver/t20" "$W/t/driver/t20"
	python3 - "$m" "$W/t/driver/t20/tx_isp_t20_firmware.c" <<'PY' || { echo "t10_binoracle $B: mutation $i text not found: $m"; exit 1; }
import re, sys
a, b = sys.argv[1].split('|', 1); p = sys.argv[2]; t = open(p).read()
start = 0
m = re.match(r'@(\w+):(.*)', a, re.S)          # @func:TEXT = first TEXT after the definition of func
if m:
    d = re.search(r'^[A-Za-z].*\b%s\([^;]*$' % m.group(1), t, re.M)
    if not d: sys.exit(1)
    start, a = d.start(), m.group(2)
i = t.find(a, start)
if i < 0: sys.exit(1)
open(p, 'w').write(t[:i] + b + t[i + len(a):])
PY
	mk "$W/t" "$W/m.o" || { echo "t10_binoracle $B: mutation $i does not build: $m"; exit 1; }
	if $PY "$KO" "$W/m.o" "$W/spec" --n "${MUT_N:-400}" > /dev/null; then echo "t10_binoracle $B: mutation $i NOT detected: $m"; NF=$((NF + 1)); else NM=$((NM + 1)); fi
done < "$W/muts"
echo "t10_binoracle $B: mutations $NM detected, $NF not detected"
[ $((NM + NF)) -gt 0 ] || { echo "t10_binoracle $B: FAIL 0 mutations ran (a block with no mutation proves nothing; check MUTATIONS quoting, openimp #99)"; exit 1; }
[ $NF = 0 ] || exit 1
echo "t10_binoracle $B: PASS"
