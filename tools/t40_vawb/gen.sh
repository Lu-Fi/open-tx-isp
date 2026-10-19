#!/bin/bash
# Regenerate driver/t40/tx_isp_t40_vawb_gen.inc (vendor AWB, t40_vawb=1).
#   gen.sh <OEM tx-isp-t40.ko with symbols (Eufy T8416 firmware)> <mipsel-linux-objdump>
# The reference module is not part of this repository.
set -e
KO=$1; OBJDUMP=$2; OBJCOPY=${OBJDUMP%objdump}objcopy
HERE=$(cd "$(dirname "$0")" && pwd); W=$(mktemp -d)
$OBJDUMP -dr "$KO" > $W/sdk.dis
$OBJDUMP -t "$KO" | awk 'NF>=5 && $0 ~ /^[0-9a-f]{8}/ {sec=$(NF-2); if (sec ~ /^\.(bss|data|rodata|sbss)$/) print sec, $1, $(NF-1), $NF}' > $W/secmap.txt
$OBJCOPY -O binary --only-section=.rodata "$KO" $W/rodata.bin
$OBJDUMP -r -j .rodata "$KO" > $W/rodata.rel
OUT=$HERE/../../driver/t40/tx_isp_t40_vawb_gen.inc
VAE_IN=$W VAE_OUT=$W/gen.inc VAE_DATA=$HERE/data.json VAE_TRACE="${VAE_TRACE:-}" \
VAE_ADAPT="system_reg_read div64_u64 __lshrdi3 __ashldi3" python3 -I "$HERE/asm2c.py" \
	tisp_awb_main_process tisp_awb_get_statistics tisp_awb_params_transmit tisp_awb_params_refresh \
	tisp_awb_set_hardware_param tisp_awb_set_gain tisp_awb_dn_params_refresh tisp_awb_main_ev_update \
	tisp_awb_tgain_update
sed -e 's/\bvae_/vawb_/g' $W/gen.inc > $OUT
rm -rf $W
