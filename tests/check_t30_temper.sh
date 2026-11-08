#!/bin/sh
# check_t30_temper.sh [driver/t30]  (Lu-Fi/openimp#202)
# Host test of the Temper attribute system-table items with a mutation check; the stab offsets
# against the SDK system_tab (T30_SDK_DIR, else that part is skipped); the driver arms present.
d=${1:-$(dirname "$0")/../driver/t30}; t=$(cd "$(dirname "$0")" && pwd)
h=$d/tx_isp_t30_temper.h; src=$t/t30_temper_host_test.c
[ -f "$h" ] || { echo "usage: $0 [driver/t30]" >&2; exit 2; }
h=$(cd "$(dirname "$h")" && pwd)/$(basename "$h")
CC=${CC:-cc}; W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
bld() { $CC -std=c99 -Wall -Wextra -Werror -DTEMPER_HDR="\"$1\"" -o "$W/t" "$src"; }
bld "$h" && "$W/t" > "$W/out" 2>&1 || { echo "check_t30_temper: FAIL"; cat "$W/out"; exit 1; }
cat "$W/out"
fail=0; n=0
mut() {
	n=$((n + 1))
	sed "$2" "$h" > "$W/m.h"
	cmp -s "$h" "$W/m.h" && { echo "check_t30_temper: mutation $1 did not apply"; fail=1; return; }
	bld "$W/m.h" || { echo "check_t30_temper: mutation $1 does not compile"; fail=1; return; }
	if "$W/t" > "$W/out" 2>&1; then echo "check_t30_temper: mutation $1 NOT detected"; fail=1; else echo "check_t30_temper: mutation $1 detected"; fi
}
mut auto-flag 's/st\[T30_STAB_MANUAL_TEMPER\] = 0;/st[T30_STAB_MANUAL_TEMPER] = 1;/'
mut auto-min 's/st\[T30_STAB_TEMPER_MIN\] = (uint8_t)temper_min;/st[T30_STAB_TEMPER_MIN] = (uint8_t)(temper_min >> 1);/'
mut manual-nowiden 's/if (strength > st\[T30_STAB_TEMPER_MAX\])/if (0)/'
mut target-off 's/#define T30_STAB_TEMPER_TARGET	46/#define T30_STAB_TEMPER_TARGET	45/'
mut get-bypass 's/if (reg40 \& 0x400)/if (reg40 \& 0x200)/'
# system_tab offsets vs the SDK header (optional)
SDK=${T30_SDK_DIR:-$t/../external/ingenic-sdk/common/isp/t30}
if [ -r "$SDK/include/apical-isp/apical.h" ]; then
	sed -n '/typedef struct _sytem_tab{/,/} system_tab;/p' "$SDK/include/apical-isp/apical.h" > "$W/st.h"
	printf '#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n#include "st.h"\nint main(void){printf("%%d %%d %%d %%d\\n",(int)offsetof(system_tab,global_manual_temper),(int)offsetof(system_tab,global_temper_threshold_target),(int)offsetof(system_tab,global_maximum_temper_strength),(int)offsetof(system_tab,global_minimum_temper_strength));return 0;}\n' > "$W/st.c"
	o=$($CC -I"$W" -o "$W/st" "$W/st.c" && "$W/st")
	[ "$o" = "11 46 47 48" ] && echo "check_t30_temper: SDK system_tab offsets $o ok" || { echo "check_t30_temper: SDK offsets '$o' != 11 46 47 48"; fail=1; }
else echo "check_t30_temper: SDK offset part SKIP (no T30 SDK header)"; fi
f=$d/tx_isp_t30_recovered.c
for c in 't30_temper_s_attr_stab(stab, temper_attr.mode,' 't30_temper_g_attr(stab, APICAL_READ_32(0x0040), &tmode, &tstr);' 'default_control.control.id == 0x08000084'; do
	grep -qF -- "$c" "$f" || { echo "check_t30_temper: missing in driver: $c"; fail=1; }
done
[ $fail = 0 ] && echo "check_t30_temper: ok ($n/$n mutations detected, driver arms present)"
exit $fail
