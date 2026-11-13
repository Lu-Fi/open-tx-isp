#!/bin/sh
# MXU2 model: unit tests (always) and the check of Matt Davis' translation against the
# vendor module (needs T32_VENDOR_KO, T32_MXU2_PM, T32_OBJDUMP and T32_RECOVERED_GIT =
# the open-tx-isp checkout holding 2e2e06bc; otherwise that part is skipped, exit 2 with
# T40_ORACLE_STRICT=1).
set -e
H=$(cd "$(dirname "$0")" && pwd)
O=${T32O_OUT:-$H/../out}/mxu2
mkdir -p "$O"
${HOSTCC:-cc} -O1 -Wall -Wextra -Werror -I"$H" "$H/mxu2_unit.c" -o "$O/unit"
"$O/unit"
if [ -z "$T32_VENDOR_KO" ] || [ -z "$T32_MXU2_PM" ] || [ -z "$T32_OBJDUMP" ] || [ -z "$T32_RECOVERED_GIT" ]; then
	echo "t32_mxu2: vendor check: SKIP (set T32_VENDOR_KO, T32_MXU2_PM, T32_OBJDUMP, T32_RECOVERED_GIT)"
	[ "${T40_ORACLE_STRICT:-0}" = 1 ] && exit 2
	exit 0
fi
SP=
for s in $("$T32_OBJDUMP" -t "$T32_VENDOR_KO" | grep -E " F \.text.*(simd|Tisp_lce_soft_simd_v4)" | awk '{print $1":"$6}'); do
	a=${s%%:*}; n=${s##*:}
	SP="$SP $n:fn_$(printf '%016x' 0x$a)_$n.c"
done
python3 -I "$H/mxu2_check.py" "$T32_MXU2_PM" "$T32_VENDOR_KO" "$T32_OBJDUMP" "$T32_RECOVERED_GIT" "$O" $SP
${HOSTCC:-cc} -O1 -w -I"$H" "$O/mxu2_check_gen.c" -o "$O/check"
"$O/check"
