#!/bin/bash
# Host run of the generated T40 vendor AWB: tests/t40_vawb_host.sh <tuning.bin> <oem data.bin> R G B [frames] [trace]
# (oem data.bin = objcopy -O binary --only-section=.data of the OEM tx-isp-t40.ko)
set -e
HERE=$(cd "$(dirname "$0")" && pwd); D=$HERE/../driver/t40; W=$(mktemp -d); trap 'rm -rf $W' EXIT
sed -n '/^#include "tx_isp_t40_vawb_gen.inc"/q;p' $D/tx_isp_t40_vawb.h > $W/top.h
sed -i -e 's/a >= 0x80000000U \&\& a < 0xfffff000U \&\& !(a \& align)/harness_ok(a, align)/' \
       -e 's/tg < 0x80000000U || (tg \& 3)/tg < 0x1000U || (tg \& 3)/' \
       -e "s/vawb_tbuf\[256\]/vawb_tbuf[65536]/" -e 's/^\(static u8 vawb_wide_tele_para\[144\]\)/\1 VD/' -e 's/^\(static u32 vawb_argarea\[16\]\)/\1 VD/' \
       -e 's/^\(static u32 vawb_tawb_custom_en\[2\]\);/\1 VD;/' -e 's/^static u32 vawb_awb_num;/static u32 vawb_awb_num VD;/' \
       -e 's/^static u32 vawb_peter_test_count\[2\];/static u32 vawb_peter_test_count[2] VD;/' \
       -e 's/^static u32 vawb_converge_tele_gain = /static u32 vawb_converge_tele_gain VD = /' \
       -e 's/^static u32 vawb_wide_tele_awb_gain\[3\] = /static u32 vawb_wide_tele_awb_gain[3] VD = /' $W/top.h
echo '#endif' >> $W/top.h
sed -e 's/^static const u8 vawb_rodata\(\[[0-9]*\]\) __aligned(8)/static u8 vawb_rodata\1 __aligned(8) VD/' ${GEN:-$D/tx_isp_t40_vawb_gen.inc} > $W/gen.inc
gcc -O1 -g0 -falign-functions=16 -no-pie -fno-pie -w -fno-strict-aliasing -Wl,--section-start=.vdata=0x30000000 \
	-DVAWB_TOP="\"$W/top.h\"" -DVAWB_INC="\"$W/gen.inc\"" $HERE/t40_vawb/vawb_host.c -o $W/vawb_host
$W/vawb_host "$@"
