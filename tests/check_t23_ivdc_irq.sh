#!/bin/sh
# check_t23_ivdc_irq.sh [driver/t23]   (Lu-Fi/openimp#910)
# The IVDC keeps its interrupt mask and latched status across an rmmod. The probe must map, quiesce
# (clocks held) and register the IVDC sd BEFORE requesting IRQ 46, the remove must quiesce before the
# irq is freed, and the ISR must not touch the IVDC while its clocks are off (bus hang, WDT reset).
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
probe=$(sed -n '/^static int regtrace_t23_probe_ivdc_safe(/,/^}/p' "$f" | grep -v '^[[:space:]]*\(/\*\|\*\)')
ln(){ echo "$probe" | grep -n "$1" | head -1 | cut -d: -f1; }
m=$(ln 'regtrace_t23_map_mem(pdev, sd'); q=$(ln 'regtrace_t23_ivdc_quiesce(sd, "probe")')
o=$(ln 'regtrace_t23_ivdc_sd = sd;'); r=$(ln 'regtrace_t23_request_irq(pdev, sd)')
if [ -z "$m" ] || [ -z "$q" ] || [ -z "$o" ] || [ -z "$r" ]; then
	echo "check_t23_ivdc_irq: probe step missing (map=$m quiesce=$q owner=$o irq=$r)"; fail=1
elif [ "$m" -ge "$q" ] || [ "$q" -ge "$r" ] || [ "$o" -ge "$r" ]; then
	echo "check_t23_ivdc_irq: probe order wrong (map=$m quiesce=$q owner=$o irq=$r; want map<quiesce<irq, owner<irq)"; fail=1
fi
rem=$(sed -n '/^static int regtrace_t23_remove_ivdc_safe(/,/^}/p' "$f")
a=$(echo "$rem" | grep -n 'regtrace_t23_ivdc_quiesce(sd, "remove")' | cut -d: -f1)
b=$(echo "$rem" | grep -n 'regtrace_t23_release_subdev_safe(' | cut -d: -f1)
{ [ -n "$a" ] && [ -n "$b" ] && [ "$a" -lt "$b" ]; } || { echo "check_t23_ivdc_irq: remove does not quiesce the IVDC before the irq is freed"; fail=1; }
qz=$(sed -n '/^static void regtrace_t23_ivdc_quiesce(/,/^}/p' "$f")
echo "$qz" | grep -q 'private_clk_enable(clk)' || { echo "check_t23_ivdc_irq: quiesce does not hold the IVDC clocks"; fail=1; }
grep -q 'if (!regtrace_t23_ivdc_clks_enabled)' "$f" || { echo "check_t23_ivdc_irq: IVDC ISR without the gated-clock guard"; fail=1; }
[ $fail -eq 0 ] && echo "check_t23_ivdc_irq: OK"
exit $fail
