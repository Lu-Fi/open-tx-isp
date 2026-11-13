#!/bin/sh
# Build-time fix of the dark_ctr_interp reference copy (ref/ stays untouched): the recovered C takes v1 (the stat
# pointer, live across the leaf calls tmo_interplate / interplate1) from the callee's return high word, which the
# translation leaves 0; the hardware leaf never touches v1.  Keep v1 across the calls.
sed -e 's/ v1 = result >> 32;//'
