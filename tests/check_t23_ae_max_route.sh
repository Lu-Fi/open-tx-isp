#!/bin/sh
# check_t23_ae_max_route.sh [driver/t23]   (Lu-Fi/openimp#985)
# The T23 MAX_AGAIN / MAX_DGAIN set controls (regtrace_t23_tuning_cid) must reach the lifted stock AE
# (t23_aelift_s_max_gain slot 1 / slot 3 = stock tiziano_ae_s_max_again / _s_max_isp_dgain): the lifted AE
# has its own ae_exp_th and ignores the generic ladder cap, so SetMaxAgain(0) did not cap the gain at 1x.
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
for c in "MAX_AGAIN:regtrace_t23_ae_set_max_again:1U, 0x14U" "MAX_DGAIN:regtrace_t23_ae_set_max_dgain:3U, 0x1cU"; do
	id=${c%%:*}; r=${c#*:}; set_fn=${r%%:*}; args=${r#*:}
	blk=$(awk -v id="case REGTRACE_TISP_CTRL_$id:" 'index($0,id){on=1} on{print} on&&/return ret;/{n++; if(n==1) exit}' "$f")
	echo "$blk" | grep -q "$set_fn(v);" || { echo "check_t23_ae_max_route: $id case without $set_fn"; fail=1; }
	echo "$blk" | grep -q "t23_aelift_s_max_gain($args, v);" || { echo "check_t23_ae_max_route: $id set does not reach the lifted AE (t23_aelift_s_max_gain($args, v))"; fail=1; }
done
[ $fail -eq 0 ] && echo "check_t23_ae_max_route: OK"
exit $fail
