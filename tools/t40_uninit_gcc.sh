#!/bin/sh
# t40_uninit_gcc.sh: compile driver/t40/tx_isp_t40_recovered.c once more with the real kernel
# command line (from the .o.cmd of a previous ./build_local.sh SOC=t40), but with -w replaced by
# -Wuninitialized -Wmaybe-uninitialized and -O2, output to /dev/null.  First filter for lifted
# code that stores through / reads never-assigned locals (issue #139).
#   usage: KDIR=<kernel build dir> tools/t40_uninit_gcc.sh [src.c] > warnings.log
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:-$HERE/driver/t40/tx_isp_t40_recovered.c}"
OBJ="${SRC%.c}.o"
CMDF="$(dirname "$OBJ")/.$(basename "$OBJ").cmd"
[ -f "$CMDF" ] || { echo "no $CMDF: build first (ROOT=... SOC=t40 ./build_local.sh)" >&2; exit 1; }
[ -n "$KDIR" ] || { echo "set KDIR" >&2; exit 1; }
CMD="$(head -1 "$CMDF" | sed 's/^cmd_[^=]*:= //')"
CMD="$(printf '%s' "$CMD" | sed -e 's/ -w / -Wuninitialized -Wmaybe-uninitialized /' \
	-e 's/ -Os / -O2 /g' -e 's/ -Wp,-MD,[^ ]* / /' -e "s# -o [^ ]*\.o # -o /dev/null #")"
cd "$KDIR"
PATH="$(dirname "$(ls "$KDIR"/../../host/bin/mipsel-linux-gcc 2>/dev/null || command -v mipsel-linux-gcc)"):$PATH" \
	nice -n 19 sh -c "$CMD" 2>&1 | grep -E "warning: .*(uninitialized|maybe-uninitialized)|In function|inlined from|declared here" || true
