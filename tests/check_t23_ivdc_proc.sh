#!/bin/sh
# check_t23_ivdc_proc.sh [driver/t23]   (Lu-Fi/open-tx-isp#4, #6; openimp #807)
# #4: /proc/jz/isp/isp-ivdc exists (0444), the IVDC ISR counts the status bits like the vendor ISR
#     (tx-isp-t23.ko 0xbf84) and the show routine keeps the vendor line set.
# #6: the lifted tiziano_ccm_init / tiziano_csccr_init / tisp_msca_init no longer address stock
#     statics through the ivdc_threshold_line / ivdc_mem_line / direct_mode placeholders.
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
grep -q 'proc_create("isp-ivdc", 0444' "$f" || { echo "check_t23_ivdc_proc: no isp-ivdc proc node"; fail=1; }
isr=$(sed -n '/^    if (sd == regtrace_t23_ivdc_sd) {/,/^    printk_ratelimited/p' "$f")
echo "$isr" | grep -q 'regtrace_t23_ivdc_count(status0)' || { echo "check_t23_ivdc_proc: IVDC ISR does not count the status bits"; fail=1; }
cnt=$(sed -n '/^static void regtrace_t23_ivdc_count(/,/^}/p' "$f")
for m in 0x00000008U 0x00000004U 0x00000002U 0x00000001U 0x00010000U 0x00020000U 0x00040000U 0x00004000U \
	0x00080000U 0x00008000U 0x04000000U 0x00000080U 0x00000040U 0x00000020U 0x00000010U 0x00400000U; do
	echo "$cnt" | grep -q "st & $m" || { echo "check_t23_ivdc_proc: status bit $m not counted"; fail=1; }
done
show=$(sed -n '/^static int regtrace_t23_ivdc_proc_show(/,/^}/p' "$f")
for l in 'IVDC INFO' 'IVDC FIFO Overflow' 'IVDC DDR Overflow' 'IVDC Vpu Err' 'ISP Frame Done' 'IVDC Y Max ddr line' 'IVDC UV Max ddr line'; do
	echo "$show" | grep -q "$l" || { echo "check_t23_ivdc_proc: show lacks '$l'"; fail=1; }
done
echo "$show" | grep -q 'regtrace_t23_ivdc_clks_enabled' || { echo "check_t23_ivdc_proc: show reads 0xec without the clock gate"; fail=1; }
for fn in tiziano_ccm_init tiziano_csccr_init tisp_msca_init; do
	body=$(sed -n "/^static int32_t $fn(/,/^}/p" "$f" | grep -v '^[[:space:]]*\(/\*\|\*\)')
	[ -n "$body" ] || { echo "check_t23_ivdc_proc: $fn missing"; fail=1; continue; }
	echo "$body" | grep -q 'ivdc_threshold_line\|ivdc_mem_line\|direct_mode' &&
		{ echo "check_t23_ivdc_proc: $fn still writes through an ivdc/direct_mode placeholder"; fail=1; }
done
[ $fail -eq 0 ] && echo "check_t23_ivdc_proc: OK"
exit $fail
