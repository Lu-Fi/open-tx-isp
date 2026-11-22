#!/bin/sh
# check_t23_param_own.sh [driver/t23]   (Lu-Fi/openimp#770, #787)
# The T23 module parameters direct_mode / ivdc_mem_line / ivdc_threshold_line / isp_memopt have their own
# storage (t23_param_*, module_param_named) that only init_module reads: lifted code writes through the
# vendor names (tiziano_ccm_init, tisp_ae0_get_hist, tisp_lsc_mirror_flip, ...) and must not reach them.
# Also guards the #787 MSCA flip path: the flip write waits for a pending 0xd010 request.
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
code=$(grep -v '^[[:space:]]*\(/\*\|\*\|//\)' "$f")
for p in direct_mode ivdc_mem_line ivdc_threshold_line isp_memopt; do
	echo "$code" | grep -Eq "^module_param\($p," && { echo "check_t23_param_own: $p is a plain module_param (lifted code writes the word)"; fail=1; }
	echo "$code" | grep -Eq "^module_param_named\($p, t23_param_$p, int," || { echo "check_t23_param_own: $p not bound to t23_param_$p"; fail=1; }
	n=$(echo "$code" | grep -c "t23_param_$p\b")
	# declaration + module_param_named + the one copy in init_module
	[ "$n" -eq 3 ] || { echo "check_t23_param_own: t23_param_$p used $n times (want 3: decl, param, init_module copy)"; fail=1; }
done
echo "$code" | grep -q 'regtrace_t23_msca_update_wait();' || { echo "check_t23_param_own: MSCA flip write without the pending-request wait (#787)"; fail=1; }
[ $fail -eq 0 ] && echo "check_t23_param_own: OK"
exit $fail
