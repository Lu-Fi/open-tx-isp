#!/bin/sh
# T33 binary oracle block "clmct" (openimp #507): tests/t33_binoracle/clmct.c (driver/t32/tx_isp_t33_bcsh.h
# t33_bcsh_ct_update) vs the vendor tisp_bcsh_ct_update in Unicorn (tools/ko_emu/diff_t33_clmct.py), then
# one-token mutations of the header that must be detected (lines: header @ from @ to).
# Needs T33_OEM_KO, T33_CC (else T40_CC) and python3 unicorn + pyelftools, else SKIP (run.sh checks).
H=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
PY="python3 $H/../../tools/ko_emu/diff_t33_clmct.py"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$H/build_emu.sh" "$CC" "$H/clmct.c" "$W/ok.o" || { echo "t33_binoracle clmct: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" --n "${N:-600}" > "$W/out" || { tail -4 "$W/out"; echo "t33_binoracle clmct: FAIL"; exit 1; }
tail -1 "$W/out"
NM=0; NF=0
while IFS='@' read -r f a b; do
	[ -n "$f" ] || continue
	rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$W/d/$f" "$a" "$b" <<'PY' || { echo "t33_binoracle clmct: mutation text not found: $a"; exit 1; }
import sys
p, a, b = sys.argv[1:4]; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/clmct.c" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle clmct: mutation does not build: $a"; exit 1; }
	if $PY "$KO" "$W/m.o" --n 300 > /dev/null; then
		echo "t33_binoracle clmct: mutation NOT detected: $a => $b"; NF=$((NF + 1))
	else NM=$((NM + 1)); fi
done <<'M'
tx_isp_t33_clm.h@	if (*seg == (u8)s && !(s & 1))@	if (*seg == (u8)s && (s & 1))
tx_isp_t33_clm.h@	if ((int)d <= (int)hy)@	if ((int)d < (int)hy)
tx_isp_t33_clm.h@	*seg = (u8)s;@	*seg = (u8)(s + 1);
tx_isp_t33_clm.h@	*last = (u32)ct;@	*last = (u32)ct + 1;
tx_isp_t33_clm.h@				return (2 * (i - 1) + 1) & 0xff;@				return (2 * (i - 1)) & 0xff;
tx_isp_t33_clm.h@			return (2 * i) & 0xff;@			return (2 * i + 2) & 0xff;
tx_isp_t33_clm.h@		return KCT(0) + HY(0) < ct ? n : 0;@		return KCT(0) + HY(0) <= ct ? n : 0;
tx_isp_t33_clm.h@			f = (u32)((ct - base) * 0x1000) / (u32)(up - base);@			f = (u32)((ct - base) * 0x1001) / (u32)(up - base);
M
echo "t33_binoracle clmct: mutations $NM detected, $NF not detected"
[ $((NM + NF)) -gt 0 ] || { echo "t33_binoracle clmct: FAIL 0 mutations ran (a block with no mutation proves nothing; check MUTATIONS quoting, openimp #99)"; exit 1; }
[ $NF = 0 ] || exit 1
echo "t33_binoracle clmct: PASS"
