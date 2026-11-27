#!/bin/sh
# check_t32_clk_defaults.sh [driver/t32]   (Lu-Fi/openimp#928, SDK 2.1.0 tx-isp-funcs.c:71, :92-106)
# Module clock defaults like the vendor 2.1.0: ISP clock parent sclka, T33 isp_clk 350 MHz / isp_clka 525 MHz,
# T32 isp_clk 200 MHz / isp_clka 500 MHz.
d=${1:-$(dirname "$0")/../driver/t32}
soc=$d/tx_isp_t32_soc.h core=$d/tx_isp_t32_core.c
[ -f "$soc" ] && [ -f "$core" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
fail=0
chk() { grep -q -- "$2" "$1" || { echo "check_t32_clk_defaults: $3"; fail=1; }; }
chk "$soc" '^#define T32_CLK_ISP		350000000UL' 'T33 isp_clk is not 350 MHz'
chk "$soc" '^#define T32_CLK_ISPA		525000000UL' 'T33 isp_clka is not 525 MHz'
chk "$soc" '^#define T32_CLK_ISP		200000000UL' 'T32 isp_clk is not 200 MHz'
chk "$soc" '^#define T32_CLK_ISPA		500000000UL' 'T32 isp_clka is not 500 MHz'
chk "$core" '{ "div_isp", "sclka", &clk_name }' 'T33 div_isp parent is not sclka'
chk "$core" '{ "mux_isp", "sclka", &clk_name }' 'T32 mux_isp parent is not sclka'
[ "$(grep -c '"div_ispa", 500000000, T32_CLK_ISPA' "$core")" = 2 ] || { echo "check_t32_clk_defaults: div_ispa activate rate is not T32_CLK_ISPA in both tables"; fail=1; }
[ $fail = 0 ] && echo "check_t32_clk_defaults: OK"
exit $fail
