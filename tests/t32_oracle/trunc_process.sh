#!/bin/sh
# Build-time variant of the process reference (ref/ stays untouched): stops at the label named in $T32O_STOP (a phase
# boundary), copies the registers a0..a3, v0, v1, t0..t9, s0..s7 (in this order) to t32o_pr_reg and sets t32o_pr_stop.
# Without the label the function runs to its end.  Stdin -> stdout.
LBL=${T32O_STOP:-L_NONE}
sed -e "s/^\(u64 tisp_awb_process\)/extern u32 t32o_pr_stop, t32o_pr_reg[28];\n\1/" \
    -e "s/^$LBL:\$/$LBL:\n    { t32o_pr_stop = 1; { u32 r_[28] = { a0, a1, a2, a3, v0, v1, t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, s0, s1, s2, s3, s4, s5, s6, s7, 0, 0, 0, 0 }; __builtin_memcpy(t32o_pr_reg, r_, sizeof(r_)); } return 0; }/"
