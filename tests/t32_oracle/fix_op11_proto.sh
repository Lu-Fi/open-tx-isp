#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for the op11 oracles:
# - `extern int tisp_xxx();` (empty prototype) no longer compiles under C23: variadic declaration;
# - the tail call `return tisp_xxx(a0 & 255);` keeps a1..a3 in the vendor binary ("jr t9" with only
#   "andi a0,a0,0xff" in the delay slot), the C lost them: pass a1..a3 on.
sed -e 's/^extern int \([a-z_0-9]*\)();$/extern int \1(int, ...);/' \
    -e 's/(a0 & 255);$/(a0 \& 255, a1, a2, a3);/'
