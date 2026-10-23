#!/bin/sh
# Apical ISP (T10/T20/T30) built-in video test generator: known pattern in
# front of the whole pipeline (BLC..CSC).  Runs ON the camera (devmem).
#   tpg_apical.sh on  <R> <G> <B> [type]   16-bit Bayer values, type 0=flat
#                                          1=h-gradient 2=v-gradient 3=v-bars
#   tpg_apical.sh off                      restore sensor input
#   tpg_apical.sh show
# Register map (apical_isp_config.h): 0x40 bit0 = top bypass test gen
# (1 = sensor, 0 = generator), 0x9c0 bit0 on/off, bit1 bayer/rgb input sel,
# 0x9c4 pattern type, 0x9c8/9cc/9d0 R/G/B background, 0x9d4.. foreground,
# 0x9e0 gradient, 0x9e8.. rect.  Core base: T20 0x13380000 ("isp_core" in
# /proc/iomem); verify for T10/T30 before use.
B=${ISP_CORE_BASE:-$(awk -F'[ -]' '/isp_core|tx-isp-core/{print "0x"$1; exit}' /proc/iomem)}
B=${B:-0x13380000}
rd(){ devmem $(($B+$1)) 32; }
wr(){ devmem $(($B+$1)) 32 $2; }
case "$1" in
 on)  wr 0x9c8 ${2:-0x8000}; wr 0x9cc ${3:-0x8000}; wr 0x9d0 ${4:-0x8000}; wr 0x9c4 ${5:-0};
      wr 0x9c0 1; wr 0x40 $(( $(rd 0x40) & ~1 )) ;;
 off) wr 0x40 $(( $(rd 0x40) | 1 )); wr 0x9c0 0 ;;
 *)   ;;
esac
echo "base=$B top40=$(rd 0x40) tpg9c0=$(rd 0x9c0) type=$(rd 0x9c4) rgb=$(rd 0x9c8)/$(rd 0x9cc)/$(rd 0x9d0)"
