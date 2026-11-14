#!/bin/sh
# tests/t33_binoracle/build_emu.sh <mipsel-linux-gcc> <wrapper.c> <out.o> [header-dir]: a block wrapper as a mipsel
# relocatable object for tools/ko_emu/t33_binoracle.py (same flags as tests/t33_lcepf/build_emu.sh; no zero-division
# traps, as the vendor module)
set -e
HERE=$(cd "$(dirname "$0")" && pwd); D=${4:-$HERE/../../driver/t32}
$1 -O1 -g0 -march=mips32r2 -msoft-float -fno-pic -mno-abicalls -G0 -mno-shared -mlong-calls -fno-strict-aliasing \
	-fno-builtin -mno-check-zero-division -Wall -Werror -Wno-unused-function -I$D "$2" -c -o "$3"
