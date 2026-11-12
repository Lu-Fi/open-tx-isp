#!/bin/sh
# t40_defog_host.sh (#332): the soft step of driver/t40/tx_isp_t40_defog.h (t40_dfg_soft: statistics -> luma, air light
# curve, transmission by luma, motion weighting, output smoothing, value caches) against the OEM instructions of
# Tiziano_defog_soft (Unicorn, tools/ko_emu/diff_defog_soft.py), several frames per case with the state carried over,
# (host side with t40_defog_motion_var=0, OEM-exact) plus one-token mutations of the header that must be detected; with
# T40_TUNING_BIN also the day/night flow (tools/ko_emu/diff_defog_dn.py, air light curve).  Needs T40_OEM_KO and python3 unicorn + pyelftools, else SKIP.
H=$(cd "$(dirname "$0")" && pwd); f=$H/../driver/t40/tx_isp_t40_defog.h
if [ ! -r "${T40_OEM_KO:-}" ] || ! python3 -c "import unicorn, elftools" 2>/dev/null; then echo "t40_defog_host: SKIP (T40_OEM_KO / python3 unicorn+pyelftools missing)"; [ -z "${T40_ORACLE_STRICT:-}" ] || exit 1; exit 0; fi
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
run() {	# $1 = header, $2 = cases
	cc -O1 -w -DDEFOG_H="\"$1\"" -o "$W/h" "$H/t40_defog/harness.c" || return 2
	python3 "$H/../tools/ko_emu/diff_defog_soft.py" "$T40_OEM_KO" "$W/h" --n "$2" --frames 3
}
run "$f" ${N:-200} || { echo "t40_defog_host: FAIL"; exit 1; }
nm=0; fail=0; nmut=0
while IFS='@' read -r a b; do
	[ -n "$a" ] || continue
	nmut=$((nmut + 1))
	python3 - "$a" "$b" "$f" "$W/m.h" <<'PY' || { echo "t40_defog_host: mutation text not found: $a"; fail=1; continue; }
import sys
a, b, src, dst = sys.argv[1:5]; t = open(src).read()
if a not in t: sys.exit(1)
open(dst, "w").write(t.replace(a, b, 1))
PY
	if run "$W/m.h" ${NMUT:-40} > /dev/null; then echo "t40_defog_host: mutation NOT detected: $a => $b"; fail=1; else nm=$((nm + 1)); fi
done <<'MUT'
y = (19 * v[0] + 38 * v[1] + 7 * v[2]) >> 6;@y = (19 * v[0] + 38 * v[1] + 7 * v[2] + 32) >> 6;
int32_t rnd = (rc >> (30 - s->prec)) & 1@int32_t rnd = (rc >> (31 - s->prec)) & 1
x = (x * 257 + ((uint32_t)s->ctl[2] << 16)) >> 16 & 0xff;@x = (x * 256 + ((uint32_t)s->ctl[2] << 16)) >> 16 & 0xff;
v = lv[j] + ((y - kn[j]) * dl + (dk >> 1)) / dk;@v = lv[j] + ((y - kn[j]) * dl) / dk;
if (s->flags[y] & 0x10)@if (s->flags[y] & 0x11)
s->out[3][k] = ((256 - c->motion[k]) * s->out[3][k]) >> 8;@s->out[3][k] = ((255 - c->motion[k]) * s->out[3][k]) >> 8;
int32_t off = s->ctl[6] << 4, lo = s->ctl[7] << 2, hi = s->ctl[8] << 6;@int32_t off = s->ctl[6] << 4, lo = s->ctl[7] << 3, hi = s->ctl[8] << 6;
a = s->curve[v >> 4];@a = s->curve[(v + 8) >> 4];
w = 255;@w = 254;
MUT
# day/night flow (#332): OEM init(day) -> frames -> dn refresh(night) -> same frames vs the header; the OEM serves the
# day air light curve from its per-value cache, t40_defog_air_day=1 must match it, =0 (night curve) must not
if [ -r "${T40_TUNING_BIN:-}" ]; then
	if cc -O1 -w -DDEFOG_H="\"$f\"" -o "$W/dn" "$H/t40_defog/dn_harness.c"; then
		P="$H/../tools/ko_emu/diff_defog_dn.py"
		python3 "$P" "$T40_OEM_KO" "$T40_TUNING_BIN" "$W/dn" --n ${N_DN:-6} --air-day 1 || fail=1
		if python3 "$P" "$T40_OEM_KO" "$T40_TUNING_BIN" "$W/dn" --n 2 --air-day 0 > /dev/null; then
			echo "t40_defog_host: air_day=0 NOT detected by the day/night oracle"; fail=1
		fi
	else
		fail=1
	fi
else
	echo "t40_defog_host: day/night part SKIP (T40_TUNING_BIN missing)"
fi
[ $fail = 0 ] && echo "t40_defog_host: ok ($nm/$nmut mutations detected, day/night flow)"
exit $fail
