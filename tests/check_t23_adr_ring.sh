#!/bin/sh
# check_t23_adr_ring.sh [driver/t23]   (Lu-Fi/openimp#987)
# The T23 ADR statistics ring control is 0x4490 (stock 1.3.0 tisp_init, tx-isp-t23.ko .text 0x176dc:
# system_reg_write(0x4490, 3) after the bank addresses 0x4494..0x44a0). Stock never writes 0x44a4; writing the
# value there left 0x4490 = 0 and the ADR DMA on bank 0 (lifted stock ADR on a buffer the hardware rewrites).
d=${1:-$(dirname "$0")/../driver/t23}
f=$d/tx_isp_t23_core.c
[ -f "$f" ] || { echo "usage: $0 [driver/t23]" >&2; exit 2; }
fail=0
blk=$(awk '/^static int regtrace_t23_program_core_dma\(void\)/{on=1} on{print} on&&/^}/{exit}' "$f")
[ -n "$blk" ] || { echo "check_t23_adr_ring: regtrace_t23_program_core_dma not found"; exit 1; }
echo "$blk" | grep -q 'system_reg_write(0x4490U, 3U);' || { echo "check_t23_adr_ring: ADR ring control 0x4490 = 3 missing"; fail=1; }
grep -q 'system_reg_write(0x44a4U' "$f" && { echo "check_t23_adr_ring: 0x44a4 is written (stock never writes it)"; fail=1; }
[ $fail -eq 0 ] && echo "check_t23_adr_ring: OK"
exit $fail
