#!/bin/sh
# check_ko_selfloops.sh <objdump> <module.ko>
#
# review-week-1030 C1: lifted functions that lost their jr.hb tail calls compiled to
# "insn; b .-4; <store in the delay slot>" (1000fffe), an endless loop in kernel context
# (tisp_sdns_par_refresh / tisp_dpc_par_refresh).  The driver has no legitimate
# two-instruction self loop, so any "b .-4" or "b ." fails the build, except the functions listed in
# tests/ko_selfloops_baseline.txt (known, not fixed yet; remove a line when its function is fixed).
# Exit 0 = none, 1 = found (listed), 2 = usage/tool error.
[ $# -eq 2 ] && [ -f "$2" ] || { echo "usage: $0 <objdump> <module.ko>" >&2; exit 2; }
out=$("$1" -d "$2" 2>/dev/null) || { echo "check_ko_selfloops: objdump failed" >&2; exit 2; }
base=$(dirname "$0")/ko_selfloops_baseline.txt
hits=$(printf '%s\n' "$out" | awk '/^[0-9a-f]+ <.*>:$/ {fn=$2} $2 == "1000fffe" || $2 == "1000ffff" {print fn, $1, $3, $4}')
known=
if [ -f "$base" ] && [ -n "$hits" ]; then
	known=$(printf '%s\n' "$hits" | grep -F -f "$base" || true)
	hits=$(printf '%s\n' "$hits" | grep -v -F -f "$base" || true)
fi
[ -n "$known" ] && { echo "check_ko_selfloops: $(basename "$2"): known (baseline):"; printf '%s\n' "$known"; }
if [ -n "$hits" ]; then
	echo "check_ko_selfloops: $(basename "$2"): branch-to-self loops:"
	printf '%s\n' "$hits"
	exit 1
fi
echo "check_ko_selfloops: $(basename "$2"): no self loops"
exit 0
