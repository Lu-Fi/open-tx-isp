#!/bin/sh
# check_t33_streamon_wait.sh [driver/t32]   (Lu-Fi/openimp#927)
# T33 stream-on waits for the register lists like SDK 2.1.0 lib_tisp_stream_on: up to 1001
# rounds with msleep(5), writes 0x22028/0x2202c = 0 while waiting, no 0x1040 on timeout.
d=${1:-$(dirname "$0")/../driver/t32}
f=$d/tx_isp_t32_hw.c
[ -f "$f" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
fail=0
bad(){ echo "check_t33_streamon_wait: $*"; fail=1; }
grep -q 'i < 1001; i++' "$f" || bad "wait is not 1001 rounds (#927)"
grep -q 'isp_wr(T33_RCFG_CLR28, 0);' "$f" || bad "0x22028 not written while waiting (#927)"
grep -q 'msleep(5);' "$f" || bad "no msleep(5) in the list wait (#927)"
grep -B2 'isp_wr(0x1040, 1);' "$f" | grep -q 't33_rcfg_failed' || bad "0x1040 not skipped after a list timeout (#927)"
grep -q 'T33_RCFG_CLR28[[:space:]]*0x22028U' "$d/tx_isp_t32_rcfg33.h" || bad "T33_RCFG_CLR28 define missing"
[ $fail = 0 ] && echo "check_t33_streamon_wait: ok"
exit $fail
