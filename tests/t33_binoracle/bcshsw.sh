#!/bin/sh
# T33 binary oracle block "bcshsw" (openimp #507): tests/t33_binoracle/bcshsw.c (driver/t32/tx_isp_t33_bcsh.h
# t33_bcsh_switch_state) vs the vendor tisp_bcsh_switch_para in Unicorn (tools/ko_emu/diff_t33_bcshsw.py), then
# one-token mutations of the header that must be detected (lines: header @ from @ to).
# Needs T33_OEM_KO, T33_CC (else T40_CC) and python3 unicorn + pyelftools, else SKIP (run.sh checks).
H=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
PY="python3 $H/../../tools/ko_emu/diff_t33_bcshsw.py"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$H/build_emu.sh" "$CC" "$H/bcshsw.c" "$W/ok.o" || { echo "t33_binoracle bcshsw: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" --n "${N:-2000}" > "$W/out" || { tail -4 "$W/out"; echo "t33_binoracle bcshsw: FAIL"; exit 1; }
tail -1 "$W/out"
NM=0; NF=0
while IFS='@' read -r f a b; do
	[ -n "$f" ] || continue
	rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$W/d/$f" "$a" "$b" <<'PY' || { echo "t33_binoracle bcshsw: mutation text not found: $a"; exit 1; }
import sys
p, a, b = sys.argv[1:4]; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/bcshsw.c" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle bcshsw: mutation does not build: $a"; exit 1; }
	if $PY "$KO" "$W/m.o" --n 600 > /dev/null; then
		echo "t33_binoracle bcshsw: mutation NOT detected: $a => $b"; NF=$((NF + 1))
	else NM=$((NM + 1)); fi
done <<'M'
tx_isp_t33_bcsh.h@	if (sec[0x4a3] != 1 && (sec[0x4a3] != 0 || sec[0x4a4] > 1) && st[0x1d0]) {@	if (sec[0x4a3] != 1 && (sec[0x4a3] != 0 || sec[0x4a4] > 2) && st[0x1d0]) {
tx_isp_t33_bcsh.h@			seg = (n - 1) * 2;@			seg = (n - 1) * 2 + 1;
tx_isp_t33_bcsh.h@						seg = (i - 1) * 2 + 1;@						seg = (i - 1) * 2;
tx_isp_t33_bcsh.h@				} else if (ct <= KCT(i) + HY(i)) {@				} else if (ct < KCT(i) + HY(i)) {
tx_isp_t33_bcsh.h@	t32_bcsh_p32(st, 0x24, (u32)seg);@	t32_bcsh_p32(st, 0x24, (u32)seg + 1);
tx_isp_t33_bcsh.h@		t32_bcsh_p32(dst, 0x10, 0x400);@		t32_bcsh_p32(dst, 0x14, 0x400);
tx_isp_t33_bcsh.h@				if (ct <= KCT(j) + HY(j)) {@				if (ct < KCT(j) + HY(j)) {
tx_isp_t33_bcsh.h@(u32)(m < -0x2000 ? -0x2000 :@(u32)(m < -0x1fff ? -0x1fff :
tx_isp_t33_bcsh.h@		memcpy(st + 0x1d4, dst, 0x24);@		memcpy(st + 0x1d8, dst, 0x20);
tx_isp_t33_bcsh.h@	t32_bcsh_p32(st, sec[0x4c0] ? 0x10 : 8, (u32)ev);@	t32_bcsh_p32(st, sec[0x4c0] ? 0x10 : 8, (u32)ev + 1);
tx_isp_t33_bcsh.h@	if (st[0x2b] == 1)@	if (st[0x2b] == 2)
tx_isp_t33_bcsh.h@	t32_bcsh_p32(st, sec[0x4c0] ? 0x14 : 0xc, (u32)(ev >> 32));@	t32_bcsh_p32(st, sec[0x4c0] ? 0x18 : 0xc, (u32)(ev >> 32));
M
echo "t33_binoracle bcshsw: mutations $NM detected, $NF not detected"
[ $NF = 0 ] || exit 1
echo "t33_binoracle bcshsw: PASS"
