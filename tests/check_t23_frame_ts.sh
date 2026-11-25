#!/bin/sh
# check_t23_frame_ts.sh [driver/t23]   (Lu-Fi/openimp#887)
# Source guard for the T23 frame time stamp: like the vendor tx-isp-t23.ko frame_chan_event
# (0xed5c..0xedac: private_getrawmonotonic, tv_usec = tv_nsec / 1000), every completion pushed
# to the done ring is stamped and DQBUF returns it in v4l2_buffer words 5/6.  Before, words 5/6 = 0.
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
st=$(awk '/^static void regtrace_framechan_done_stamp\(struct regtrace_framechan_done \*done\)$/{p=1} p{print} p&&/^}/{exit}' "$f")
echo "$st" | grep -q 'private_getrawmonotonic(&now);' || { echo "check_t23_frame_ts: stamp is not raw monotonic"; fail=1; }
echo "$st" | grep -q 'done->ts_usec = (uint32_t)now.tv_nsec / 1000U;' || { echo "check_t23_frame_ts: tv_usec is not tv_nsec / 1000"; fail=1; }
pg=$(awk '/^static void private_getrawmonotonic\(struct timespec \*ts\)$/{p=1} p{print} p&&/^}/{exit}' "$f")
echo "$pg" | grep -q 'getrawmonotonic(ts);' || { echo "check_t23_frame_ts: private_getrawmonotonic does not call getrawmonotonic"; fail=1; }
# every done-ring producer stamps its entry: MSCA completion and the direct-mode frame start
for fn in regtrace_framechan_complete_fifo regtrace_t23_ivdc_frame_start; do
  b=$(awk -v fn="$fn" '$0 ~ "^static void " fn "\\(" && !/;$/{p=1} p{print} p&&/^}/{exit}' "$f")
  echo "$b" | grep -q 'regtrace_framechan_done_stamp(done);' || { echo "check_t23_frame_ts: $fn does not stamp its ring entry"; fail=1; }
done
dq=$(awk '/^static long regtrace_framechan_repair_dqbuf\(int channel, unsigned long arg,$/{p=1} p{print} p&&/^}/{exit}' "$f")
n=$(echo "$dq" | grep -c 'words\[TX_ISP_FRAME_WORD_TIMESTAMP_SEC\] = done.ts_sec;')
[ "$n" = 2 ] || { echo "check_t23_frame_ts: DQBUF writes tv_sec in $n of 2 branches"; fail=1; }
n=$(echo "$dq" | grep -c 'words\[TX_ISP_FRAME_WORD_TIMESTAMP_USEC\] = done.ts_usec;')
[ "$n" = 2 ] || { echo "check_t23_frame_ts: DQBUF writes tv_usec in $n of 2 branches"; fail=1; }
[ $fail = 0 ] && echo "check_t23_frame_ts: OK"
exit $fail
