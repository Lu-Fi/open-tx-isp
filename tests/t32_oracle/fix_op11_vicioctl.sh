#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for t32_vicdev_oracle:
# - gp (current_thread_info, NULL in ref_compat.h) points at a fake thread_info (addr_limit at +24);
# - the MIPS kernel __copy_user returns the bytes not copied in a2: the stub returns them in v1,
#   copied to a2 after the two calls (the mechanical C keeps a2 unchanged).
sed -e 's/(u32)(unsigned long)current_thread_info()/(u32)(unsigned long)t32o_vic_ti/' \
    -e '/^#include/a extern unsigned char t32o_vic_ti[];' \
    -e 's/^\(    \)goto L_2c4c;$/\1a2 = v1; goto L_2c4c;/' \
    -e 's/^\(    \)goto L_2ca4;$/\1a2 = v1; goto L_2ca4;/'
