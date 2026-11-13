#!/bin/sh
# REFFIX of the glue oracles: env.c defines private_mutex_lock / _unlock as plain
# (strong) no-op stubs; the reference copy calls the oracle's recording versions
# t32o_mutex_lock / t32o_mutex_unlock instead (symbol name of the call only).
sed 's/__asm__("private_mutex_lock")/__asm__("t32o_mutex_lock")/; s/__asm__("private_mutex_unlock")/__asm__("t32o_mutex_unlock")/'
