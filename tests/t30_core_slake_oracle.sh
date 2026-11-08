#!/bin/sh
# t30_core_slake_oracle.sh: ispcore_slake_module + the vin activate/slake and csi slake it calls (fd close -> core/vin/sensor release, #2) of the built open
# tx-isp-t30.ko against the SDK-built vendor ko, both executed in Unicorn (tools/ko_emu/diff_t30_core_slake.py).
# Needs T30_OEM_KO (vendor tx-isp-t30.ko 4ea12e1f) and a built open module (T30_OPEN_KO, default
# driver/t30/tx-isp-t30.ko from build_local.sh SOC=t30); SKIP without them (make check).
H=$(cd "$(dirname "$0")" && pwd); OPEN=${T30_OPEN_KO:-$H/../driver/t30/tx-isp-t30.ko}
[ -r "${T30_OEM_KO:-}" ] && [ -r "$OPEN" ] && python3 -c 'import unicorn, elftools' 2>/dev/null || {
	echo "t30_core_slake_oracle: SKIP (set T30_OEM_KO, build the T30 module, python3 unicorn+pyelftools)"; [ -z "${T40_ORACLE_STRICT:-}" ] || exit 1; exit 0; }
python3 "$H/../tools/ko_emu/diff_t30_core_slake.py" "$T30_OEM_KO" "$OPEN" || { echo "t30_core_slake_oracle: FAIL"; exit 1; }
python3 "$H/../tools/ko_emu/diff_t30_child_slake.py" "$T30_OEM_KO" "$OPEN" || { echo "t30_core_slake_oracle: FAIL (child slakes)"; exit 1; }
echo "t30_core_slake_oracle: ok"
