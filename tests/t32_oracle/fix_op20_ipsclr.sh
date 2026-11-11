#!/bin/sh
# Build-time fix of the reference copy (ref/ itself stays untouched): tisp_intp_static_clear
# calls memcpy(dst, NULL, n) (the vendor object relocates these calls to memcpy, not memset).
# The host memcpy model would read address 0, so the reference calls a recorder instead.
sed 's/__asm__("memcpy")/__asm__("op20_ips_memcpy")/'
