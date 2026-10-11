#!/bin/sh
# usage: check_isp_exports.sh SOC MODULE [READELF] [IMPORTS]
#
# Fails (exit 1) when MODULE (tx-isp-<soc>.ko or the linked tx-isp-<soc>.o)
# does not export every symbol that a known module imports from it
# (driver/isp_imports.txt, from tools/scan_isp_imports.py).
set -eu
soc=$1
mod=$2
readelf=${3:-readelf}
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
imports=${4:-"$here/../driver/isp_imports.txt"}

[ -f "$mod" ] || { echo "check_isp_exports: $mod missing" >&2; exit 1; }
have=$("$readelf" -W -p __ksymtab_strings "$mod" 2>/dev/null |
	sed -n 's/^ *\[ *[0-9a-f]*\] *\([^ ]*\) *$/\1/p' | sort -u)
missing=$(awk -v soc="$soc" '$1 == soc { print $2 }' "$imports" | sort -u |
	while read -r sym; do
		printf '%s\n' "$have" | grep -qx "$sym" || echo "$sym"
	done)
need=$(awk -v soc="$soc" '$1 == soc' "$imports" | wc -l)
if [ -n "$missing" ]; then
	echo "check_isp_exports: $(basename "$mod") does not export what known modules import:" >&2
	for sym in $missing; do
		awk -v soc="$soc" -v s="$sym" '$1 == soc && $2 == s { print "  " $2 " (" $3 ")" }' "$imports" >&2
	done
	exit 1
fi
echo "check_isp_exports: $(basename "$mod"): $need known imports exported"
