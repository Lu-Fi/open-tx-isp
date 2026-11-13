#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for the op2 getter oracles
# (t32_getd_oracle, t32_geth_oracle):
# - `extern int tisp_xxx();` (empty prototype) no longer compiles under C23: variadic declaration;
# - the tail calls `return tisp_xxx(a0 & 255);` keep a1 in the vendor binary ("jr t9; andi a0,a0,0xff",
#   a1 untouched), the C lost it: pass a1 on;
# - tisp_get_system_sensor_num is also an env.c stub: the reference copy is renamed ref_*.
sed -e 's/^extern int \([a-z_0-9]*\)();$/extern int \1(int, ...);/' \
    -e 's/(a0 & 255);$/(a0 \& 255, a1);/' \
    -e 's/\btisp_get_system_sensor_num\b/ref_tisp_get_system_sensor_num/g'
