#!/bin/sh
# Build-time variant of the ct_detect_simd_3 reference (ref/ stays untouched): stops at the label
# named in $T32O_STOP (a phase boundary), copies the whole local stack to t32o_ct_stack and sets
# t32o_ct_stop.  Without the label the function runs to its end.  Stdin -> stdout.
LBL=${T32O_STOP:-L_20ff4}
sed -e "s/^\(u64 tisp_awb_ct_detect_simd_3\)/extern u32 t32o_ct_stop, t32o_ct_stack[208];\n\1/" \
    -e "s/^$LBL:\$/$LBL:\n    { t32o_ct_stop = 1; __builtin_memcpy(t32o_ct_stack, stack, sizeof(stack)); return 0; }/"
