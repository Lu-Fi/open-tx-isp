#!/bin/sh
# Build-time fix of the reference copy of video_input_cmd_show (ref/ stays untouched): its sprintf
# goes to the oracle's recorder t32o8_sprintf (the libc one through the 64-word call is not varargs-safe).
sed 's/__asm__("sprintf")/__asm__("t32o8_sprintf")/'
