#!/bin/sh
# T33 binary oracle block "bcshct" (openimp #507): tests/t33_binoracle/bcshct.c (driver/t32/tx_isp_t33_bcsh.h
# t33_bcsh_ct_update) vs the vendor tisp_bcsh_ct_update in Unicorn (tools/ko_emu/diff_t33_bcshct.py), then
# one-token mutations of the header that must be detected (lines: header @ from @ to).
# Needs T33_OEM_KO, T33_CC (else T40_CC) and python3 unicorn + pyelftools, else SKIP (run.sh checks).
H=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
PY="python3 $H/../../tools/ko_emu/diff_t33_bcshct.py"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$H/build_emu.sh" "$CC" "$H/bcshct.c" "$W/ok.o" || { echo "t33_binoracle bcshct: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" --n "${N:-2000}" > "$W/out" || { tail -4 "$W/out"; echo "t33_binoracle bcshct: FAIL"; exit 1; }
tail -1 "$W/out"
NM=0; NF=0
while IFS='@' read -r f a b; do
	[ -n "$f" ] || continue
	rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$W/d/$f" "$a" "$b" <<'PY' || { echo "t33_binoracle bcshct: mutation text not found: $a"; exit 1; }
import sys
p, a, b = sys.argv[1:4]; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/bcshct.c" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle bcshct: mutation does not build: $a"; exit 1; }
	if $PY "$KO" "$W/m.o" --n 600 > /dev/null; then
		echo "t33_binoracle bcshct: mutation NOT detected: $a => $b"; NF=$((NF + 1))
	else NM=$((NM + 1)); fi
done <<'M'
tx_isp_t33_bcsh.h@		if ((u32)seg == t33_le32(st + 0x24) && !(seg & 1))@		if ((u32)seg == t33_le32(st + 0x24) && (seg & 1))
tx_isp_t33_bcsh.h@		if ((d < 0 ? -d : d) <= (int)t32_tp_le16(sec + 0xac))@		if ((d < 0 ? -d : d) < (int)t32_tp_le16(sec + 0xac))
tx_isp_t33_bcsh.h@		d = ct - (int)t33_le32(st + 0x20);@		d = ct - (int)t33_le32(st + 0x24);
tx_isp_t33_bcsh.h@		if (!force)@		if (force)
tx_isp_t33_bcsh.h@	st[0x29] = st[0x29] == 0xff ? 0 : st[0x29] + 1;@	st[0x29] = st[0x29] == 0xfe ? 0 : st[0x29] + 1;
tx_isp_t33_bcsh.h@	st[0x29] = st[0x29] == 0xff ? 0 : st[0x29] + 1;@	st[0x28] = st[0x28] == 0xff ? 0 : st[0x28] + 1;
tx_isp_t33_bcsh.h@		hue = sec[0x4a7] ? sec[0x4ac] : 0x80;@		hue = sec[0x4a7] ? sec[0x4ac] : 0x7f;
tx_isp_t33_bcsh.h@	if (r)@	if (!r)
tx_isp_t33_bcsh.h@	int r = t33_bcsh_ct_interp(st, sec, lut, ct, 0);@	int r = t33_bcsh_ct_interp(st, sec, lut, ct, 1);
M
echo "t33_binoracle bcshct: mutations $NM detected, $NF not detected"
[ $NF = 0 ] || exit 1
echo "t33_binoracle bcshct: PASS"
