#!/bin/sh
# check_t40_awb_ct.sh (openimp #576): IMP_ISP_Tuning_GetAwbAttr (0x8000010 -> tisp_g_awb_attr -> tisp_awb_get_attr) must return
# the AWB context's live CT like the vendor (libt40-firmware 0x44be8: param[4] = *(u32 *)pstMainAwbOri[7]) while the translated
# vendor AWB chain owns pstMainAwbOri (t40_vawb_live); the old snapshot returned the last SetAwbAttr ct (0 before any set).
F=$(dirname "$0")/../driver/t40/tx_isp_t40_recovered.c
T=$(mktemp)
awk '/^static void regtrace_t40_awb_attr_snapshot\(/{f=1} f{print} f&&/^}/{exit}' "$F" > "$T"
fail=0
grep -q 'READ_ONCE(t40_vawb_live)' "$T" && grep -q 'pstMainAwbOri\[7\]' "$T" &&
	grep -q 'attr->ct = READ_ONCE(\*(const uint32_t \*)(uintptr_t)ctp);' "$T" ||
	{ echo "check_t40_awb_ct: the AWB attr snapshot must return *pstMainAwbOri[7] (vendor live CT) when t40_vawb_live"; fail=1; }
rm -f "$T"
[ $fail = 0 ] && echo "check_t40_awb_ct: ok"
exit $fail
