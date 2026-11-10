#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched): Matt Davis' recovered
# `extern int tisp_xxx();` (empty prototype) no longer compiles under C23.  Make the
# declaration variadic: the x86-64 call then passes the wrapper's integer and pointer
# arguments in the same registers as before (used for wrappers with 1..4 arguments).
sed -e 's/^extern int \(tisp_[a-z_A-Z0-9]*\)();$/extern int \1(...);/'
