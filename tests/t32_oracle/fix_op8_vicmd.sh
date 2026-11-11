#!/bin/sh
# Build-time fix of the reference copy of video_input_cmd_set (ref/ stays untouched): the fixes of
# fix_op8_glue.sh; the inline copy_from_user's byte count left comes back in a2 after __copy_user
# (0x57e4), which the mechanical C cannot express; private_mutex_lock / _unlock and sprintf go to
# the oracle's recorders (env.c has shared strong mutex stubs; libc sprintf is not safe through the
# 64-word call).
sh "$(dirname "$0")/fix_op8_glue.sh" | perl -0pe 's/(L_57e4:\n)    ;/$1    a2 = (u32)result;/' | sed \
	-e 's/__asm__("private_mutex_lock")/__asm__("t32o8_mutex_lock")/' \
	-e 's/__asm__("private_mutex_unlock")/__asm__("t32o8_mutex_unlock")/' \
	-e 's/__asm__("sprintf")/__asm__("t32o8_sprintf")/'
