#!/bin/sh
# REFFIX for the normalized references: their K&R callee declarations "extern int f();" mean f(void) in C23
# (the host compiler default) and break the build of calls with arguments.  Declare them variadic, "(...)":
# the call passes the same integer arguments in the same registers.  Nothing else changes.
exec sed -E 's/^(extern int [A-Za-z_0-9]+)\(\);$/\1(...);/'
