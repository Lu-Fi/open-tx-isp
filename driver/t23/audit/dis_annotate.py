#!/usr/bin/env python3
"""Annotate objdump -dr output of a relocatable MIPS .ko: resolve HI16/LO16
pairs (incl. section-relative .text/.data/.bss with addend) to symbol names.
usage: objdump -dr tx-isp-t23.ko > stock.dis; nm -n tx-isp-t23.ko > stock.nm
       dis_annotate.py stock.dis stock.nm > stock.ann"""
import sys, re, bisect
dis, nm = sys.argv[1:3]
secsyms = {}
for l in open(nm):
    p = l.split()
    if len(p) == 3:
        t = p[1].lower()
        sec = {'t': '.text', 'd': '.data', 'b': '.bss', 'r': '.rodata'}.get(t)
        if sec: secsyms.setdefault(sec, []).append((int(p[0], 16), p[2]))
for s in secsyms.values(): s.sort()
def name(sec, a):
    L = secsyms.get(sec)
    if not L: return '%s+0x%x' % (sec, a)
    i = bisect.bisect_right([x[0] for x in L], a) - 1
    if i < 0: return '%s+0x%x' % (sec, a)
    b, n = L[i]
    return n if a == b else '%s+0x%x' % (n, a - b)
lines = open(dis).read().split('\n')
pend = {}  # reg -> (sym, hi)
ins_re = re.compile(r'^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+(\S+)\s*(.*)$')
rel_re = re.compile(r'^\s*([0-9a-f]+): (R_MIPS_\w+)\s+(\S+)$')
last = None
out = []
for l in lines:
    m = rel_re.match(l)
    if m and last is not None:
        typ, sym = m.group(2), m.group(3)
        ins = int(last[1], 16); op = ins >> 26; rt = (ins >> 16) & 31; rs = (ins >> 21) & 31
        imm = ins & 0xffff
        if typ == 'R_MIPS_HI16':
            pend[rt] = (sym, imm)
            out[-1] += '    ; %hi(' + sym + ')'
        elif typ == 'R_MIPS_LO16':
            lo = imm - 0x10000 if imm & 0x8000 else imm
            hi = pend.get(rs, (sym, 0))[1]
            if sym.startswith('.'):
                out[-1] += '    ; = ' + name(sym, ((hi << 16) + lo) & 0xffffffff)
            else:
                out[-1] += '    ; = ' + sym + ('+%d' % lo if lo else '')
        else:
            out[-1] += '    ; ' + typ + ' ' + sym
        continue
    m = ins_re.match(l)
    if m:
        last = (m.group(1), m.group(2))
    out.append(l)
print('\n'.join(out))
