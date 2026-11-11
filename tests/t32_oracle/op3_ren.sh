#!/bin/sh
# REFFIX of the op3 (tx_* glue) oracles: every external symbol NAME of a
# reference copy (Matt Davis' recovered C) is linked as op3_NAME, so the
# oracle supplies its own recording stubs and data objects (t32_op3_glue.h)
# and the shared env.c stays untouched.  Function bodies are unchanged.
sed 's/__asm__("\([A-Za-z_][A-Za-z0-9_]*\)")/__asm__("op3_\1")/g'
