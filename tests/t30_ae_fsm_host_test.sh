#!/bin/sh
# t30_ae_fsm_host_test.sh [driver/t30]   (Lu-Fi/openimp#283)
# Extracts AE_fsm_switch_state / AE_fsm_process_state / AE_fsm_process_event from the T30 driver,
# links them against recording stubs and drives the AE FSM (init, events 16, 1, 15, 1 in state 1,
# same-state switch).  The expected call sequences are the OEM ones (libt30-firmware
# ae_balanced_fsm.c.o = tx-isp-t30.ko 0x2e824/0x2e910/0x2e98c): state 4 tail-calls
# ae_calculate_target, state 5 tail-calls ae_calculate_exposure, a switch to the current state
# does nothing, leaving states 3..5 raises event 8.
d=${1:-$(dirname "$0")/../driver/t30}
f=$d/tx_isp_t30_recovered.c
[ -f "$f" ] || { echo "usage: $0 [driver/t30]" >&2; exit 2; }
CC=${CC:-cc}
t=$(mktemp -d) || exit 2
trap 'rm -rf "$t"' EXIT
for fn in 'int32_t AE_fsm_switch_state(int32_t *arg1, int32_t arg2)' \
	  'int32_t AE_fsm_process_state(int32_t *arg1)' \
	  'int32_t AE_fsm_process_event(int32_t *arg1, int32_t arg2)'; do
	n=$(grep -cFx -- "$fn" "$f")
	[ "$n" = 1 ] || { echo "t30_ae_fsm: '$fn' found $n times"; exit 1; }
	awk -v p="$fn" '$0 == p {on=1} on {print} on && /^}/ {exit}' "$f" >> "$t/fsm.c"
done
cat > "$t/main.c" <<'EOC'
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static char log_buf[512];
static void rec(const char *s) { strcat(log_buf, s); strcat(log_buf, " "); }
int32_t ae_initialize(int32_t *a) { (void)a; rec("init"); return 0; }
int32_t AE_request_interrupt(int32_t *a, int32_t m) { char b[16]; (void)a; snprintf(b, sizeof(b), "req%d", m); rec(b); return 0; }
void *ae_calculate_target(int32_t *a) { (void)a; rec("target"); return 0; }
int32_t ae_calculate_exposure(uintptr_t a) { (void)a; rec("exposure"); return 0; }
uint32_t apical_isp_raise_event(void *c, int32_t e) { char b[16]; (void)c; snprintf(b, sizeof(b), "raise%d", e); rec(b); return 0; }
int32_t AE_fsm_switch_state(int32_t *arg1, int32_t arg2);
int32_t AE_fsm_process_state(int32_t *arg1);
int32_t AE_fsm_process_event(int32_t *arg1, int32_t arg2);
#include "fsm.c"
static int fail;
static void expect(const char *what, int32_t *ae, int st, const char *calls)
{
	size_t n = strlen(log_buf);
	if (n && log_buf[n - 1] == ' ')
		log_buf[n - 1] = 0;
	if (ae[1] != st || strcmp(log_buf, calls)) {
		printf("FAIL %s: state %d calls '%s' (OEM: state %d calls '%s')\n", what, ae[1], log_buf, st, calls);
		fail = 1;
	}
	log_buf[0] = 0;
}
int main(void)
{
	int32_t ae[16] = { 0x1234, 6 };	/* isp-init: TX_ISP_T30_INIT_FSM(ae, 6) */
	AE_fsm_switch_state(ae, 0); AE_fsm_process_state(ae);
	expect("init 6->0->1", ae, 1, "init");
	AE_fsm_process_event(ae, 16);
	expect("event 16 (1->2)", ae, 2, "req8");
	AE_fsm_process_event(ae, 1);	/* statistics done: 2 -> 4 -> 5 -> 2 */
	expect("event 1 (2->4->5->2)", ae, 2, "target exposure raise8 req8");
	AE_fsm_process_event(ae, 1);
	expect("event 1 again", ae, 2, "target exposure raise8 req8");
	AE_fsm_process_event(ae, 15);
	expect("event 15 (2->1)", ae, 1, "");
	if (AE_fsm_process_event(ae, 1) != 0)
		fail = 1;
	expect("event 1 in state 1 ignored", ae, 1, "");
	AE_fsm_switch_state(ae, 1);
	expect("same-state switch is a no-op", ae, 1, "");
	ae[1] = 2; AE_fsm_switch_state(ae, 5);
	expect("2->5 (no event 8 entering 3..5)", ae, 5, "exposure");
	AE_fsm_switch_state(ae, 4);
	expect("5->4 (stays inside 3..5)", ae, 4, "target");
	AE_fsm_switch_state(ae, 0);
	expect("4->0 leaves 3..5", ae, 0, "raise8 init");
	puts(fail ? "t30_ae_fsm_host_test: FAIL" : "t30_ae_fsm_host_test: PASS");
	return fail;
}
EOC
$CC -O1 -w -Wno-int-conversion -I"$t" -o "$t/fsm_test" "$t/main.c" || { echo "t30_ae_fsm: build failed"; exit 1; }
"$t/fsm_test"
