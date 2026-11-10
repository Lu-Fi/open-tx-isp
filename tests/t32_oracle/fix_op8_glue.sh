#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for the op8 glue oracles.
# printk is a counting stub in the shared env.c: the glue oracles record its arguments in
# their own t32o8_printk, so the call is pointed there.
# The ll/sc loops become READ_ONCE / cmpxchg in the mechanical C: give them host definitions.
# For the vendor read handlers that inline copy_to_user (camera_mode / vpuBs / ivdcbuf /
# ispbuf_fops_read):
# Matt Davis' mechanical C cannot express two things of that inline code (checked against
# the disassembly of the vendor module, e.g. 0x168b0..0x168bc):
#  - gp is current_thread_info(); access_ok reads its addr_limit at +24: point gp at the
#    oracle's fake thread_info t32o8_ti instead of 0;
#  - __copy_user returns the bytes NOT copied in a2 ("move v0,a2" after the jalr); the C
#    keeps the caller's a2.  The oracle's stub returns that count in v0: take it from the
#    call result.
perl -0pe '
s/gp = \(u32\)\(unsigned long\)current_thread_info\(\)/gp = (u32)(unsigned long)t32o8_ti/;
s/^(#include "..\/ref_compat.h"\n)/$1extern unsigned char t32o8_ti[];\n#define READ_ONCE(x) (x)\n#define cmpxchg(p, o, n) __sync_val_compare_and_swap((p), (o), (n))\n/m;
s/    v0 = a2;\n/    v0 = (u32)result;\n/ if /__copy_user/;
s/__asm__\("printk"\)/__asm__("t32o8_printk")/g;
'
