#!/bin/sh
# T32 defog init vs the vendor tisp_defog_init writes (openimp #854); needs the cv8001 PRJ007 day bin:
# $T32_IQ_DIR/cv8001-PRJ007-day.bin (else SKIP).
H=$(cd "$(dirname "$0")" && pwd)
bin=${T32_IQ_DIR:-/mnt/NVMe/git/scratch-builds/ingenic-sdk-motor-params/sensor-iq/t32}/cv8001-PRJ007-day.bin
[ -f "$bin" ] || { echo "t32_defog_trace_host: SKIP (no $bin; set T32_IQ_DIR)"; exit 0; }
o=${TMPDIR:-/tmp}/t32_defog_trace_host.$$
${HOSTCC:-cc} -O1 -Wall -Wextra -Werror -Wno-unused-function -I"$H/../../driver/t32" "$H/t32_defog_trace_host.c" -o "$o" || exit 1
"$o" "$bin" "$H/vendor_cv8001_3840x2160_init.txt"; rc=$?
rm -f "$o"
exit $rc
