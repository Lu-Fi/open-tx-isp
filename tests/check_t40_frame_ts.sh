#!/bin/sh
# check_t40_frame_ts.sh [driver/t40]   (Lu-Fi/openimp#886)
# Source guard for the T40 frame time stamp.
# 1. private_getrawmonotonic must hand getrawmonotonic64 a real struct timespec64: on the 4.4
#    kernel it is { time64_t tv_sec; long tv_nsec; } (16 bytes, stores at 0/4/8).  The lift used
#    an 8-byte int32_t[2]: tv_nsec overwrote the saved s0 and tv_usec was always 0.  Vendor
#    tx-isp-t40.ko 0x914: 40-byte frame, timespec64 at sp+16, copies words 0 and 2.
# 2. DQBUF (v4l2_buffer words 5/6) carries the raw monotonic stamp of the frame-done interrupt,
#    tv_usec = tv_nsec / 1000 (vendor frame_chan_event 0xee9c..0xeed0).
d=${1:-$(dirname "$0")/../driver/t40}
f=$d/tx_isp_t40_recovered.c
[ -f "$f" ] || { echo "usage: $0 [driver/t40]" >&2; exit 2; }
fail=0
body=$(awk '/^int32_t private_getrawmonotonic\(int32_t \*arg1\)$/{p=1} p{print} p&&/^}/{exit}' "$f")
echo "$body" | grep -q 'struct timespec64 ts;' || { echo "check_t40_frame_ts: private_getrawmonotonic does not use struct timespec64"; fail=1; }
echo "$body" | grep -q 'getrawmonotonic64(&ts);' || { echo "check_t40_frame_ts: private_getrawmonotonic does not pass &ts"; fail=1; }
echo "$body" | grep -q 'arg1\[1\] = (int32_t)ts.tv_nsec;' || { echo "check_t40_frame_ts: private_getrawmonotonic word 1 is not tv_nsec"; fail=1; }
n=$(grep -c 'getrawmonotonic64(var_' "$f")
[ "$n" = 0 ] || { echo "check_t40_frame_ts: $n getrawmonotonic64 calls into a local array"; fail=1; }
st=$(awk '/^static void regtrace_frame_stamp\(uint32_t ts\[2\]\)$/{p=1} p{print} p&&/^}/{exit}' "$f")
echo "$st" | grep -q 'getrawmonotonic64(&now);' || { echo "check_t40_frame_ts: regtrace_frame_stamp not raw monotonic"; fail=1; }
echo "$st" | grep -q 'ts\[1\] = (uint32_t)now.tv_nsec / 1000U;' || { echo "check_t40_frame_ts: tv_usec not tv_nsec / 1000"; fail=1; }
n=$(grep -c 'regtrace_frame_stamp(item->ts);' "$f")
[ "$n" = 2 ] || { echo "check_t40_frame_ts: expected 2 interrupt stamps for the deferred frame-done work, got $n"; fail=1; }
grep -q 'regtrace_frame_stamp(regtrace_irq_frame_done_queue_ts\[channel\]\[slot\]);' "$f" || { echo "check_t40_frame_ts: queue push does not stamp the ring slot"; fail=1; }
n=$(grep -c 'words\[5\] = ts\[0\];\|words\[5\] = done_ts\[0\];' "$f")
[ "$n" = 2 ] || { echo "check_t40_frame_ts: expected 2 DQBUF timestamp writes (irq-dqbuf, private), got $n"; fail=1; }
[ $fail = 0 ] && echo "check_t40_frame_ts: ok"
exit $fail
