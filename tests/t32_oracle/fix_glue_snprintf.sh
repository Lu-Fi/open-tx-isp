#!/bin/sh
# REFFIX of the glue oracles: the reference calls the kernel's snprintf through the
# 64-argument o32 call shape; it is pointed at the oracle's t32o_snprintf (one
# integer argument, as the vendor uses it) instead of the host libc's variadic one.
sed 's/__asm__("snprintf")/__asm__("t32o_snprintf")/'
