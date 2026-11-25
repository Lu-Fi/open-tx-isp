#!/bin/sh
# check_t32_frame_ts.sh [driver/t32]   (Lu-Fi/openimp#884)
# Source guard for the T32 / T33 frame time stamp: the vendor ko's frame_chan_event stamps the
# done buffer with the raw monotonic clock (T32 0x13220 getrawmonotonic64, T33 0x11f78
# getrawmonotonic, tv_usec = tv_nsec / 1000), and the vendor libimp compares it with
# IMP_System_GetTimeStamp (CLOCK_MONOTONIC_RAW).  A wall clock stamp (do_gettimeofday) made
# GetTimedFrame and SetRgnAttrWithTimestamp fail with the vendor library on our ko.
d=${1:-$(dirname "$0")/../driver/t32}
f=$d/tx_isp_t32_fs.c
[ -f "$f" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
fail=0
grep -q 'getrawmonotonic(&ts);' "$f" || { echo "check_t32_frame_ts: fs_stamp() does not use getrawmonotonic"; fail=1; }
grep -q 'tv->tv_usec = ts.tv_nsec / 1000;' "$f" || { echo "check_t32_frame_ts: tv_usec not tv_nsec / 1000"; fail=1; }
n=$(grep -c 'do_gettimeofday(&f->buf' "$f")
[ "$n" = 0 ] || { echo "check_t32_frame_ts: $n buffer stamps still use do_gettimeofday"; fail=1; }
n=$(grep -c 'fs_stamp(&f->buf\[i\].ts);' "$f")
[ "$n" = 2 ] || { echo "check_t32_frame_ts: expected 2 fs_stamp() buffer stamps (MSCA done, IVDC), got $n"; fail=1; }
[ $fail = 0 ] && echo "check_t32_frame_ts: ok"
exit $fail
