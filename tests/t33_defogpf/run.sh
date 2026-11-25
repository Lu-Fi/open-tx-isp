#!/bin/sh
# T33 per-frame defog (tx_isp_t33_defogpf.h) against the T32 port on a moved section (openimp #878)
H=$(cd "$(dirname "$0")" && pwd)
o=${TMPDIR:-/tmp}/t33_defogpf_host.$$
${HOSTCC:-cc} -O1 -Wall -Wextra -Werror -Wno-unused-function -I"$H/../../driver/t32" "$H/t33_defogpf_host.c" -o "$o" || exit 1
"$o"; rc=$?
rm -f "$o"
exit $rc
