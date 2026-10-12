#!/usr/bin/env python3
"""Approximate call-graph reachability for a relocatable kernel object.

reach.py <objdump -t combined.o> <objdump -r combined.o>
Prints every function reachable from a non-text reference (ops tables,
init/exit, ksymtab, module params) or from a local (static) function, which
are treated as roots because their section-relative calls carry no symbol.
"""
import re
import sys

funcs = {}
local = set()
for l in open(sys.argv[1]):
    m = re.match(r'([0-9a-f]{8}) (.{7}) (\S+)\s+([0-9a-f]{8}) (\S+)$', l.rstrip())
    if m and 'F' in m.group(2):
        funcs[m.group(5)] = (m.group(3), int(m.group(1), 16), int(m.group(4), 16))
        if m.group(2).startswith('l'):
            local.add(m.group(5))
bysec = {}
for n, (s, a, z) in funcs.items():
    bysec.setdefault(s, []).append((a, z, n))


def owner(sec, off):
    for a, z, n in bysec.get(sec, []):
        if a <= off < a + z:
            return n
    return None


edges = {}
roots = set(local) | {'init_module', 'cleanup_module'}
sec = None
for l in open(sys.argv[2]):
    m = re.match(r'RELOCATION RECORDS FOR \[(.*)\]', l)
    if m:
        sec = m.group(1)
        continue
    m = re.match(r'([0-9a-f]{8}) (\S+)\s+(\S+)$', l.strip())
    if not m or sec is None:
        continue
    tgt = m.group(3).split('+')[0].split('-')[0]
    if tgt not in funcs:
        continue
    if '.text' in sec:
        src = owner(sec, int(m.group(1), 16))
        if src:
            edges.setdefault(src, set()).add(tgt)
        else:
            roots.add(tgt)
    else:
        roots.add(tgt)
live = set()
todo = list(roots)
while todo:
    f = todo.pop()
    if f in live:
        continue
    live.add(f)
    todo.extend(edges.get(f, ()))
print('\n'.join(sorted(live)))
