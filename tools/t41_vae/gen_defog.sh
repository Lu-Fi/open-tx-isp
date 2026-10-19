#!/bin/bash
# Regenerate driver/t41/tx_isp_t41_dfg_gen.inc (OEM defog chain, mechanical
# translation like gen.sh).  Same reference module as gen.sh; the .rodata
# image is shared with the vendor-AE translation (vae_rodata).
#   gen_defog.sh <tx-isp-t41.ko with symbols, H20250310a SDK build> <mipsel-linux-objdump>
set -e
KO=$1; OBJDUMP=$2; OBJCOPY=${OBJDUMP%objdump}objcopy
HERE=$(cd "$(dirname "$0")" && pwd); W=$(mktemp -d)
$OBJDUMP -dr "$KO" > $W/sdk.dis
$OBJDUMP -t "$KO" | awk 'NF>=5 && $0 ~ /^[0-9a-f]{8}/ {sec=$(NF-2); if (sec ~ /^\.(bss|data|rodata|sbss)$/) print sec, $1, $(NF-1), $NF}' > $W/secmap.txt
$OBJCOPY -O binary --only-section=.rodata "$KO" $W/rodata.bin
$OBJDUMP -r -j .rodata "$KO" > $W/rodata.rel
# translate only the defog chain; every other OEM callee is an adapter in the driver
VAE_ONLY='^(tisp_defog_|Tiziano_defog|defog_|dfg_|tisp_s_defog)' VAE_NORODATA=1 \
VAE_IN=$W VAE_OUT=$HERE/../../driver/t41/tx_isp_t41_dfg_gen.inc python3 -I "$HERE/asm2c.py" \
	tisp_defog_init tisp_defog_deinit tisp_defog_process tisp_defog_interrupt_static \
	tisp_defog_ev_update tisp_defog_wdr_en tisp_defog_dn_params_refresh \
	tisp_defog_param_array_get tisp_defog_param_array_set tisp_defog_api_set_strength \
	tisp_defog_api_get_strength tisp_defog_strength_itp tisp_s_defog_ratio \
	tisp_defog_pm_get_regsize tisp_defog_pm_suspend tisp_defog_pm_resume
rm -rf $W
