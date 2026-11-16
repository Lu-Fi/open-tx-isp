#!/bin/sh
# T33 binary oracle block "mdnspr" (openimp #99): tests/t33_binoracle/mdnspr.c (tisp_mdns_init / _refresh /
# _par_refresh / _switch_para chain in our form) vs the vendor tx-isp-t33.ko in Unicorn
# (tools/ko_emu/diff_t33_mdnspr.py), then one-token mutations of the headers that must be detected
# (lines: header @ from @ to).  Inputs: MDNS sections of the IQ bins in T33_IQ_BINS (comma list, e.g. the cv2005
# PRJ008 day + night bins) and gain traces of QEMU vendor runs in T33_GAIN_LOGS (qemu-guest.log, comma list);
# without them random sections / gain walks only (said in the output).
# Needs T33_OEM_KO, T33_CC (else T40_CC) and python3 unicorn + pyelftools, else SKIP (run.sh checks).
H=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
B="random sections only"; [ -z "${T33_IQ_BINS:-}" ] || B="$(echo "$T33_IQ_BINS" | tr , "\n" | wc -l) bins"
G="random walks only"; [ -z "${T33_GAIN_LOGS:-}" ] || G="$(echo "$T33_GAIN_LOGS" | tr , "\n" | wc -l) traces + random walks"
echo "t33_binoracle mdnspr: inputs: MDNS sections $B, gains $G"
PY="python3 $H/../../tools/ko_emu/diff_t33_mdnspr.py"
IN=""; [ -z "${T33_IQ_BINS:-}" ] || IN="--bins $T33_IQ_BINS"; [ -z "${T33_GAIN_LOGS:-}" ] || IN="$IN --gains $T33_GAIN_LOGS"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$H/build_emu.sh" "$CC" "$H/mdnspr.c" "$W/ok.o" || { echo "t33_binoracle mdnspr: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" --n "${N:-200}" $IN > "$W/out" || { tail -4 "$W/out"; echo "t33_binoracle mdnspr: FAIL"; exit 1; }
tail -1 "$W/out"
NM=0; NF=0
while IFS='@' read -r f a b; do
	[ -n "$f" ] || continue
	rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$W/d/$f" "$a" "$b" <<'PY' || { echo "t33_binoracle mdnspr: mutation text not found: $a"; exit 1; }
import sys
p, a, b = sys.argv[1:4]; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/mdnspr.c" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle mdnspr: mutation does not build: $a"; exit 1; }
	if $PY "$KO" "$W/m.o" --n "${NMUT:-60}" $IN > /dev/null; then
		echo "t33_binoracle mdnspr: mutation NOT detected: $a => $b"; NF=$((NF + 1))
	else NM=$((NM + 1)); fi
done <<'M'
tx_isp_t33_swmdns.h@: last - gain) < thr)@: last - gain) <= thr)
tx_isp_t33_swmdns.h@	if (last == 0xffffffffU) {@	if (last == 0xfffffffeU) {
tx_isp_t33_swmdns.h@? 2 : 0xd018, 0);@? 2 : 0xd018, 1);
tx_isp_t33_swmdns.h@? 1 : 0xd014, 1);@? 1 : 0xd014, 0);
tx_isp_t33_swmdns.h@	*refresh = 1;@	*refresh = 2;
tx_isp_t33_swmdns.h@		inf[2181] = gain;	/* moved@		inf[2181] = gain + 1;	/* moved
tx_isp_t33_swmdns.h@		inf[2181] = gain;@		inf[2181] = gain - 1;
tx_isp_t33_swmdns.h@{ 3548, 9192, 59, 8, 11, 4 },@{ 3548, 9192, 58, 8, 11, 4 },
tx_isp_t33_swmdns.h@	f = gain & 0xffff;@	f = gain & 0x7fff;
tx_isp_t33_swmdns.h@	if (uvflag)@	if (!uvflag)
tx_isp_t33_mdns.h@{ 8, 0x986, 0xe3 }@{ 8, 0x987, 0xe3 }
tx_isp_t33_mdns.h@  t33_mdns_sort(v + 0x60a,4);@  t33_mdns_sort(v + 0x60a,3);
tx_isp_t33_mdnsP.h@	tisp_mdns_par_refresh(chn, gain, v[2182]);@	tisp_mdns_par_refresh(chn, gain, v[2183]);
tx_isp_t33_mdnsP.h@r < 1200 ? 0xfcfc0002@r < 1201 ? 0xfcfc0002
tx_isp_t33_mdnsR.h@	bw[8724 / 4] = 0xffffffffU;@	bw[8724 / 4] = 0;
tx_isp_t32_tp.h@	v = (d >> 16) + ((d >> 15) & 1);@	v = (d >> 16);
M
echo "t33_binoracle mdnspr: mutations $NM detected, $NF not detected"
[ $NF = 0 ] || exit 1
echo "t33_binoracle mdnspr: PASS"
