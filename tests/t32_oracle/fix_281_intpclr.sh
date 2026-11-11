#!/bin/sh
# Build-time fix of the reference copy of tisp_intp_static_clear (ref/ stays untouched): its
# memcpy(dst, NULL, len) calls (vendor bug, a read from address 0) go to the oracle's recorder
# t32o281_memcpy, which logs (dst, src, len) and writes nothing (openimp #281).
sed 's/__asm__("memcpy")/__asm__("t32o281_memcpy")/'
