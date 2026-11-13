#!/bin/sh
# Build-time fix of the reference copy (ref/ itself stays untouched): Matt Davis' recovered
# tisp_bcsh_adjust_hue is declared with 4 arguments, but the vendor function takes an 88-byte
# structure on the stack (the two 3x3 matrices of the user/csc set, at its sp + 160).  Without
# the 22 extra words the host copy reads a zeroed local array and returns an all-zero hue
# matrix.  Declare the words as arg4..arg25 and store them where the code reads them
# (stack[40 .. 61], i.e. sp + 160 with sp = &stack[0]).
args=""; stores=""
i=4
while [ $i -le 25 ]; do
	args="$args, u32 arg$i"
	stores="$stores stack[$((36 + i))] = arg$i;"
	i=$((i + 1))
done
sed -e "s/^u64 tisp_bcsh_adjust_hue(u32 a0, u32 a1, u32 a2, u32 a3)\$/u64 tisp_bcsh_adjust_hue(u32 a0, u32 a1, u32 a2, u32 a3$args)/" \
    -e "s/^\\(    sp = (u32)(unsigned long)&stack\\[36\\];\\)\$/\\1$stores/"
