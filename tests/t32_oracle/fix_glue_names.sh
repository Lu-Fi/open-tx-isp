#!/bin/sh
# REFFIX of the glue oracles: env.c provides system_reg_write / system_reg_read /
# system_irq_func_set as logging stubs for the block oracles, so the reference copy
# of the function itself is built under the name ref_<name> (definition line only).
# The normalized system_reg_ldc_write assigns the loaded register base (uint32_t)
# to a void * (an error for current host compilers): the cast is made explicit.
sed -E 's/^(u64 |void \*)(system_reg_write|system_reg_read|system_irq_func_set|system_reg_ldc_write|system_reg_ldc_read)\(/\1ref_\2(/
s/^(\tresult = )(\*\(volatile uint32_t \*\)\(\(char \*\)t32_data_d9944 \+ 0xf4\);)/\1(void *)(uintptr_t)\2/'
