#!/bin/sh
# REFFIX for lib_tisp_vic_set_par / lib_tisp_vic_save_raw: env.c's printk is a strong counting stub, snprintf would
# be the host libc one (32-bit arguments) and env.c's t32_data_dc074 is 32 bytes (the vendor reads +32).  Point the
# reference at the oracle's recording printk / formatting snprintf and its full-size dc074.  Stdin -> stdout.
exec sed -e 's/__asm__("printk")/__asm__("t32o7_printk")/' -e 's/__asm__("snprintf")/__asm__("t32o7_snprintf")/' \
	-e 's/__asm__("t32_data_dc074")/__asm__("t32o7p_data_dc074")/'
