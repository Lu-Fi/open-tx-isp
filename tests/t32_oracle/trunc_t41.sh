#!/bin/sh
# Build-time variant of the Tisp_lce_soft_t41_simd reference (ref/ stays untouched): returns at the first
# arrival at the label named in $T32O_STOP (a block boundary of the port), so the memory the vendor has
# written up to that point can be compared with the matching port blocks.  Without the label (L_NONE)
# the function runs to its end.  Stdin -> stdout.
LBL=${T32O_STOP:-L_NONE}
sed -e "s/^$LBL:\$/$LBL:\n    return 0;/"
