#!/bin/sh
# check_t33_tmo_flip.sh [driver/t32]   (Lu-Fi/openimp#928, SDK 2.1.0 PRJ008 ispcore_core_ops_ioctl frame-done flip)
# T33: the flip work flips the LSC tables (deferred from the 0x73 control) and mirrors the TMO V table right after the sensor flip, a fresh table (ddrv init) resets
# the tracked mode, the table flip syncs the 6000 bytes to the device.
d=${1:-$(dirname "$0")/../driver/t32}
hw=$d/tx_isp_t32_hw.c
[ -f "$hw" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
fail=0
body() { awk -v s="$2" 'index($0, s) == 1 {on = 1} on {print} on && /^}/ {exit}' "$1"; }
need() { echo "$1" | grep -q -- "$2" || { echo "check_t33_tmo_flip: $3"; fail=1; }; }
w=$(body "$hw" 'static void t33_flip_work(')
a=$(echo "$w" | grep -n 't32_core_sensor_flip(' | head -1 | cut -d: -f1)
b=$(echo "$w" | grep -n 't33_tmo_hvflip(0, mode)' | head -1 | cut -d: -f1)
if [ -z "$a" ] || [ -z "$b" ] || [ "$b" -le "$a" ]; then
	echo "check_t33_tmo_flip: the flip work must call t33_tmo_hvflip after t32_core_sensor_flip"; fail=1
fi
c=$(echo "$w" | grep -n 't33_lsc_hvflip(0, lh, lv)' | head -1 | cut -d: -f1)
if [ -z "$c" ] || [ -z "$a" ] || [ "$c" -le "$a" ]; then
	echo "check_t33_tmo_flip: the LSC flip must run in the flip work after the sensor flip"; fail=1
fi
grep -A3 'int t32_tx_os_tisp_lsc_hvflip' "$d/tx_isp_t32_tunctl.c" | grep -q 't33_lsc_hvflip_defer' || { echo "check_t33_tmo_flip: T33 0x73 does not defer the LSC flip"; fail=1; }
need "$(body "$hw" 'void t33_tmo_hvflip(')" 'dma_cache_wback_inv' 'table flip does not sync the cache'
need "$(body "$hw" 'void t33_tmo_hvflip(')" 't33_tmo_hvflip_table(tab, (d >> 1) & 1, d & 1)' 'table flip does not apply the mode difference'
need "$(body "$hw" 'static void t33_g16_tmo_ddrv(')" 't33_tmo_flip_cur = 0' 'a fresh V table does not reset the tracked mode'
[ $fail = 0 ] && echo "check_t33_tmo_flip: OK"
exit $fail
