#!/bin/sh
# check_919_lows.sh [driver]   (Lu-Fi/openimp#919)
# Source guard for the low findings of the 2026-11-26 code check: T23 IVDC probe failure paths release the
# subdev (L1), the IVDC gate does not swallow the interrupt when the clocks are not driver-managed (L2), the
# remove frees the driver-allocated ring (L3), the 0x1c read-modify-write is serialised (L4), the T32/T33
# rmmod lets the frame in flight drain before it frees the DMA buffers (L6).
d=${1:-$(dirname "$0")/../driver}
c=$d/t23/tx_isp_t23_core.c; w=$d/t32/tx_isp_t32_hw.c; k=$d/t32/tx_isp_t32_core.c
[ -f "$c" ] && [ -f "$w" ] || { echo "usage: $0 [driver]" >&2; exit 2; }
fail=0
err() { echo "check_919_lows: $*"; fail=1; }
p=$(awk '/^static int regtrace_t23_probe_ivdc_safe\(/{p=1} p{print} p&&/^}/{exit}' "$c")
[ -n "$p" ] || err "probe_ivdc_safe not found"
echo "$p" | grep -q 'regtrace_t23_release_subdev_safe(pdev, sd, false, true, NULL)' || err "L1: request_irq failure does not release mem + subdev"
echo "$p" | grep -q 'release_subdev_safe(pdev, sd, false, false, NULL)' || err "L1: map failure does not release the subdev"
grep -q 'regtrace_t23_direct_enable_clks && !regtrace_t23_ivdc_clks_enabled' "$c" || err "L2: IVDC gate not tied to direct_enable_clks"
r=$(awk '/^static int regtrace_t23_remove_ivdc_safe\(/{p=1} p{print} p&&/^}/{exit}' "$c")
echo "$r" | grep -q 'regtrace_t23_ivdc_free_ring()' || err "L3: remove does not free the IVDC ring"
n=$(grep -c 'spin_lock_irqsave(&t23_top1c_lock' "$c"); [ "$n" = 3 ] || err "L4: $n process-context 0x1c locks (expected 3)"
grep -q 'spin_lock(&t23_top1c_lock)' "$c" || err "L4: frame-start guard not under the 0x1c lock"
x=$(awk '/^void t32_hw_exit\(/{p=1} p{print} p&&/^}/{exit}' "$w")
echo "$x" | awk '/t32_stats_off\(\)/{a=1} a&&/msleep\(T32_HW_EXIT_DRAIN_MS\)/{ok=1} /free_pages\(ae_buf/{if(!ok)bad=1} END{exit !(ok&&!bad)}' || err "L6: rmmod frees buffers without the drain wait"
# openimp #929: the once requests 0xc00454cd / 0xc00454ce are served on T32 and T33
grep -q 'case T32_IOC_SET_FRAME_DROP_ONCE:' "$k" && grep -q 'case T32_IOC_GET_FRAME_DROP_ONCE:' "$k" || err "#929: frame drop once ioctls not dispatched"
grep -q 'T32_IOC_SET_FRAME_DROP_ONCE	0xc00454cdU' "$d/t32/tx_isp_t32_abi.h" && grep -q 'T32_IOC_GET_FRAME_DROP_ONCE	0xc00454ceU' "$d/t32/tx_isp_t32_abi.h" || err "#929: once request numbers wrong"
[ $fail = 0 ] && echo "check_919_lows: OK"
exit $fail
