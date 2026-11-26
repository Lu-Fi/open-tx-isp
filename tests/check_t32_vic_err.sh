#!/bin/sh
# check_t32_vic_err.sh [driver/t32]   (Lu-Fi/openimp#897, #915)
# The live T32 VIC interrupt (t32_vic_irq) must recover a VIC error like the vendor SDK 2.0.0
# isp_vic_interrupt_service_routine (libPRJ007-firmware-310-540.a 0x2da4..0x2e00): per VIC channel a
# status with bits 0xffffff00 runs lib_tisp_vic_reset (0x200 pulse, 0x18 = 0, 0x10 = 4 on both VICs)
# and lib_tisp_vic_stream_on (0x10 = 1 on both VICs).  Without it a VIC that starts in error never
# delivers another frame end (first streamer session after boot, PRJ007 gc4653).
d=${1:-$(dirname "$0")/../driver/t32}
hw=$d/tx_isp_t32_hw.c
[ -f "$hw" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
fail=0
body() { awk -v s="$2" 'index($0, s) == 1 {on = 1} on {print} on && /^}/ {exit}' "$1"; }
need() { echo "$1" | grep -q -- "$2" || { echo "check_t32_vic_err: $3"; fail=1; }; }
i=$(body "$hw" 'static irqreturn_t t32_vic_irq(')
need "$i" 'if (ACCESS_ONCE(t32_vic_on) && (s0 & 0xffffff00U))' 'VIC0 error status does not trigger the recovery, or not gated on the stream (#915)'
need "$i" 'if (ACCESS_ONCE(t32_vic_on) && (t0 & 0xffffff00U))' 'VIC1 error status does not trigger the recovery, or not gated on the stream (#915)'
# #915: stream-off clears the gate and masks the VIC interrupts before the VIC stops (vendor order)
o=$(body "$hw" 'void t32_hw_stream_off(')
g=$(echo "$o" | grep -n 'ACCESS_ONCE(t32_vic_on) = false;' | head -1 | cut -d: -f1)
m=$(echo "$o" | grep -n 't32_vic_wr(0, T32_VIC_INT_MSK, 0xffffffff);' | head -1 | cut -d: -f1)
c=$(echo "$o" | grep -n 't32_vic_wr(0, T32_VIC_CTRL, 4);' | head -1 | cut -d: -f1)
if [ -z "$g" ] || [ -z "$m" ] || [ -z "$c" ] || [ "$g" -ge "$c" ] || [ "$m" -ge "$c" ]; then
	echo "check_t32_vic_err: stream-off must clear t32_vic_on and mask the VIC irqs before CTRL = 4 (#915)"; fail=1
fi
grep -q 'ACCESS_ONCE(t32_vic_on) = true;' "$hw" || { echo "check_t32_vic_err: stream-on never sets t32_vic_on (#915)"; fail=1; }
r=$(body "$hw" 'static void t32_vic_err_restart(')
need "$r" 't32_vic_wr(i, 0x200, 0);' 'reset pulse of 0x200 missing'
need "$r" 't32_vic_wr(i, 0x200, v);' '0x200 is not restored after the pulse'
need "$r" 't32_vic_wr(1, 0x018, 0);' '0x18 = 0 missing'
a=$(echo "$r" | grep -n 'T32_VIC_CTRL, 4);' | tail -1 | cut -d: -f1)
b=$(echo "$r" | grep -n 'T32_VIC_CTRL, 1);' | head -1 | cut -d: -f1)
if [ -z "$a" ] || [ -z "$b" ] || [ "$a" -ge "$b" ]; then
	echo "check_t32_vic_err: the VICs must be reset (0x10 = 4) before they run again (0x10 = 1)"; fail=1
fi
[ $fail -eq 0 ] && echo "check_t32_vic_err: ok"
exit $fail
