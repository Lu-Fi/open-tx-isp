#!/bin/sh
# check_t32_flip_lock.sh [driver/t32]   (Lu-Fi/openimp#779, #780)
# Source guards for the T32/T33 sensor-flip work and the IVDC mode:
#  1. t33_flip_work runs its VIC + sensor part under core.lock (t32_core_lock) and rechecks
#     t32_core_flip_ready (sensor 0 registered + streaming, clocks on) there;
#  2. t32_hw_stream_off and the last close (before t32_clks_deactivate) drop the VIC hold (t33_hw_flip_stop);
#  3. t33_flip_w is cancelled synchronously only in t32_hw_exit (rmmod, core.lock not held): a
#     cancel_work_sync under core.lock would deadlock with the work waiting for core.lock;
#  4. (#780, #783) SET_MODE goes through t32_ivdc_set_mode_checked (-EBUSY while ivdc_live) and t32_ivdc_stream runs core_ops_init under ivdc_mutex.
#  5. (#828) sensor rmmod and the last close clear ivdc_live (t32_ivdc_unlive) under core.lock.
d=${1:-$(dirname "$0")/../driver/t32}
hw=$d/tx_isp_t32_hw.c core=$d/tx_isp_t32_core.c iv=$d/tx_isp_t32_ivdcdrv.c
for f in "$hw" "$core" "$iv"; do [ -f "$f" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }; done
fail=0
body() { awk -v s="$2" 'index($0, s) == 1 {on = 1} on {print} on && /^}/ {exit}' "$1"; }
need() { echo "$1" | grep -q -- "$2" || { echo "check_t32_flip_lock: $3"; fail=1; }; }
w=$(body "$hw" 'static void t33_flip_work(')
need "$w" 't32_core_lock();' 'flip work does not take core.lock'
need "$w" 't32_core_flip_ready()' 'flip work does not recheck the state under core.lock'
need "$(echo "$w" | grep -n 't32_core_lock();\|t32_core_sensor_flip(' | head -1)" 't32_core_lock' 'core.lock must be taken before the sensor flip'
need "$(body "$core" 'int t32_core_flip_ready(')" 'vi->streaming && t32_clk_users' 'flip_ready must check streaming and the clocks'
need "$(body "$hw" 'void t32_hw_stream_off(')" 't33_hw_flip_stop();' 'stream-off does not drop the VIC hold'
r=$(body "$core" 'static int t32_isp_release(')
a=$(echo "$r" | grep -n 't33_hw_flip_stop();' | cut -d: -f1)
b=$(echo "$r" | grep -n 't32_clks_deactivate();' | cut -d: -f1)
if [ -z "$a" ] || [ -z "$b" ] || [ "$a" -ge "$b" ]; then
	echo "check_t32_flip_lock: the last close must drop the VIC hold before the clock gate"; fail=1
fi
n=$(grep -c 'cancel_work_sync(&t33_flip_w)' "$hw")
m=$(body "$hw" 'void t32_hw_exit(' | grep -c 'cancel_work_sync(&t33_flip_w)')
[ "$n" -ge 1 ] && [ "$n" = "$m" ] || { echo "check_t32_flip_lock: cancel_work_sync(&t33_flip_w) outside t32_hw_exit ($n/$m)"; fail=1; }
need "$(body "$iv" 'long t32_ivdc_misc_ioctl(')" 't32_ivdc_set_mode_checked(' 'SET_MODE not validated before publish'
s=$(body "$iv" 'int t32_ivdc_stream(')
need "$(echo "$s" | grep -A3 'mutex_lock(&ivdc_mutex);')" 't32_ivdc_core_ops_init(' 'core_ops_init not under ivdc_mutex'
need "$s" 'ivdc_live = on;' '#783: the stream does not fix the mode (ivdc_live)'
need "$(body "$iv" 'long t32_ivdc_misc_ioctl(')" 'ivdc_live)' '#783: SET_MODE does not pass the live state'
# 5. (#828) a stream that ends without STREAMOFF (sensor rmmod, last close) clears ivdc_live under core.lock
need "$(body "$core" 'void t32_core_sensor_driver_gone(' | grep -B30 'mutex_unlock(&core.lock);')" 't32_ivdc_unlive();' '#828: sensor rmmod leaves ivdc_live set'
r=$(body "$core" 'static int t32_isp_release(')
need "$(echo "$r" | sed -n '/!--t32_clk_users/,/mutex_unlock(&core.lock);/p')" 't32_ivdc_unlive();' '#828: last close leaves ivdc_live set'
need "$(body "$iv" 'void t32_ivdc_unlive(')" 'ivdc_live = 0;' '#828: t32_ivdc_unlive does not clear ivdc_live'
[ $fail = 0 ] && echo "check_t32_flip_lock: ok"
exit $fail
