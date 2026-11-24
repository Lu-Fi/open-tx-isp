#!/bin/sh
# check_t32_last_close.sh [driver/t32]   (Lu-Fi/openimp#806)
# Source guard: the last close of /dev/tx-isp (t32_isp_release) does what the vendor tx_isp_release ->
# tx_isp_vin_slake_subdev / ispcore_slake_module do (T33 0x1481c / 0x112018): frame channels stopped before
# core.lock, then a streaming sensor stopped, the input cleared and the sensor released, all before the
# clocks are gated.  Without it a killed client left the sensor streaming and input_on set: the next
# session got no frames (QEMU -M ingenic-t33 qk806n vs qk806a).
# #830 (T33L device, 10 x kill -9 + restart): the VIC hold release, the lib deinit (T33) and the clock gate
# follow the sensor release, the IVDC live flag (#828) is cleared after the gate; a ko without these steps
# gave no frames / a timps recovery reset after a killed streamer.
d=${1:-$(dirname "$0")/../driver/t32}
f=$d/tx_isp_t32_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
body=$(awk '/^static int t32_isp_release\(/{p=1} p{print} p&&/^}/{exit}' "$f")
[ -n "$body" ] || { echo "check_t32_last_close: t32_isp_release not found"; exit 1; }
ln() { printf '%s\n' "$body" | grep -n -- "$1" | head -1 | cut -d: -f1; }
fs=$(ln 't32_fs_stop_all();'); lk=$(ln 'if (t32_clk_users && !--t32_clk_users)')
st=$(ln 't32_sensor_stream(vi, 0);'); in=$(ln 'vi->input_on = 0;'); rl=$(ln 't32_vi_release(vi);')
ck=$(ln 't32_clks_deactivate();')
fl=$(ln 't33_hw_flip_stop();'); de=$(ln 't33_hw_lib_deinit();'); ul=$(ln 't32_ivdc_unlive();')
for v in "$fl" "$de" "$ul"; do
	[ -n "$v" ] || { echo "check_t32_last_close: missing #830 step (flip_stop=$fl lib_deinit=$de unlive=$ul)"; exit 1; }
done
[ "$rl" -lt "$fl" ] && [ "$fl" -lt "$de" ] && [ "$de" -lt "$ck" ] && [ "$ck" -lt "$ul" ] ||
	{ echo "check_t32_last_close: #830 order wrong (release=$rl flip_stop=$fl lib_deinit=$de clk=$ck unlive=$ul)"; exit 1; }
for v in "$fs" "$lk" "$st" "$in" "$rl" "$ck"; do
	[ -n "$v" ] || { echo "check_t32_last_close: missing step (fs=$fs last=$lk stream=$st input=$in release=$rl clk=$ck)"; exit 1; }
done
if [ "$fs" -lt "$lk" ] && [ "$lk" -lt "$st" ] && [ "$st" -lt "$in" ] && [ "$in" -lt "$rl" ] && [ "$rl" -lt "$ck" ]; then
	echo "check_t32_last_close: ok"; exit 0
fi
echo "check_t32_last_close: order wrong (fs=$fs last=$lk stream=$st input=$in release=$rl clk=$ck)"; exit 1
