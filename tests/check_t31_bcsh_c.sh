#!/bin/sh
# check_t31_bcsh_c.sh   (openimp #550)
#
# Vendor tx-isp-t31.ko: tiziano_bcsh_update writes the EV-interpolated Cxl / Cxh / Cyl / Cyh into
# tisp_BCSH_au32C[1..4], and tiziano_bcsh_TransitParam copies tisp_BCSH_au32C (20 bytes) at contrast 128 or
# reads [1..4] in its contrast branch.  Ours must keep the same single array (bcsh_C), not separate "now"
# copies (night BCSH curve differed from the vendor while the day matched).
# Exit 0 = ok, 1 = violation.
F=$(dirname "$0")/../driver/t31/tx_isp_tuning.c
fail=0
no() { echo "check_t31_bcsh_c: $*"; fail=1; }
grep -q '&bcsh_C\[1\], &bcsh_C\[2\], &bcsh_C\[3\], &bcsh_C\[4\] };' "$F" || no "tiziano_bcsh_update must write the C curve into bcsh_C[1..4]"
grep -qE 'bcsh_C[xy][lh]_now' "$F" && no "separate bcsh_C*_now copies are back (TransitParam would read stale C values)"
awk '/^static void tiziano_bcsh_TransitParam\(/,/^}/' "$F" | grep -q 'memcpy(bcsh_ai32_C, bcsh_C, sizeof(bcsh_ai32_C));' \
	|| no "TransitParam must copy bcsh_C at contrast 128"
[ $fail = 0 ] && echo "check_t31_bcsh_c: ok"
exit $fail
