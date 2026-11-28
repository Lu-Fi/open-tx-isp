#!/bin/sh
# check_t23_isp_block_reset.sh [driver/t23]   (Lu-Fi/openimp#800)
# Every tx-isp session start must run the vendor ISP block reset (CPM 0xb00000c4 stop 0x200000 / ack 0x100000 /
# soft reset 0x400000, tx-isp-t23.ko 1.3.0 private_reset_tx_isp_module 0x11f34) before the core start, the session
# release with its FIFO clear only as the fallback, and a killed streamer (tx-isp closed first) must not leave the
# input running past the last frame channel stop.
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
rr=$(sed -n '/^static bool regtrace_t23_isp_block_reset_run(/,/^}/p' "$f")
for p in 'T23_SRBC_ISP_STOP, T23_CPM_SRBC' '& T23_SRBC_ISP_ACK' '| T23_SRBC_ISP_RESET, T23_CPM_SRBC' '& ~T23_SRBC_ISP_RESET, T23_CPM_SRBC' 'regtrace_t23_core_started' 'regtrace_t23_vic_streaming' 'regtrace_t23_msca_kept = 0'; do
  echo "$rr" | grep -qF "$p" || { echo "check_t23_isp_block_reset: reset_run lacks '$p'"; fail=1; }
done
grep -q '#define T23_CPM_SRBC.*0xb00000c4UL' "$f" || { echo "check_t23_isp_block_reset: SRBC address changed"; fail=1; }
grep -q '#define T23_SRBC_ISP_STOP.*0x200000U' "$f" && grep -q '#define T23_SRBC_ISP_ACK.*0x100000U' "$f" && grep -q '#define T23_SRBC_ISP_RESET.*0x400000U' "$f" || { echo "check_t23_isp_block_reset: SRBC bits differ from the vendor"; fail=1; }
en=$(sed -n '/^static int regtrace_t23_txisp_stream_locked(/,/^}/p' "$f")
r=$(echo "$en" | grep -n 'reset = regtrace_t23_isp_block_reset_run(reason)' | head -1 | cut -d: -f1)
p=$(echo "$en" | grep -n 'ret = regtrace_t23_tisp_prestart(reason)' | head -1 | cut -d: -f1)
{ [ -n "$r" ] && [ -n "$p" ] && [ "$r" -lt "$p" ]; } || { echo "check_t23_isp_block_reset: block reset not before the core start"; fail=1; }
[ "$(echo "$en" | grep -c 'if (!reset)')" -ge 2 ] || { echo "check_t23_isp_block_reset: session release not limited to the no-reset fallback"; fail=1; }
grep -q 'atomic_read(&regtrace_tx_isp_open_count) > 0)' "$f" || { echo "check_t23_isp_block_reset: chan_stop_keep_input keeps the input without a tx-isp owner"; fail=1; }
[ $fail -eq 0 ] && echo "check_t23_isp_block_reset: OK"
exit $fail
