#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for t32_ctio_oracle:
# - gp (current_thread_info, NULL in ref_compat.h) points at a fake thread_info (addr_limit at +24);
# - the MIPS kernel __copy_user returns the bytes not copied in a2: the stub returns them in v1,
#   copied to a2 after each of the eight calls (the mechanical C keeps a2 unchanged).
sed -e 's/(u32)(unsigned long)current_thread_info()/(u32)(unsigned long)t32o_cti_ti/' \
    -e '/^#include/a extern unsigned char t32o_cti_ti[];' \
    -e 's/^\(    \)goto L_\(1f1d8\|1f5b8\|1f8a4\|1f8f4\|1f964\|1f9dc\|1fa18\|1fa80\);$/\1a2 = v1; goto L_\2;/'
