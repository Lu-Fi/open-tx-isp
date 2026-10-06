#!/bin/sh
# check_ko_symbols.sh <Module.symvers> <module.ko> [provider.ko ...]
#
# Load check without a camera: every undefined symbol of <module.ko> must be
# exported by the target kernel (Module.symvers of its build tree) or by one
# of the provider modules loaded before it (their __ksymtab_strings).  Anything
# else would make insmod fail with "Unknown symbol ... (err 0)" (the MODPOST
# "undefined!" warnings are only warnings and are easy to miss).
#
# Tools: CROSS_COMPILE prefix (e.g. .../mipsel-linux-) or NM/OBJCOPY env vars.
# Exit 0 = all resolved, 1 = unresolved symbols, 2 = usage/tool error.
set -u
LC_ALL=C; export LC_ALL
[ $# -ge 2 ] || { echo "usage: $0 <Module.symvers> <module.ko> [provider.ko ...]" >&2; exit 2; }
SYMVERS=$1; KO=$2; shift 2
NM=${NM:-${CROSS_COMPILE:-}nm}
OBJCOPY=${OBJCOPY:-${CROSS_COMPILE:-}objcopy}
[ -f "$SYMVERS" ] && [ -f "$KO" ] || { echo "missing $SYMVERS or $KO" >&2; exit 2; }
T=$(mktemp -d) || exit 2
trap 'rm -rf "$T"' EXIT

awk '{print $2}' "$SYMVERS" > "$T/exp"
# provider modules (and the module itself, for self-references) export via
# __ksymtab_strings (survives "ld -x" and gc-sections)
for p in "$KO" "$@"; do
	[ -f "$p" ] || continue
	"$OBJCOPY" -O binary --only-section=__ksymtab_strings "$p" "$T/ks" 2>/dev/null &&
		tr '\0' '\n' < "$T/ks" >> "$T/exp"
	"$NM" "$p" 2>/dev/null | awk '$2 ~ /^[A-TV-Z]$/ && $2 != "U" {print $3}' >> "$T/exp"
done
sort -u "$T/exp" > "$T/exp.s"
"$NM" -u "$KO" | awk '{print $2}' | sort -u > "$T/und"
[ -s "$T/und" ] || { echo "nm -u failed or empty for $KO" >&2; exit 2; }
comm -23 "$T/und" "$T/exp.s" > "$T/bad"
n=$(wc -l < "$T/bad")
if [ "$n" -ne 0 ]; then
	echo "FAIL $(basename "$KO"): $n unresolved symbol(s):"
	sed 's/^/  /' "$T/bad"
	exit 1
fi
echo "OK   $(basename "$KO"): $(wc -l < "$T/und") undefined symbols all resolved"
