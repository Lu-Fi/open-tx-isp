#!/bin/sh
# REFFIX for lib_tisp_vic_cfg: env.c's system_reg_write / _read are strong shared stubs; point the reference at the
# oracle's t32o7_wr / t32o7_rd so that writes, reads and prints land in one ordered log.  Stdin -> stdout.
exec sed -e 's/__asm__("system_reg_write")/__asm__("t32o7_wr")/' -e 's/__asm__("system_reg_read")/__asm__("t32o7_rd")/'
