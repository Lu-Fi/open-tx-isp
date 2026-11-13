#!/bin/sh
# t32_lceirq_oracle: env.c has a strong empty tisp_lce_interrupt_static (link stub of other oracles); rename the reference copy
sed "s/^u64 tisp_lce_interrupt_static(/u64 t32o_ref_tisp_lce_interrupt_static(/"
