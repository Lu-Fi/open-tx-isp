#!/bin/sh
# REFFIX of t32_txuio_oracle: op3_ren.sh (op3_* symbols) plus the host
# forms of the two atomics the vendor ioctl uses (ll / sc pair of the
# frame-drop / AOV paths): READ_ONCE and cmpxchg.  Function body unchanged.
sh "$(dirname "$0")/op3_ren.sh" | sed '/^#include /a\
#define READ_ONCE(x) (*(volatile u32 *)&(x))\
#define cmpxchg(p, o, n) __sync_val_compare_and_swap((p), (o), (n))'
