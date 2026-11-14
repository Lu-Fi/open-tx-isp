#!/bin/sh
# T33 binary oracle block "lceev" (openimp #500): tests/t33_binoracle/lceev.c (driver/t32/tx_isp_t33_lceev.h) vs the
# vendor tisp_lce_init / tisp_lce_ev_update / tisp_lce_switch_para / func_lce_para_update / tisp_lce_bin_para_update /
# tisp_lce_gausswgt_calc
# in Unicorn (tools/ko_emu/diff_t33_lceev.py), then one-token mutations of the headers that must be detected
# (lines: oracle part @ header @ from @ to).
# Needs T33_OEM_KO, T33_CC (else T40_CC) and python3 unicorn + pyelftools, else SKIP (run.sh checks).
H=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
PY="python3 $H/../../tools/ko_emu/diff_t33_lceev.py"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$H/build_emu.sh" "$CC" "$H/lceev.c" "$W/ok.o" || { echo "t33_binoracle lceev: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" --n "${N:-40}" --evs "${EVS:-4}" --poke "${POKE:-300}" || { echo "t33_binoracle lceev: FAIL"; exit 1; }
NM=0; NF=0
while IFS='@' read -r fn f a b; do
	[ -n "$fn" ] || continue
	rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$W/d/$f" "$a" "$b" <<'PY' || { echo "t33_binoracle lceev: mutation text not found: $a"; exit 1; }
import sys
p, a, b = sys.argv[1:4]; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/lceev.c" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle lceev: mutation does not build: $a"; exit 1; }
	if $PY "$KO" "$W/m.o" --fn "$fn" --n 6 --evs 2 --poke 30 > /dev/null; then
		echo "t33_binoracle lceev: mutation NOT detected: $a => $b"; NF=$((NF + 1))
	else NM=$((NM + 1)); fi
done <<'M'
poke@tx_isp_t33_lceev.h@	T33_LIW(c, 0x52c) = T33_LIW(c, 0x04) != 4;@	T33_LIW(c, 0x52c) = T33_LIW(c, 0x04) != 3;
leaf@tx_isp_t33_lceev.h@{ 0x5d0, 0x4a0 }@{ 0x5d0, 0x49c }
leaf@tx_isp_t33_lceev.h@{ 0x5d4, 0x4a4, 21 }@{ 0x5d4, 0x4a4, 20 }
leaf@tx_isp_t33_lceev.h@	t33_lce_gauss_lpf(T33_LIW(c, 0x48) & 0xff, lpf);@	t33_lce_gauss_lpf(T33_LIW(c, 0x48) & 0x7f, lpf);
leaf@tx_isp_t33_lceev.h@{ { 0x164, 0x7cc, 0x1a3 }, { 0x17c, 0x68c, 0x117 } }@{ { 0x164, 0x7cc, 0x1a3 }, { 0x17c, 0x68c, 0x118 } }
leaf@tx_isp_t33_lceev.h@			T33_LIW(c, ws[k][1] + 0x80 + 4 * i) = dn[i];@			T33_LIW(c, ws[k][1] + 0x7c + 4 * i) = dn[i];
poke@tx_isp_t33_lceev.h@		T33_SW(l16, 6 + 2 * k, T33_LIW(c, 0x548 + 0xc * k) & 0x1fffU);@		T33_SW(l16, 6 + 2 * k, T33_LIW(c, 0x548 + 0xc * k) & 0xfffU);
poke@tx_isp_t33_lceev.h@(T33_LIW(c, 0x62ec) & 3U));@(T33_LIW(c, 0x62ec) & 1U));
poke@tx_isp_t33_lceev.h@(T33_LIW(c, 0x648) & 1U) << 20);@(T33_LIW(c, 0x644) & 1U) << 20);
poke@tx_isp_t33_lceev.h@	T33_SW(l37, 0x12, t33_lceev_p4(c, 0x674, 0x678, 0x67c, 0x670));@	T33_SW(l37, 0x12, T33_LIP4(c, 0x670));
poke@tx_isp_t33_lceev.h@T33_LIW(c, 0x628) << 24@T33_LIW(c, 0x628) << 23
poke@tx_isp_t33_lceev.h@		T33_SW(l37, 0x3c + k, T33_LIP4(c, 0x84c + 0x10 * k));@		T33_SW(l37, 0x3b + k, T33_LIP4(c, 0x84c + 0x10 * k));
poke@tx_isp_t33_lceev.h@	if (s[0x2fb] == 1 && s[0xc8] == 1) {@	if (s[0x2fb] == 1 || s[0xc8] == 1) {
poke@tx_isp_t33_lceev.h@		mode[0] = 0;@		mode[0] = 2;
poke@tx_isp_t33_lceev.h@	c->ev = hi << 22 | lo >> 10;@	c->ev = hi << 21 | lo >> 10;
poke@tx_isp_t33_lceev.h@	T33_SW(l16, T33_LCE_SLOT_EN, 1);@	T33_SW(l16, T33_LCE_SLOT_EN + 1, 1);
poke@tx_isp_t33_lceev.h@		T33_SW(l37, 0x1b + k, T33_LIP2(c, 0x56c + 8 * k, 0x568 + 8 * k, 0xfffU));@		T33_SW(l37, 0x1b + k, T33_LIP2(c, 0x56c + 8 * k, 0x568 + 8 * k, 0x7ffU));
chain@tx_isp_t33_lcepf.h@	((u32 *)(void *)c)[0x62e4 / 4] = 3;@	((u32 *)(void *)c)[0x62e4 / 4] = 2;
chain@tx_isp_t33_lcepf.h@	T33_LCEPF_PARA_UPDATE(c, s);@	do { } while (0);
chain,switch@tx_isp_t33_lceev.h@	s[5] = 3;@	s[5] = 2;
chain,switch@tx_isp_t33_lceev.h@	if (*dn != s[0xc8]) {@	if (*dn != s[0xc9]) {
chain,switch@tx_isp_t33_lceev.h@		t33_lcepf_write_curve(c, half2);@		(void)half2;
chain,switch@tx_isp_t33_lceev.h@	t33_lce_size(s, c->h, c->w);@	t33_lce_size(s, c->w, c->h);
M
echo "t33_binoracle lceev: mutations $NM detected, $NF not detected"
[ $NF = 0 ] || exit 1
echo "t33_binoracle lceev: PASS"
