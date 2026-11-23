#!/bin/sh
# check_t23_qc3_1123.sh [driver/t23]   (Lu-Fi/openimp#834, #811, #816)
# #834: the AE lift shares the driver's IspAeStatic (one 16.2 KiB copy like stock), and
#       tisp_ae_get_y_zone copies IspAeStatic + 13872 (stock 0x4a300), not offset 0.
# #811: a live MSCA flip re-sends the shown OSD pictures/masks, and the OSD commit
#       waits while the 0xd010 update request is pending.
# #816: per-channel completion counters (chan_done_cnt) exist.
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
bad(){ echo "check_t23_qc3_1123: $*"; fail=1; }
grep -q '^#define oem_IspAeStatic IspAeStatic$' "$d/tx_isp_t23_ae_oem.inc" || bad "AE lift has its own oem_IspAeStatic copy (#834)"
grep -q 'static uint8_t oem_IspAeStatic\[' "$d/tx_isp_t23_ae_oem.inc" && bad "oem_IspAeStatic array still defined (#834)"
grep -q '#define oem_IspAeStatic IspAeStatic' "$d/audit/dolift_ae.py" || bad "dolift_ae.py does not emit the IspAeStatic alias (#834)"
grep -q '(uintptr_t)&IspAeStatic + 13872U), 900)' "$f" || bad "tisp_ae_get_y_zone not at IspAeStatic + 13872 (#834)"
awk '/^static void regtrace_t23_isp_flip_apply\(/{p=1} p&&/^}/{exit} p' "$f" | grep -q 't23_osd_restart();' || bad "flip apply does not re-send the OSD (#811)"
awk '/^static void t23_osd_commit\(/{p=1} p&&/^}/{exit} p' "$f" | grep -q 'system_reg_read(0xd010U) & 1U' || bad "OSD commit ignores a pending 0xd010 request (#811)"
grep -q 'module_param_array_named(chan_done_cnt, t23_chan_cnt' "$f" || bad "chan_done_cnt counters missing (#816)"
[ $fail -eq 0 ] && echo "check_t23_qc3_1123: OK"
exit $fail
