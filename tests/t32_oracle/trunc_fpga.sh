#!/bin/sh
# Build-time variant of the tisp_tmo_adr_fpga reference (ref/ stays untouched): at the first arrival at the label named in $T32O_STOP (a sub-block
# boundary of the port: L_9afd4 end of block 1, L_9b1b8 end of block 2, L_9b4a4 end of block 3 (reached from several paths), L_9bb08 end of
# block 4, L_9bda8 start of the faceae tail) the function returns, so that the memory written up to that point can be compared with the matching
# port blocks.  Without the label (L_NONE) it runs to its end.  Stdin -> stdout.
LBL=${T32O_STOP:-L_NONE}
sed -e "s/^$LBL:\$/$LBL:\n    return 0;/"
