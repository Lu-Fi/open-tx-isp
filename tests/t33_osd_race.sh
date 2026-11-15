#!/bin/sh
# Build the T33 MSCA attribute block race test (openimp #578) for mipsel: t33_osd_race.mips, to run in the
# T33 QEMU guest (or on a T33 camera from /tmp).  Needs T33_CC or T40_CC (a mipsel-linux-gcc), else SKIP.
cd "$(dirname "$0")" || exit 1
CC=${T33_CC:-${T40_CC:-}}
if [ -z "$CC" ]; then
	echo "t33_osd_race_mips: SKIP (T33_CC / T40_CC missing)"
	exit 0
fi
"$CC" -std=gnu99 -O2 -pthread -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -Wno-array-bounds \
	-I t33_osd_race_shim -I ../driver/t32 -I ../driver/include -o t33_osd_race.mips t33_osd_race_guest.c || exit 1
echo "t33_osd_race_mips: built tests/t33_osd_race.mips (run it in the T33 guest: PASS line, rc 0)"
