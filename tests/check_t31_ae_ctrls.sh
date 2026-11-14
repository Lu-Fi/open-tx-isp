#!/bin/sh
# check_t31_ae_ctrls.sh   (openimp #551)
#
# Vendor tx-isp-t31.ko: max analog gain and max ISP digital gain are fields of the AE control object
# (tisp_ae_ctrls[5] / [8], the BN names data_c46b0 / data_c46bc), set from the IQ bin ae_exp_th by
# tiziano_ae_init_exp_th and read by the AE solver and tisp_g_ev_attr (isp-m0 "MAX SENSOR analog gain",
# "MAX ISP digital gain").  Separate copies with fixed defaults made the AE run with 85x / 1x instead of the bin limits.
# Exit 0 = ok, 1 = violation.
F=$(dirname "$0")/../driver/t31/tx_isp_tuning.c
fail=0
no() { echo "check_t31_ae_ctrls: $*"; fail=1; }
grep -qE '^static uint32_t data_c46(b0|bc)\b' "$F" && no "separate data_c46b0 / data_c46bc copies are back"
grep -q 'ev_buffer\[8\] = tisp_log2_fixed_to_fixed(tisp_ae_ctrls\[5\] + 4, 10, 5);' "$F" || no "tisp_g_ev_attr word 8 must read tisp_ae_ctrls[5]"
grep -q 'ev_buffer\[9\] = tisp_log2_fixed_to_fixed(tisp_ae_ctrls\[8\] + 4, 10, 5);' "$F" || no "tisp_g_ev_attr word 9 must read tisp_ae_ctrls[8]"
grep -q 'tisp_ae_ctrls\[5\] = ae_exp_th.data\[1\];' "$F" && grep -q 'tisp_ae_ctrls\[8\] = ae_exp_th.data\[3\];' "$F" || no "AE init must fill ctrls[5] / [8] from ae_exp_th"
[ $fail = 0 ] && echo "check_t31_ae_ctrls: ok"
exit $fail
