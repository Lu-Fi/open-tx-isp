#!/bin/sh
# check_t31_bcsh_c.sh   (openimp #550)
#
# Vendor tx-isp-t31.ko: tiziano_bcsh_update writes the EV-interpolated Cxl / Cxh / Cyl / Cyh into
# tisp_BCSH_au32C[1..4], and tiziano_bcsh_TransitParam copies tisp_BCSH_au32C (20 bytes) at contrast 128 or
# reads [1..4] in its contrast branch.  Ours must keep the same single array (bcsh_C), not separate "now"
# copies (night BCSH curve differed from the vendor while the day matched).
# Also: the LUT gets the constant clip tisp_BCSH_au32clip, Cslope2 uses clip1[1], and the IQ bin clip0 words are
# the HLSP control bytes (vendor tiziano_bcsh_lut_parameter / TransitParam).
# Exit 0 = ok, 1 = violation.
F=$(dirname "$0")/../driver/t31/tx_isp_tuning.c
fail=0
no() { echo "check_t31_bcsh_c: $*"; fail=1; }
grep -q '&bcsh_C\[1\], &bcsh_C\[2\], &bcsh_C\[3\], &bcsh_C\[4\] };' "$F" || no "tiziano_bcsh_update must write the C curve into bcsh_C[1..4]"
grep -qE 'bcsh_C[xy][lh]_now' "$F" && no "separate bcsh_C*_now copies are back (TransitParam would read stale C values)"
awk '/^static void tiziano_bcsh_TransitParam\(/,/^}/' "$F" | grep -q 'memcpy(bcsh_ai32_C, bcsh_C, sizeof(bcsh_ai32_C));' \
	|| no "TransitParam must copy bcsh_C at contrast 128"
grep -q '^static const uint32_t bcsh_clip\[4\] = { 0x0000, 0x03ff, 0x0000, 0x03ff };' "$F" || no "the constant clip (vendor tisp_BCSH_au32clip) is missing"
[ "$(grep -c 'tiziano_bcsh_lut_parameter((void \*)bcsh_clip, bcsh_clip1, bcsh_clip2, bcsh_Offset0)' "$F")" = 2 ] || no "the LUT must get the constant clip, not the IQ bin clip0"
grep -q 'bcsh_Cslope2 = (diff << 10) / (bcsh_clip1\[1\] - bcsh_ai32_C\[2\]);' "$F" || no "Cslope2 must use clip1[1] like the vendor"
grep -q 'bcsh_HLSP_now, &bcsh_HLSPslope, bcsh_ev_current, bcsh_clip0, bcsh_EvList_now, bcsh_HLSP_now);' "$F" || no "HLSP control bytes must come from clip0 (t31_bcsh_lut control argument)"
[ $fail = 0 ] && echo "check_t31_bcsh_c: ok"
exit $fail
