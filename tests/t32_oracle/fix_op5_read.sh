#!/bin/sh
# Build-time fix of the isp_tunning_read reference copy (ref/ itself stays untouched), two host
# models of the MIPS kernel ABI the mechanical C cannot express:
# - gp is current_thread_info(); the function reads addr_limit at +24: point gp at the oracle's
#   fake thread_info t32o_op5_ti instead of address 0;
# - __copy_user (asm/uaccess.h __invoke_copy_to_user) returns the bytes left in a2 and keeps v0/v1
#   (not in its clobber list; the caller reuses v1): take a2 from the call result, keep v0/v1.
sed -e 's/gp = (u32)(unsigned long)current_thread_info()/gp = (u32)(unsigned long)t32o_op5_ti/' \
    -e 's/^#include "..\/ref_compat.h"$/&\nextern unsigned char t32o_op5_ti[];/' \
    -e 's/^    v0 = result; v1 = result >> 32;$/    a2 = (u32)result; \/* __copy_user: bytes left in a2, v0\/v1 kept *\//'
