#!/bin/sh
# Build-time fix of the reference copy of table_intp (ref/ stays untouched): its call of
# fix_point_intp goes to the oracle's recorder t32o8_fpi, which runs the reference copy.
sed 's/__asm__("fix_point_intp")/__asm__("t32o8_fpi")/'
