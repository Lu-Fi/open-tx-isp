#!/bin/sh
# T33 binary oracle block "gammaev" (openimp #507): tests/t33_binoracle/gammaev.c (driver/t32/tx_isp_t33_gamma.h
# t33_gamma_ev_update) vs the vendor tisp_gamma_ev_update in Unicorn (tools/ko_emu/diff_t33_gammaev.py), then
# one-token mutations of the header that must be detected (lines: header @ from @ to).
# Covers tisp_gamma_ev_update and tisp_gamma_switch_para (the API-curve write of a raw section).
# Needs T33_OEM_KO, T33_CC (else T40_CC) and python3 unicorn + pyelftools, else SKIP (run.sh checks).
H=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
PY="python3 $H/../../tools/ko_emu/diff_t33_gammaev.py"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$H/build_emu.sh" "$CC" "$H/gammaev.c" "$W/ok.o" || { echo "t33_binoracle gammaev: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" --n "${N:-2000}" > "$W/out" || { tail -4 "$W/out"; echo "t33_binoracle gammaev: FAIL"; exit 1; }
tail -1 "$W/out"
NM=0; NF=0
while IFS='@' read -r f a b; do
	[ -n "$f" ] || continue
	rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$W/d/$f" "$a" "$b" <<'PY' || { echo "t33_binoracle gammaev: mutation text not found: $a"; exit 1; }
import sys
p, a, b = sys.argv[1:4]; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/gammaev.c" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle gammaev: mutation does not build: $a"; exit 1; }
	if $PY "$KO" "$W/m.o" --n 600 > /dev/null; then
		echo "t33_binoracle gammaev: mutation NOT detected: $a => $b"; NF=$((NF + 1))
	else NM=$((NM + 1)); fi
done <<'M'
tx_isp_t33_gamma.h@	if (sec[0x42] == 1 || (sec[0x42] == 0 && sec[0x43] != 2)) {@	if (sec[0x42] == 1 || (sec[0x42] == 0 && sec[0x43] != 1)) {
tx_isp_t33_gamma.h@		if (sec[0x44] == 1)@		if (sec[0x44] != 1)
tx_isp_t33_gamma.h@	t33_gamma_lut(ap, l);@	t33_gamma_lut(c, l);
tx_isp_t33_gamma.h@	t33_gamma_interp(sec, ip[0], ip[1], str, 1);@	t33_gamma_interp(sec, ip[1], ip[0], str, 1);
tx_isp_t33_gamma.h@	t33_gamma_curve(sec, tab, ip[1] << 22 | ip[0] >> 10, c);@	t33_gamma_curve(sec, tab, ip[0] << 22 | ip[1] >> 10, c);
tx_isp_t33_gamma.h@	if (hi > 0x100 || (hi == 0x100 && lo)) {@	if (hi > 0x100 || (hi == 0x100 && !lo)) {
tx_isp_t33_gamma.h@	if (sec[0x42] != 1 && (sec[0x42] != 0 || sec[0x43] == 2)) {@	if (sec[0x42] != 1 && (sec[0x42] != 0 || sec[0x43] == 1)) {
tx_isp_t33_gamma.h@		return force != 1;@		return force == 1;
tx_isp_t33_gamma.h@		if (!force && *str == 0xff)@		if (!force && *str == 0xfe)
tx_isp_t33_gamma.h@		if (!force && *str == sec[0x35])@		if (!force && *str == sec[0x36])
tx_isp_t33_gamma.h@				v = sec[0x36];@				v = sec[0x37];
tx_isp_t33_gamma.h@			v = sec[0x41];@			v = sec[0x40];
tx_isp_t33_gamma.h@	if (v == *str && !force)@	if (v == *str || !force)
tx_isp_t33_gamma.h@	t33_gamma_curve(sec, tab, hi << 22 | lo >> 10, c);@	t33_gamma_curve(sec, tab, hi << 22 | lo >> 9, c);
tx_isp_t33_gamma.h@		return r < 0 ? -1 : 1;@		return r < 0 ? -1 : 0;
tx_isp_t33_gamma.h@					w = e - (u32)t33_s32(prev + 0x106);@					w = e - (u32)t33_s32(prev + 0x102);
tx_isp_t33_gamma.h@			if ((int)e <= t33_s32(src + 0x106))@			if ((int)e < t33_s32(src + 0x106))
tx_isp_t33_gamma.h@	x = c[0x80] * s + (0x100 - s) * 0xfff;@	x = c[0x80] * s + (0x100 - s) * 0xffe;
M
echo "t33_binoracle gammaev: mutations $NM detected, $NF not detected"
[ $NF = 0 ] || exit 1
echo "t33_binoracle gammaev: PASS"
