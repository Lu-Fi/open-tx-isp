#!/bin/sh
# Build-time fix of the show / print reference copies (ref/ stays untouched): printk (env.c owns that symbol)
# goes to the print-stream recorder t32show_printk of t32_show_rec.h.
sed -e 's/__asm__("printk")/__asm__("t32show_printk")/'
