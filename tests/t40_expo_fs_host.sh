#!/bin/sh
# t40_expo_fs_host.sh (openimp #505): driver/t40/tx_isp_t40_expo_fs.h (VIC frame-start accounting, the AE sensor_set_* slot arming,
# the frame-start / frame-done slot walk) against the vendor sensor_set_integration_time, sensor_set_analog_gain,
# ispcore_irq_main_fs_work, ispcore_irq_main_fd_work and isp_vic_interrupt_service_routine under Unicorn
# (tools/ko_emu/diff_expo_fs.py), plus one-token mutations of the header that must be detected.
# Needs T40_OEM_KO and python3 unicorn + pyelftools, else SKIP (a failure with T40_ORACLE_STRICT=1).
H=$(cd "$(dirname "$0")" && pwd); f=$H/../driver/t40/tx_isp_t40_expo_fs.h
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
# host part (no vendor file): builds warning-free and a set + take round trip arms and returns EXPO once
cc -O1 -Wall -Wextra -Werror -DEXPO_FS_H="\"$f\"" -o "$W/h" "$H/t40_expo_fs/harness.c" || { echo "t40_expo_fs_host: FAIL (build)"; exit 1; }
z=$(printf '%0288d' 0); za=$(python3 -c 'b=bytearray(0x130); b[0x124]=1; print(b.hex())')
r=$(printf 'T 0 4d2 %s %s\n' "$z" "$za" | "$W/h" | cut -d' ' -f1)
[ "$r" = 1 ] || { echo "t40_expo_fs_host: FAIL (set_it on a fresh attr: $r)"; exit 1; }
if [ ! -r "${T40_OEM_KO:-}" ] || ! python3 -c "import unicorn, elftools" 2>/dev/null; then echo "t40_expo_fs_host: SKIP (T40_OEM_KO / python3 unicorn+pyelftools missing)"; [ -z "${T40_ORACLE_STRICT:-}" ] || exit 1; exit 0; fi
run() {	# $1 = header, $2 = cases
	cc -O1 -Wall -Werror -DEXPO_FS_H="\"$1\"" -o "$W/m" "$H/t40_expo_fs/harness.c" || return 2
	python3 "$H/../tools/ko_emu/diff_expo_fs.py" "$T40_OEM_KO" "$W/m" --n "$2"
}
run "$f" ${N:-2000} || { echo "t40_expo_fs_host: FAIL"; exit 1; }
nm=0; fail=0; tot=0
while IFS='@' read -r a b; do
	[ -n "$a" ] || continue
	tot=$((tot + 1))
	python3 - "$a" "$b" "$f" "$W/m.h" <<'PY' || { echo "t40_expo_fs_host: mutation text not found: $a"; fail=1; continue; }
import sys
a, b, src, dst = sys.argv[1:5]; t = open(src).read()
if a not in t: sys.exit(1)
open(dst, "w").write(t.replace(a, b, 1))
PY
	if run "$W/m.h" ${NMUT:-400} > /dev/null; then echo "t40_expo_fs_host: mutation NOT detected: $a => $b"; fail=1; else nm=$((nm + 1)); fi
done <<'MUT'
& 0xffff0000U) + it;@& 0xffff0000U) | it;
if (it == T40_EFS_U32(attr, T40_EFS_ATTR_IT))@if (it != T40_EFS_U32(attr, T40_EFS_ATTR_IT))
t40_efs_arm(core, ch, 3U, it);@t40_efs_arm(core, ch, 2U, it);
t40_efs_arm(core, ch, 0U, again);@t40_efs_arm(core, ch, 1U, again);
| (again << 16);@| (again << 15);
& 0xffffU) | (again@& 0xfffffU) | (again
0x2000016, -1, 0x200000a@0x2000016, 0x2000017, 0x200000a
0x2000009, 0x2000005, 0x2000006@0x2000009, 0x2000006, 0x2000005
if (i != 5U &&@if (i != 4U &&
T40_EFS_ATTR_EXPO_FS) != fs))@T40_EFS_ATTR_EXPO_FS) == fs))
#define T40_EFS_ATTR_EXPO_FS 0x124U@#define T40_EFS_ATTR_EXPO_FS 0x128U
(0x300U + (ch) * 0x48U@(0x300U + (ch) * 0x40U
0x2c0U + idx * 0x10U@0x2c0U + idx * 0x14U
b < 4U;@b < 3U;
if (idx == 2U ||@if (idx == 3U ||
0x24cU + idx * 4U@0x24cU + idx * 8U
if (sel == 1U)@if (sel == 2U)
!(pending0 & 0x10U)@!(pending0 & 0x20U)
MUT
[ $fail = 0 ] || exit 1
echo "t40_expo_fs_host: PASS (oracle + $nm/$tot mutations detected)"
