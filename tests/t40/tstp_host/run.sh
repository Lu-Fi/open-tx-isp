#!/bin/bash
# T40 tisp_tstp_reg_cfg translation vs the OEM register writes (tests/t40/tstp_host).
# With the OEM firmware object present (T40_OEM_FW or the default SDK path) oem_expected.inc is
# regenerated from its disassembly first and must be unchanged.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
W=$(mktemp -d); trap 'rm -rf $W' EXIT
FW=${T40_OEM_FW:-/mnt/NVMe/git/ingenic-sdk-tg/sdk/ISVP-T40-1.3.1-20240803/ISVP-T40-1.3.1-20240803/sdk/Ingenic-SDK-T40-1.3.1-20240803-en/opensource/drivers/drivers/isp-t40/tx-isp-t40/libt40-firmware.a}
OD=${MIPS_OBJDUMP:-$(command -v mipsel-linux-objdump || command -v mips-linux-gnu-objdump || true)}
if [ -r "$FW" ] && [ -n "$OD" ]; then
    "$OD" -dr "$FW" --disassemble=tisp_tstp_reg_cfg > $W/oem.dis 2>/dev/null
    python3 "$HERE/oem_tstp.py" $W/oem.dis > $W/oem_expected.inc
    cmp -s $W/oem_expected.inc "$HERE/oem_expected.inc" || { echo "t40_tstp_host: FAIL oem_expected.inc differs from the OEM disassembly"; exit 1; }
    echo "t40_tstp_host: oem_expected.inc matches the OEM disassembly"
fi
${CC:-cc} -std=c99 -Wall -Wextra -Werror -O2 "$HERE/tstp_host.c" -o $W/tstp_host
$W/tstp_host
