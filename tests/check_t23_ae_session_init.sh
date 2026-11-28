#!/bin/sh
# check_t23_ae_session_init.sh [driver/t23]   (Lu-Fi/openimp#1014)
# Stock T23 1.3.0 runs tisp_init -> tiziano_ae_init once per IMP session (ispcore_core_ops_init on, tx-isp-t23.ko
# .text 0x6a7d8 -> tisp_init 0x16ef0): the AE control block and histories start fresh, the exposure stays.
# The lifted AE must not restore its saved state on the first stream start of a new /dev/tx-isp session.
d=${1:-$(dirname "$0")/../driver/t23}
g=$d/tx_isp_t23_ae_oem_glue.inc; c=$d/tx_isp_t23_core.c
[ -f "$g" ] && [ -f "$c" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
grep -q 'resume = !t23_aelift_new_session &&' "$g" || { echo "check_t23_ae_session_init: resume not gated by the new-session flag"; fail=1; }
grep -q 't23_aelift_new_session = false;' "$g" || { echo "check_t23_ae_session_init: new-session flag never cleared"; fail=1; }
blk=$(awk '/^static int regtrace_tx_isp_open\(/{on=1} on{print} on&&/^}/{exit}' "$c")
echo "$blk" | grep -q 't23_aelift_session_open();' || { echo "check_t23_ae_session_init: first /dev/tx-isp open does not mark a new AE session"; fail=1; }
[ $fail -eq 0 ] && echo "check_t23_ae_session_init: OK"
exit $fail
