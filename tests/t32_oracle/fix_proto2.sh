#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched): Matt Davis' recovered
# `extern int tisp_xxx();` (empty prototype, a tail call with two arguments) no longer
# compiles under C23.  Give the declaration the two arguments the wrapper passes.
sed -e 's/^extern int \(tisp_[a-z_0-9]*\)();$/extern int \1(int, int);/'
