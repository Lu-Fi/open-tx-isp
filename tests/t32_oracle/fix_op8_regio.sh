#!/bin/sh
# Build-time fix of a reference copy (ref/ stays untouched): its system_reg_read / _write calls go
# to the oracle's recorders t32o8_reg_read / t32o8_reg_write (env.c's are shared strong stubs), so
# that reads and writes are logged in one sequence; printk as in fix_op8_glue.sh.
sh "$(dirname "$0")/fix_op8_glue.sh" | sed -e 's/__asm__("system_reg_read")/__asm__("t32o8_reg_read")/' \
	-e 's/__asm__("system_reg_write")/__asm__("t32o8_reg_write")/'
