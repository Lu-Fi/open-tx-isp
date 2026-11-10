#!/bin/sh
# Build-time variant of the Tisp_lce_soft_t41_simd reference (ref/ stays untouched): at the first arrival at the
# label named in $T32O_STOP (a block boundary of the port) it copies the local stack to t32o_t41_stack and returns,
# so the memory the vendor has written up to that point (and the vector results it parked on its stack) can be
# compared with the matching port blocks.  Without the label (L_NONE) the function runs to its end.  Stdin -> stdout.
LBL=${T32O_STOP:-L_NONE}
sed -e "s/^\(u64 Tisp_lce_soft_t41_simd\)/extern u32 t32o_t41_stack[236];\n\1/" \
    -e "s/^$LBL:\$/$LBL:\n    __builtin_memcpy(t32o_t41_stack, stack, sizeof(stack));\n    return 0;/"
