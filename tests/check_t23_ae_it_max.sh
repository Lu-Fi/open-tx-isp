#!/bin/sh
# check_t23_ae_it_max.sh [driver/t23]   (Lu-Fi/openimp#1008)
# T23 AE_IT_MAX with the lifted stock AE0 must stay the stock no-op (1.3.0 tisp_s_ae_it_max -> tisp_ae_min_max_set,
# a 4-instruction stub) and read back tisp_ae_ctrls word 7 (tisp_g_ae_it_max), not the open ladder cap.
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
blk=$(awk 'index($0,"case REGTRACE_TISP_CTRL_AE_IT_MAX:"){on=1} on{print} on&&/return ret;/{exit}' "$f")
echo "$blk" | grep -q 't23_aelift_g_it_max(value)' || { echo "check_t23_ae_it_max: get does not read the lifted AE (t23_aelift_g_it_max)"; fail=1; }
echo "$blk" | awk '/if \(regtrace_t23_source_ae_oem\)/{getline; if ($0 ~ /return 0;/) ok=1} END{exit !ok}' || { echo "check_t23_ae_it_max: set on the lifted AE is not the stock no-op"; fail=1; }
[ $fail -eq 0 ] && echo "check_t23_ae_it_max: OK"
exit $fail
