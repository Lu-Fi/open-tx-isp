#!/bin/sh
# check_t23_ae_fs_expo.sh [driver/t23]   (Lu-Fi/openimp#988)
# Stock T23 sends the AE exposure to the sensor from the IRQ thread (status bits 13/27, expo_fs 0) or the
# frame-start work (bit 12, expo_fs 1); the lifted AE work must only latch it.  With bit 12 masked or the AE work writing at
# once, the stock AE saw its exposure one frame early and converged slower (comp 40->200: 11 s vs 6 s).
d=${1:-$(dirname "$0")/../driver/t23}
c=$d/tx_isp_t23_core.c; g=$d/tx_isp_t23_ae_oem_glue.inc
[ -f "$c" ] && [ -f "$g" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
m=$(grep '^#define T23_ISR_UNUSED' "$c")
case "$m" in *BIT\(13\)*|"") echo "check_t23_ae_fs_expo: IPC frame done (bit 13, stock IRQ-thread wake-up) masked or T23_ISR_UNUSED missing: $m"; fail=1;; esac
w=$(awk '/^static void t23_aelift_work_fn\(struct work_struct \*work\)$/{on=1} on{print} on&&/^}/{exit}' "$g")
echo "$w" | grep -q 't23_aelift_fs_latch(' || { echo "check_t23_ae_fs_expo: AE work does not latch the exposure (t23_aelift_fs_latch)"; fail=1; }
echo "$w" | grep -q 'regtrace_t23_call_sensor_exposure(' && { echo "check_t23_ae_fs_expo: AE work writes the sensor exposure at once"; fail=1; }
i=$(awk '/^static bool t23_aelift_irq\(uint32_t status\)$/{on=1} on{print} on&&/^}/{exit}' "$g")
echo "$i" | grep -q 't23_aelift_fs_irq(status);' || { echo "check_t23_ae_fs_expo: AE ISR does not run the frame-start write (t23_aelift_fs_irq)"; fail=1; }
[ $fail -eq 0 ] && echo "check_t23_ae_fs_expo: OK"
exit $fail
