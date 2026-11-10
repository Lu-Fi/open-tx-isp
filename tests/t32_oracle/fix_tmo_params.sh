#!/bin/sh
# Build-time fix of the adr_params_refresh reference copy (ref/ stays untouched): the recovered
# `extern int tisp_tmo_adr_gamma_refresh();` (empty prototype) no longer compiles under C23 for the one
# argument call; give the declaration the argument.
sed -e 's/^extern int \(tisp_[a-z_0-9]*\)();$/extern int \1(int);/'
