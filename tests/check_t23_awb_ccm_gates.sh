#!/bin/sh
# check_t23_awb_ccm_gates.sh [driver/t23]   (Lu-Fi/openimp#1013, #1016)
# Stock pike T23 1.3.0 tx-isp-t23.ko 7695c89c:
#  - tiziano_awb_set_hardware_param @0x2ff84 takes 0xb028..0xb034 from the IQ AWB parameters / low-light window
#    (whole range only with _light_src[76] != 0); JZ_Isp_Awb @0x2f6bc switches ModeFlag by EV > _awb_mode[2] << 10.
#  - tisp_ccm_ev_update @0x331c8 gates the CCM on |(ev >> 10) - last| > 0x28, tisp_ccm_ct_update @0x3323c on CT > 100.
d=${1:-$(dirname "$0")/../driver/t23}
c=$d/tx_isp_t23_core.c; a=$d/tx_isp_t23_awb_runtime.inc; g=$d/tx_isp_t23_ae_oem_glue.inc; r=$d/tx_isp_t23_ae_runtime.inc
for f in "$c" "$a" "$g" "$r"; do [ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }; done
fail=0
blk=$(awk '/^static void regtrace_t23_source_awb_write_stats_startup\(void\)/{on=1} on{print} on&&/^}/{exit}' "$c")
echo "$blk" | grep -q 't23x_awb_set_hardware_param();' || { echo "check_t23_awb_ccm_gates: stats startup does not use the stock threshold writer"; fail=1; }
echo "$blk" | grep -q '0x0fff0001U' && { echo "check_t23_awb_ccm_gates: stats startup still writes the fixed whole-range 0xb028/0xb02c"; fail=1; }
grep -q '#define T23X_AWB_HW_TH_OFFSET  (0x1028U + 136U)' "$a" || { echo "check_t23_awb_ccm_gates: IQ threshold offset (par[34], 0x1028 + 136) changed"; fail=1; }
grep -q '#define T23X_AWB_LL_EV_OFFSET  (0x1028U + 232U + 8U)' "$a" || { echo "check_t23_awb_ccm_gates: low-light EV offset (_awb_mode[2]) changed"; fail=1; }
grep -q 't23x_awb_write(0xb02cU, 0x03ff0001U);' "$a" || { echo "check_t23_awb_ccm_gates: low-light 0xb02c = 0x03ff0001 missing"; fail=1; }
grep -q 't23x_awb_lowlight_switch(regtrace_t23_ae_ev_q10());' "$a" || { echo "check_t23_awb_ccm_gates: AWB run without the ModeFlag switch"; fail=1; }
grep -q '#define REGTRACE_T23_CCM_EV_GATE 0x28U' "$c" || { echo "check_t23_awb_ccm_gates: CCM EV gate is not 0x28"; fail=1; }
grep -q '#define REGTRACE_T23_CCM_CT_GATE 100U' "$c" || { echo "check_t23_awb_ccm_gates: CCM CT gate is not 100"; fail=1; }
grep -q 'regtrace_t23_ccm_saturation(.*) !=' "$g" "$r" && { echo "check_t23_awb_ccm_gates: CCM EV path still gated on a saturation change"; fail=1; }
grep -q 'regtrace_t23_source_ccm_ev_update(' "$g" || { echo "check_t23_awb_ccm_gates: AE event 6 does not call the stock CCM EV gate"; fail=1; }
[ $fail -eq 0 ] && echo "check_t23_awb_ccm_gates: OK"
exit $fail
