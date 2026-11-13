#!/bin/sh
# REFFIX for lib_tisp_get_isp_memory_space: env.c's t32_data_dc074 is 32 bytes, the vendor object is 528 bytes
# (76 per channel up to dc284).  Point the reference at the oracle's full-size copy t32o7_data_dc074.  Stdin -> stdout.
exec sed 's/__asm__("t32_data_dc074")/__asm__("t32o7_data_dc074")/'
