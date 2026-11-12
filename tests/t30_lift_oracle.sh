#!/bin/sh
# t30_lift_oracle.sh: the T30 lift defects a -Os build warned about (Lu-Fi/openimp#274: uninitialised
# returns, missing returns, returned stack address, scaled offsets, AWB mesh chain), our
# driver/t30/tx_isp_t30_recovered.o against the SDK-built vendor ko, both executed in Unicorn
# (tools/ko_emu/diff_t30_lift.py).  Needs T30_OEM_KO (vendor tx-isp-t30.ko 4ea12e1f) and the object of a
# T30 build (T30_OPEN_O, default driver/t30/tx_isp_t30_recovered.o from build_local.sh SOC=t30); SKIP without them.
H=$(cd "$(dirname "$0")" && pwd); OPEN=${T30_OPEN_O:-$H/../driver/t30/tx_isp_t30_recovered.o}
[ -r "${T30_OEM_KO:-}" ] && [ -r "$OPEN" ] && python3 -c 'import unicorn, elftools' 2>/dev/null || {
	echo "t30_lift_oracle: SKIP (set T30_OEM_KO, build the T30 module, python3 unicorn+pyelftools)"; [ -z "${T40_ORACLE_STRICT:-}" ] || exit 1; exit 0; }
python3 "$H/../tools/ko_emu/diff_t30_lift.py" "$T30_OEM_KO" "$OPEN" --n "${N:-200}" || { echo "t30_lift_oracle: FAIL"; exit 1; }
echo "t30_lift_oracle: ok"
