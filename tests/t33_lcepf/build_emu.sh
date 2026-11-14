#!/bin/bash
# tests/t33_lcepf/build_emu.sh <mipsel-linux-gcc> <out.o> [header-dir] : the T33 LCE / LSF per-frame path as a mipsel relocatable object
set -e
HERE=$(cd "$(dirname "$0")" && pwd); D=${3:-$HERE/../../driver/t32}
$1 -O1 -g0 -march=mips32r2 -msoft-float -fno-pic -mno-abicalls -G0 -mno-shared -mlong-calls -fno-strict-aliasing -fno-builtin -Wall -Werror \
	-Wno-unused-function -I$D $HERE/lcepf_emu.c -c -o $2
