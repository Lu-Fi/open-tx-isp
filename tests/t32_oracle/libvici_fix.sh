#!/bin/sh
# REFFIX for lib_tisp_vic_intp_handle: env.c's printk and private_complete are strong do-nothing stubs; point the
# reference at the oracle's recording t32o7_printk / t32o7_complete.  Stdin -> stdout.
exec sed -e 's/__asm__("printk")/__asm__("t32o7_printk")/' -e 's/__asm__("private_complete")/__asm__("t32o7_complete")/'
