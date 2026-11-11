#!/bin/sh
# t32_blkirq_oracle: rename the reference tisp_lsf_interrupt_static (env.c has the name tisp_lce_init registers)
sed "s/^u64 tisp_lsf_interrupt_static(/u64 t32o_ref_tisp_lsf_interrupt_static(/"
