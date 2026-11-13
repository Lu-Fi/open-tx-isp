#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for the private_* wrapper oracles:
# Matt Davis' normalized wrappers are kernel C; point their include at the host kernel model
# t32o_kern.h and give the wrapper the prefix ref_ so that it does not clash with env.c's stubs.
sed -e "s|#include \"../ref_compat.h\"|#include \"$(cd "$(dirname "$0")" && pwd)/t32o_kern.h\"|" \
    -e 's/\bprivate_\([a-z0-9_]*\)(/ref_private_\1(/g'
