#!/bin/sh
# Build-time fix of the reference copy of tisp_write_proc (ref/ stays untouched): the fixes of
# fix_op8_glue.sh, and the inline copy_from_user's "move s0,a2" after __copy_user (0x1ea20):
# the bytes not copied come back in a2, which the mechanical C cannot express.
sh "$(dirname "$0")/fix_op8_glue.sh" | perl -0pe 's/    s0 = a2; goto L_1ea38;/    s0 = (u32)result; goto L_1ea38;/'
