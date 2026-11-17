#!/bin/sh
# T33 binary oracle block "mipiphy" (openimp #99): tests/t33_binoracle/mipiphy.c (driver/t32/tx_isp_t33_mipi.h) vs the vendor module in
# Unicorn (tools/ko_emu/diff_t33_mipiphy.py), then one-token mutations of the header that must be detected (lines: header @ from @ to).
# Needs T33_OEM_KO, T33_CC (else T40_CC) and python3 unicorn + pyelftools, else SKIP (run.sh checks).
H=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
PY="python3 $H/../../tools/ko_emu/diff_t33_mipiphy.py"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$H/build_emu.sh" "$CC" "$H/mipiphy.c" "$W/ok.o" || { echo "t33_binoracle mipiphy: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" --n "${N:-2000}" > "$W/out" || { tail -6 "$W/out"; echo "t33_binoracle mipiphy: FAIL"; exit 1; }
tail -1 "$W/out"
NM=0; NF=0
while IFS='@' read -r f a b; do
	[ -n "$f" ] || continue
	rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$W/d/$f" "$a" "$b" <<'PY' || { echo "t33_binoracle mipiphy: mutation text not found: $a"; exit 1; }
import sys
p, a, b = sys.argv[1:4]; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/mipiphy.c" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle mipiphy: mutation does not build: $a"; exit 1; }
	if $PY "$KO" "$W/m.o" --n 600 > /dev/null; then
		echo "t33_binoracle mipiphy: mutation NOT detected: $a => $b"; NF=$((NF + 1))
	else NM=$((NM + 1)); fi
done <<'M'
tx_isp_t33_mipi.h@	k->host(0, 0x0c, 0);@	k->host(0, 0x0c, 1);
tx_isp_t33_mipi.h@	k->host(0, 0x10, 0);@	k->host(0, 0x10, 1);
tx_isp_t33_mipi.h@	k->host(0, 0x0c, 1);@	k->host(0, 0x0c, 0);
tx_isp_t33_mipi.h@	k->host(0, 0x10, 1);@	k->host(0, 0x14, 1);
tx_isp_t33_mipi.h@	if (state < 2)@	if (state < 3)
tx_isp_t33_mipi.h@	if (state < 2)@	if ((unsigned int)state < 2)
tx_isp_t33_mipi.h@			if (s->dbus != 2)@			if (s->dbus != 0)
tx_isp_t33_mipi.h@lanes == 4 ? 0xf : 3@lanes >= 3 ? 0xf : 3
tx_isp_t33_mipi.h@			k->host(0, 0x04, lanes - 1);@			k->host(0, 0x04, lanes);
tx_isp_t33_mipi.h@			k->phy(0x160, t);@			k->phy(0x164, t);
tx_isp_t33_mipi.h@			k->phy(0x260, t);@			k->phy(0x264, t);
tx_isp_t33_mipi.h@			if (lanes > 2) {@			if (lanes > 3) {
tx_isp_t33_mipi.h@			k->phy(0x80, 0xdf);@			k->phy(0x80, 0x5f);
tx_isp_t33_mipi.h@			k->phy(0x3e0, t);@			k->phy(0x3e4, t);
tx_isp_t33_mipi.h@		k->phy(0x2e0, t);@		k->phy(0x2e4, t);
tx_isp_t33_mipi.h@		k->phy(0x360, t);@		;
tx_isp_t33_mipi.h@	if (mode != 2)@	if (mode < 2)
tx_isp_t33_mipi.h@		k->host(0, 0x80, 1);@		k->host(1, 0x80, 1);
tx_isp_t33_mipi.h@		t = t33_settle_time(s[0].clk);@		t = t33_settle_time(s[1].clk);
tx_isp_t33_mipi.h@	t = t33_settle_time(s[1].clk);@	t = t33_settle_time(s[0].clk);
tx_isp_t33_mipi.h@	k->log(T33_MIPI_LOG_SETTLE1, t);@	k->log(T33_MIPI_LOG_SETTLE1, t + 1);
tx_isp_t33_mipi.h@	lanes = s[1].lanes;@	lanes = s[0].lanes;
tx_isp_t33_mipi.h@		s += idx;@		s += 0;
tx_isp_t33_mipi.h@			t = t33_settle_time(s->clk);@			t = t33_settle_time(s->clk + 1);
tx_isp_t33_mipi.h@			k->host(1, 0x10, 1);@			k->host(1, 0x10, 0);
tx_isp_t33_mipi.h@		if (s->dbus != 1) {@		if (s->dbus > 1) {
tx_isp_t33_mipi.h@		} else if (s->index == 1) {@		} else if (s->index >= 1) {
M
echo "t33_binoracle mipiphy: mutations $NM detected, $NF not detected"
[ $((NM + NF)) -gt 0 ] || { echo "t33_binoracle mipiphy: FAIL 0 mutations ran (a block with no mutation proves nothing; check MUTATIONS quoting, openimp #99)"; exit 1; }
[ $NF = 0 ] || exit 1
echo "t33_binoracle mipiphy: PASS"
