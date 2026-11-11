#!/bin/sh
# Build-time fix of the reference copy (ref/ itself stays untouched): tisp_s_wdr_en's callees (system and
# tisp register access, memcpy, isp_printf, tisp_lut_params_copy and the nine block *_wdr_en) are renamed
# to the oracle's recorders op20_w_<name> (isp_printf with its stack arguments, passed as arguments 5..).
sed -E 's/__asm__\("(system_reg_read|system_reg_write|tisp_reg_read|tisp_reg_write|memcpy|isp_printf|tisp_lut_params_copy|tisp_[a-z]+_wdr_en)"\)/__asm__("op20_w_\1")/'
