#!/bin/sh
# T33 binary oracle block "bcshlist" (openimp #507): tests/t33_binoracle/bcshlist.c (driver/t32/tx_isp_t33_bcsh.h
# t33_bcsh_switch_para / _ev_update / _ct_update) vs the vendor tisp_bcsh_switch_para / _ev_update / _ct_update / _irq_frame_down sequences in Unicorn (tools/ko_emu/diff_t33_bcshlist.py), then
# mutations of the header that must be detected (lines: header @ from @ to, \n = line break).
# Needs T33_OEM_KO, T33_CC (else T40_CC) and python3 unicorn + pyelftools, else SKIP (run.sh checks).
H=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
PY="python3 $H/../../tools/ko_emu/diff_t33_bcshlist.py"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$H/build_emu.sh" "$CC" "$H/bcshlist.c" "$W/ok.o" || { echo "t33_binoracle bcshlist: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" --n "${N:-2000}" > "$W/out" || { tail -4 "$W/out"; echo "t33_binoracle bcshlist: FAIL"; exit 1; }
tail -1 "$W/out"
NM=0; NF=0
while IFS='@' read -r f a b; do
	[ -n "$f" ] || continue
	rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$W/d/$f" "$a" "$b" <<'PY' || { echo "t33_binoracle bcshlist: mutation text not found: $a"; exit 1; }
import sys
p, a, b = sys.argv[1:4]; t = open(p).read(); a = a.replace('\\n', '\n'); b = b.replace('\\n', '\n')
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/bcshlist.c" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle bcshlist: mutation does not build: $a"; exit 1; }
	if $PY "$KO" "$W/m.o" --n 600 > /dev/null; then
		echo "t33_binoracle bcshlist: mutation NOT detected: $a => $b"; NF=$((NF + 1))
	else NM=$((NM + 1)); fi
done <<'M'
tx_isp_t33_bcsh.h@	(void)l;@	t33_bcsh_store(st, sec, l);
tx_isp_t33_bcsh.h@	t33_bcsh_switch_state(sec, lut, st);\n	return 0;@	t33_bcsh_switch_state(sec, lut, st);\n	return 1;
tx_isp_t33_bcsh.h@	t33_bcsh_calc_bcs(st, sec);	/* mode 1 */@	t33_bcsh_calc(st, sec);	/* mode 1 */
tx_isp_t33_bcsh.h@	if (x <= t33_le32(sec + 0x64))@	if (x < t33_le32(sec + 0x64))
tx_isp_t33_bcsh.h@		x = (u32)(d >> 32) ? 0xffffffffU : (u32)d;@		x = (u32)d;
tx_isp_t33_bcsh.h@		x = (int)(u32)(d >> 32) < 1 ? (u32)d : 0xffffffffU;@		x = (int)(u32)(d >> 32) < 0 ? (u32)d : 0xffffffffU;
tx_isp_t33_bcsh.h@		lo = bvl;@		lo = bvh;
tx_isp_t33_bcsh.h@		hi = bvh;@		hi = bvl;
tx_isp_t33_bcsh.h@	t32_bcsh_p32(st, sec[0x4c0] ? 0x10 : 8, lo);\n	t32_bcsh_p32(st, sec[0x4c0] ? 0x14 : 0xc, hi);\n	t33_bcsh_calc_bcs@	t32_bcsh_p32(st, 8, lo);\n	t32_bcsh_p32(st, sec[0x4c0] ? 0x14 : 0xc, hi);\n	t33_bcsh_calc_bcs
tx_isp_t33_bcsh.h@	st[0x28] = st[0x28] == 0xff ? 0 : st[0x28] + 1;@	st[0x28] = st[0x28] == 0xfe ? 0 : st[0x28] + 1;
tx_isp_t33_bcsh.h@	if (hi > 0x100 || (hi == 0x100 && lo)) {@	if (hi > 0x101 || (hi == 0x100 && lo)) {
tx_isp_t33_bcsh.h@		n = (long long)(unsigned long long)lo;@		n = (long long)((unsigned long long)hi << 32 | lo);
tx_isp_t33_bcsh.h@	d = n < o ?@	d = (unsigned long long)n < (unsigned long long)o ?
tx_isp_t33_bcsh.h@	if (r)\n		return 0;\n	t33_bcsh_hue(st, sec);@	if (r)\n		return 0;\n	t33_bcsh_calc(st, sec);
M
echo "t33_binoracle bcshlist: mutations $NM detected, $NF not detected"
[ $NF = 0 ] || exit 1
echo "t33_binoracle bcshlist: PASS"
