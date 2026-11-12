#!/bin/sh
# check_t41_raw_teardown.sh   (openimp #345, review 2026-11-11 F1/F2/F4/F6)
#
# T41 RAW ring (IMP_ISP_RAW_RwControl):
#  - tisp_raw_deinit goes through t41_raw_teardown: rpars unpublished,
#    ring stopped, both completions completed for all, sleepers drained
#    before the vfree,
#  - the RAW get and the disable sleep through the counted t41_raw_wait,
#    the disable with the t41_raw_disable_ms limit (default 1000),
#  - the VIC-done completion is statically initialised (0xc0045432 first),
#  - the enable checks the user buffers against rmem.
# Exit 0 = ok, 1 = violation.
R=$(dirname "$0")/../driver/t41/tx_isp_t41_recovered.c
fail=0
no() { echo "check_t41_raw_teardown: $*"; fail=1; }

awk '/^int32_t tisp_raw_deinit\(void\)/,/^}/' "$R" | grep -q 'return t41_raw_teardown();' \
	|| no "tisp_raw_deinit must call t41_raw_teardown"
awk '/^static int32_t t41_raw_teardown\(void\)$/,/^}/' "$R" | tr -d '\n' | \
	grep -q 'rpars = 0;.*complete_all(.*+ 16));.*complete_all(.*+ 32));.*atomic_read(&t41_raw_waiters).*private_vfree' \
	|| no "teardown order: unpublish, complete_all x2, drain sleepers, then vfree"
[ "$(grep -c 'wait_for_completion_interruptible(T41_RAW_' "$R")" -eq 0 ] || no "uncounted RAW completion wait left"
grep -q 'ret = t41_raw_wait(32, 0);' "$R" || no "RAW get must use t41_raw_wait"
grep -q 'ret = t41_raw_wait(16, READ_ONCE(t41_raw_disable_ms));' "$R" || no "RAW disable must wait with the limit"
grep -q '^static unsigned int t41_raw_disable_ms = 1000;' "$R" || no "t41_raw_disable_ms default 1000"
grep -q '\.comp = COMPLETION_INITIALIZER(globe_wait_attr.comp),' "$R" || no "VIC done completion not statically initialised"
grep -q 'if (words\[0\] == 1 && t41_raw_check_paddr(words))' "$R" || no "RAW enable buffers must be checked against rmem"
[ $fail -eq 0 ] && echo "check_t41_raw_teardown: ok"
exit $fail
