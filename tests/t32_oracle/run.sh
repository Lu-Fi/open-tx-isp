#!/bin/sh
# T32 host oracle runner (see audit/t32pr/HOWTO_ORACLE.md in the Lu-Fi tree
# and tests/README.oracles.md).  Builds the reference functions of ref/
# (Matt Davis' recovered C, PR #15 @ 2e2e06bc) once, links them with env.c
# and the oracle main, runs it, then rebuilds the oracle against one-token
# mutations of our driver header: every mutation must be detected.
#   usage: run.sh <oracle>      e.g. run.sh t32_ccm_oracle
set -e
H=$(cd "$(dirname "$0")" && pwd)
D=$H/../../driver/t32
O=${T32O_OUT:-$H/out}
CC=${HOSTCC:-cc}
M=$O/mut
link() {	# $1 = header dir in front of driver/t32, $2 = output
	$CC -O1 -fno-pie -Wall -Wextra -Werror -Wno-unused-function -I"$1" -I"$D" -I"$H" \
		"$H/$NAME.c" "$O/env.o" $objs -no-pie -o "$2"
}
one() {	# $1 = index, $2 = sed expression; writes $M/$1.msg on failure
	d=$M/$1; mkdir -p "$d"
	sed "$2" "$D/$HDR" > "$d/$HDR"
	if cmp -s "$d/$HDR" "$D/$HDR"; then echo "$NAME: mutation did not apply: $2" > "$M/$1.msg"; return; fi
	if ! link "$d" "$d/$NAME.mut" 2>/dev/null; then echo "$NAME: mutation does not build: $2" > "$M/$1.msg"; return; fi
	# exit 0 = not detected; a non-zero exit or the time limit (rc 124: it no longer ends, the original does) = detected
	if T32_IQ_DIR="${T32_IQ_DIR:-}" T40_ORACLE_STRICT=0 timeout "$T" "$d/$NAME.mut" >/dev/null 2>&1; then
		echo "$NAME: mutation NOT detected: $2" > "$M/$1.msg"
	fi
}
if [ "$1" = --mut ]; then	# worker of the mutation check below: run.sh --mut <oracle> <index>, objs / T from the environment
	NAME=$2
	. "$H/$NAME.conf"
	one "$3" "$(echo "$MUTATIONS" | grep . | sed -n "$3p")"
	exit 0
fi
NAME=$1
. "$H/$NAME.conf"		# REFS="fn_..c ...", HDR=tx_isp_t32_xxx.h, MUTATIONS (one sed expression per line)
mkdir -p "$O/ref"
RF="-O1 -fno-pie -fwrapv -fno-strict-aliasing -w"
objs=
for f in $REFS; do
	o=$O/ref/${f%.c}.o
	src=$H/ref/$f
	case " $REFFIX " in	# REFFIX="file=script ..." : a build-time fix of one reference copy
	*" $f="*)
		fix=${REFFIX#*"$f="}; fix=${fix%% *}
		mkdir -p "$O/refp"
		sh "$H/$fix" < "$H/ref/$f" > "$O/refp/$f"
		src=$O/refp/$f
		rm -f "$o"
		;;
	esac
	if [ ! -f "$o" ] || [ "$H/ref/$f" -nt "$o" ]; then
		$CC $RF -I"$H/ref" -c "$src" -o "$o"
		objcopy --redefine-sym memcpy=t32o_memcpy --redefine-sym __ashrdi3=t32o___ashrdi3 --redefine-sym __ashldi3=t32o___ashldi3 --redefine-sym __lshrdi3=t32o___lshrdi3 "$o"
	fi
	objs="$objs $o"
done
$CC -O1 -fno-pie -Wall -Wextra -Werror -c "$H/env.c" -o "$O/env.o"
link "$D" "$O/$NAME"
t0=$(date +%s)
"$O/$NAME"
rc=$?
T=$(( ($(date +%s) - t0) * 2 + 20 ))	# a mutant that needs more than twice the time of the original is cut off
[ $rc -eq 0 ] || exit $rc
# mutation check: the mutants are independent, T32O_JOBS (default: all CPUs) of them build and run at a time
rm -rf "$M"; mkdir -p "$M"
J=${T32O_JOBS:-$(nproc 2>/dev/null || echo 4)}
nm=$(echo "$MUTATIONS" | grep -c .)
export objs T
seq 1 "$nm" | xargs -P "$J" -I{} sh "$H/run.sh" --mut "$NAME" {}
if ls "$M"/*.msg >/dev/null 2>&1; then cat "$M"/*.msg | head -n 1; exit 1; fi
echo "$NAME: PASS, $nm/$nm mutations of $HDR detected"
