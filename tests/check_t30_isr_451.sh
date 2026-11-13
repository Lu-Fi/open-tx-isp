#!/bin/sh
# check_t30_isr_451.sh [driver/t30]   (Lu-Fi/openimp #451)
# Source guards for the T30 core ISR against the OEM ispcore_interrupt_service_routine:
#  1. status bit 13 (event 50) is handled (OEM case 13, 0xde94: channel-1 DMA direction/writer);
#  2. IRQ_WAKE_THREAD only on frame start (bit 7, OEM 0xd5ec), not on AE (bit 3);
#  4. the compact AE runs as work, not in the hard IRQ, on the histogram copied in the IRQ (OEM ae_read_full_histogram_data); no 0xa0 re-poll (beyond vendor);
#  3. vic_sensor_ops_ioctl 0x200000c writes VIC +0 through the register base (OEM 0x564), not through a byte value.
d=${1:-$(dirname "$0")/../driver/t30}
f=$d/tx_isp_t30_recovered.c
[ -f "$f" ] || { echo "usage: $0 [driver/t30]" >&2; exit 2; }
fail=0
grep -q 'if (irq_status & BIT(13))' "$f" && grep -q 'tx_isp_t30_core_chan1_event(core);' "$f" || { echo "check_t30_isr_451: bit 13 not handled"; fail=1; }
n=$(grep -c 'ret = IRQ_WAKE_THREAD;' "$f")
[ "$n" = 1 ] || { echo "check_t30_isr_451: expected 1 IRQ_WAKE_THREAD in the core ISR, found $n"; fail=1; }
grep -q 'simple_3a && (irq_status & BIT(3))' "$f" && { echo "check_t30_isr_451: AE still wakes the IRQ thread"; fail=1; }
grep -q '\*(int \*\*)((char \*)vic + 0xb8)\[0\]' "$f" && { echo "check_t30_isr_451: vic ioctl byte-address store"; fail=1; }
grep -q "queue_work(system_highpri_wq, &tx_isp_t30_ae_work);" "$f" && grep -q "tx_isp_t30_ae_snapshot_histogram();" "$f" || { echo "check_t30_isr_451: AE work / histogram copy missing"; fail=1; }
grep -q "goto again" "$f" && { echo "check_t30_isr_451: 0xa0 re-poll is beyond vendor"; fail=1; }
[ $fail = 0 ] && echo "check_t30_isr_451: ok"
exit $fail
