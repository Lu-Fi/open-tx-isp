#!/bin/sh
# check_t33_mem_slots.sh [driver/t32]   (Lu-Fi/openimp#928, SDK 2.1.0 PRJ008 tx_isp_unlocked_ioctl MEMORY_MALLOC / FREE)
# T33 keeps the MEMORY_MALLOC buffers across MEMORY_FREE and the last close (slots with a used flag, 16 at most),
# reuses a slot whose buffer is large enough, frees all at rmmod; T32 still frees at the last close (PRJ007).
d=${1:-$(dirname "$0")/../driver/t32}
c=$d/tx_isp_t32_core.c
[ -f "$c" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
fail=0
body() { awk -v s="$2" 'index($0, s) == 1 {on = 1} on {print} on && /^}/ {exit}' "$1"; }
need() { echo "$1" | grep -q -- "$2" || { echo "check_t33_mem_slots: $3"; fail=1; }; }
no() { echo "$1" | grep -q -- "$2" && { echo "check_t33_mem_slots: $3"; fail=1; }; }
# the T33 block sits between "#ifdef TX_ISP_T33" before t32_malloc and the "#else" before the T32 copy
t33=$(awk '/^#define T33_MEM_SLOTS/ {on = 1} on {print} /^#else/ && on {exit}' "$c")
need "$t33" 'T33_MEM_SLOTS		16' 'slot count is not 16'
need "$t33" 'T33_MEM_MIN_BYTES	153600U' 'minimum buffer is not 153600 bytes (vendor 0x25800)'
need "$t33" 'n >= T33_MEM_SLOTS' 'no -ENOMEM when all slots are in use'
need "$t33" 'slot->cap < bi->size' 'a too small slot is not reallocated'
need "$t33" 'a->used = false;' 'MEMORY_FREE does not clear the used flag'
f=$(echo "$t33" | awk '/^static int t32_free\(/ {on = 1} on {print} on && /^}/ {exit}')
no "$f" 'free_pages' 'T33 MEMORY_FREE frees the buffer (2.1.0 keeps it)'
no "$f" 'list_del' 'T33 MEMORY_FREE drops the slot (2.1.0 keeps it)'
r=$(body "$c" 'static int t32_isp_release(')
need "$r" 't32_release_slots();' 'T33 last close does not release the slots'
echo "$r" | grep -B1 -A3 't32_release_slots();' | grep -q 'ifdef TX_ISP_T33' || { echo "check_t33_mem_slots: last close: release_slots not under TX_ISP_T33"; fail=1; }
echo "$r" | grep -A2 '^#else' | grep -q 't32_free_all();' || { echo "check_t33_mem_slots: T32 last close no longer frees all"; fail=1; }
n=$(grep -c 't32_free_all();' "$c")
[ "$n" -ge 2 ] || { echo "check_t33_mem_slots: rmmod path does not call t32_free_all"; fail=1; }
[ $fail = 0 ] && echo "check_t33_mem_slots: OK"
exit $fail
