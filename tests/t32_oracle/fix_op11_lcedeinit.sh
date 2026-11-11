#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for t32_lcem_oracle:
# t32_data_d6c66 is the third byte of t32_data_d6c64 (env.c defines d6c64 with the d6c65 / d6c67
# aliases only): the reference addresses it as d6c64 + 2.
sed -e '/__asm__("t32_data_d6c66")/d' -e 's/ref_t32_data_d6c66/(ref_t32_data_d6c64 + 2)/g'
