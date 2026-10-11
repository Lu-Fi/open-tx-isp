#!/usr/bin/env python3
# (allowed.txt: "<function> &<sym>+<offset>" per line - known, guarded stores)
# asmscan.py <file.s> <nm -S of .o> <reachable-list>
# Stores/loads in compiled code that address &SYM + N with N+width > sizeof(SYM).
import re, sys
sz = {}
for l in open(sys.argv[2]):
    x = l.split()
    if len(x) == 4 and x[2] in 'bBdDrR': sz[x[3]] = int(x[1], 16)
reach = set(open(sys.argv[3]).read().split())
W = {'sw': 4, 'sh': 2, 'sb': 1, 'lw': 4, 'lh': 2, 'lhu': 2, 'lb': 1, 'lbu': 1, 'swl': 4, 'swr': 4, 'lwl': 4, 'lwr': 4}
fn = None; out = {}
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'\s+\.ent\s+(\S+)', line)
    if m: fn = m.group(1); continue
    m = re.match(r'\s+(\w+)\s+[^#]*#(.*)$', line)
    if not m or m.group(1) not in W: continue
    # verbose-asm comment lists the operands in insn order; the memory
    # operand of a MIPS load/store is the last one
    mem = m.group(2).split(',')[-1] if m.group(2).count(',') else m.group(2)
    mm = re.search(r'MEM\[\(([^)]*)\)&(\w+) \+ (\d+)B\]', mem)
    if not mm: continue
    sym, off = mm.group(2), int(mm.group(3))
    if sym not in sz or off + W[m.group(1)] <= sz[sym]: continue
    key = (fn, sym, off, m.group(1)[0])
    out[key] = out.get(key, 0) + 1
for (fn, sym, off, rw), n in sorted(out.items()):
    print("%-4s %-11s %-40s &%s+%d (size %d)" % ('W' if rw == 's' else 'R', 'REACH' if fn in reach else 'unreach', fn, sym, off, sz[sym]))
