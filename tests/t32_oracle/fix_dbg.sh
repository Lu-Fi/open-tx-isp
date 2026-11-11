#!/bin/sh
# Build-time fix of the debug-print reference copies (ref/ stays untouched): printk goes to the oracle's recorder
# t32dbg_printk; isp_printf (kernel struct va_format, printk, dump_stack) gets the host declarations; the endless
# loops of check_vic_error / check_csi_error end after the number of passes the oracle asks for (t32dbg_pass_stop).
sed -e 's/__asm__("printk")/__asm__("t32dbg_printk")/' \
    -e 's/printk(/t32dbg_printk(/; s/dump_stack(/t32dbg_dump_stack(/' \
    -e 's/^#include "..\/ref_compat.h"/&\n#include <stdarg.h>\nstruct va_format { const char *fmt; va_list *va; };\nint t32dbg_printk(const char *fmt, ...);\nvoid t32dbg_dump_stack(void);\nint t32dbg_pass_stop(void);/' \
    -e 's/^L_2f48:$/&\n    if (t32dbg_pass_stop()) return 0;/' \
    -e 's/^L_88fc:$/&\n    if (t32dbg_pass_stop()) return 0;/'
