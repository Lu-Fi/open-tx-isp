#!/bin/sh
# check_t32_ldc_reset.sh [driver/t32]   (Lu-Fi/openimp#926)
# Source guard: SDK 2.1.0 tx_isp_release calls the new tx_isp_ldc_online_reset on the last close (clears the LDC
# mode and the online flag, ldc_core_ops_ioctl 0x5000005).  Ours: the last close resets core.ldc_mode and calls
# t32_ldc_online_reset(), which clears the sticky online state (T33: ldc_online_mode + ldc_init_on, T32: t32_ldc_sticky).
d=${1:-$(dirname "$0")/../driver/t32}
c=$d/tx_isp_t32_core.c; l=$d/tx_isp_t32_snapldc.c
[ -f "$c" ] && [ -f "$l" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
body=$(awk '/^static int t32_isp_release\(/{p=1} p{print} p&&/^}/{exit}' "$c")
fail() { echo "check_t32_ldc_reset: $1"; exit 1; }
printf '%s\n' "$body" | grep -q 'core.ldc_mode = 0;' || fail "release does not reset core.ldc_mode"
printf '%s\n' "$body" | grep -q 't32_ldc_online_reset();' || fail "release does not call t32_ldc_online_reset"
n=$(printf '%s\n' "$body" | grep -n 't32_ldc_online_reset();' | head -1 | cut -d: -f1)
k=$(printf '%s\n' "$body" | grep -n 't32_clks_deactivate();' | head -1 | cut -d: -f1)
[ "$n" -lt "$k" ] || fail "reset must come before the clock gate"
cnt=$(grep -c '^void t32_ldc_online_reset(void)' "$l")
[ "$cnt" = 2 ] || fail "expected a T33 and a T32 t32_ldc_online_reset, found $cnt"
awk '/^void t32_ldc_online_reset\(void\)/{p=1} p&&/ldc_online_mode = 0;/{a++} p&&/ldc_init_on = 0;/{b++} p&&/t32_ldc_sticky = 0;/{c++} p&&/^}/{p=0} END{exit !(a&&b&&c)}' "$l" || fail "reset does not clear ldc_online_mode/ldc_init_on/t32_ldc_sticky"
echo "check_t32_ldc_reset: ok"
