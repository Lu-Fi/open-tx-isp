#!/bin/sh
# check_t23_framedrop_wrap.sh [driver/t23]   (Lu-Fi/open-tx-isp#5)
# The direct-mode frame-start ISR wraps the frame-drop counter (+478) only when it passed
# the period (+477) + 1, like the vendor; the beqzl delay slot must not store 0 always.
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
awk '/beqzl\tv0,6b930/{p=1} p&&/if \(!v0\)/{ok=1} END{exit !ok}' "$f" || { echo "check_t23_framedrop_wrap: counter store not conditional (open-tx-isp#5)"; exit 1; }
echo "check_t23_framedrop_wrap: ok"
