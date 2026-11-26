#!/bin/sh
# check_t23_ivdc_ring.sh [driver/t23]   (Lu-Fi/openimp#914)
# The driver-allocated IVDC ring must not be freed while channel 0's kept MSCA output (msca_keep_enabled=2)
# still feeds the IVDC: free_ring stops the kept output first, and REQBUFS on a streaming IVDC channel is -EBUSY.
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
fr=$(sed -n '/^static void regtrace_t23_ivdc_free_ring(/,/^}/p' "$f")
q=$(echo "$fr" | grep -n 'regtrace_t23_ivdc_ring_quiesce()' | head -1 | cut -d: -f1)
x=$(echo "$fr" | grep -n 'dma_free_coherent(' | head -1 | cut -d: -f1)
{ [ -n "$q" ] && [ -n "$x" ] && [ "$q" -lt "$x" ]; } || { echo "check_t23_ivdc_ring: free_ring frees before stopping the kept MSCA ch0"; fail=1; }
qz=$(sed -n '/^static void regtrace_t23_ivdc_ring_quiesce(/,/^}/p' "$f")
echo "$qz" | grep -q 'regtrace_t23_msca_kept &= ~1U' || { echo "check_t23_ivdc_ring: quiesce does not drop the kept bit"; fail=1; }
echo "$qz" | grep -q "system_reg_write(0xd040U, system_reg_read(0xd040U) & ~1U)" || { echo "check_t23_ivdc_ring: quiesce does not clear 0xd040 bit 0"; fail=1; }
echo "$qz" | grep -q 'regtrace_t23_msca_lock' || { echo "check_t23_ivdc_ring: quiesce not under the MSCA lock (ISR refill race)"; fail=1; }
rq=$(sed -n '/case REGTRACE_FRAMECHAN_REQBUFS: {/,/case REGTRACE_FRAMECHAN_QBUF: {/p' "$f")
b=$(echo "$rq" | grep -n 'ret = -EBUSY' | head -1 | cut -d: -f1)
a=$(echo "$rq" | grep -n 'regtrace_t23_ivdc_reqbufs(\|regtrace_t23_ivdc_free_ring()' | head -1 | cut -d: -f1)
{ [ -n "$b" ] && [ -n "$a" ] && [ "$b" -lt "$a" ]; } || { echo "check_t23_ivdc_ring: REQBUFS can touch the ring of a streaming channel"; fail=1; }
[ $fail -eq 0 ] && echo "check_t23_ivdc_ring: OK"
exit $fail
