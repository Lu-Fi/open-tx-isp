#!/usr/bin/env python3
"""oem_tstp.py <objdump -dr of the OEM tisp_tstp_reg_cfg> : interpret the OEM MIPS code (RE reference only).

Runs the OEM instruction stream of tisp_tstp_reg_cfg (libt40-firmware 0x6cca0, 259 instructions) for
vinum 0 and 1 on the parameter patterns of patterns.h and prints every system_reg_write (jalr s2 and
the final jr.hb t9 tail call) as "vinum pattern reg value".  Little-endian lwl/lwr/lhu/lbu semantics;
main_tstp = .bss+18916, sec_tstp = .bss+18912 (OEM symbol table).  Used to generate oem_expected.inc."""
import re, sys

M32 = 0xffffffff
def patterns():
    # same generator as patterns.h: 154-byte parameter blocks
    out = []
    for p in range(4):
        b = bytearray(154)
        x = (0x1234567 * (p + 1)) & M32
        for i in range(154):
            if p == 0: b[i] = 0
            elif p == 1: b[i] = 0xff
            else:
                x = (x * 1103515245 + 12345) & M32
                b[i] = (x >> 16) & 0xff
        out.append(bytes(b))
    return out

def parse(text):
    ins, relocs = [], {}
    for line in text.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+R_MIPS_(HI16|LO16)\s+(\S+)', line)
        if m:
            relocs[int(m.group(1), 16)] = m.group(3); continue
        m = re.match(r'\s*([0-9a-f]+):\s+[0-9a-f]{8}\s+(\S+)\s*(.*)', line)
        if m:
            ins.append((int(m.group(1), 16), m.group(2), [a.strip() for a in m.group(3).split(',')] if m.group(3) else []))
    return ins, relocs

def run(ins, relocs, vinum, buf_main, buf_sec):
    MAIN, SEC = 0x10000000, 0x20000000
    mem = {MAIN: buf_main + bytes(8), SEC: buf_sec + bytes(8)}
    R = {'zero': 0, 'a0': vinum, 'sp': 0x7fff0000}
    idx = {a: i for i, (a, _, _) in enumerate(ins)}
    writes = []
    def g(r): return R.get(r, 0) & M32
    def s(r, v):
        if r != 'zero': R[r] = v & M32
    def memaddr(arg):
        m = re.match(r'(-?\d+)\((\w+)\)', arg); return int(m.group(1)), m.group(2)
    def word(addr):
        for base, b in mem.items():
            if base <= addr < base + len(b) - 3:
                o = addr - base; return int.from_bytes(b[o:o + 4], 'little')
        raise RuntimeError('load outside the parameter block: %#x' % addr)
    def byte(addr): return word(addr) & 0xff
    def half(addr): return word(addr) & 0xffff
    pc = 0; delay = None; steps = 0
    while True:
        steps += 1
        if steps > 2000: raise RuntimeError('runaway')
        a, op, args = ins[pc]
        nxt = pc + 1; target = None; call = None; done = False
        if op == 'addiu': s(args[0], g(args[1]) + int(args[2], 0))
        elif op == 'lui': s(args[0], int(args[1], 0) << 16)
        elif op == 'move': s(args[0], g(args[1]))
        elif op == 'li': s(args[0], int(args[1], 0))
        elif op == 'sll': s(args[0], g(args[1]) << int(args[2], 0))
        elif op == 'andi': s(args[0], g(args[1]) & int(args[2], 0))
        elif op == 'and': s(args[0], g(args[1]) & g(args[2]))
        elif op == 'or': s(args[0], g(args[1]) | g(args[2]))
        elif op == 'sw': pass                       # register saves to the stack
        elif op == 'lw':
            off, base = memaddr(args[1])
            if base == 'sp': pass                   # register restores
            elif relocs.get(a) == '.bss':
                s(args[0], {18916: MAIN, 18912: SEC}[off])
            else: s(args[0], word(g(base) + off))
        elif op in ('lhu', 'lbu'):
            off, base = memaddr(args[1]); ad = g(base) + off
            s(args[0], half(ad) if op == 'lhu' else byte(ad))
        elif op in ('lwl', 'lwr'):
            off, base = memaddr(args[1]); ad = g(base) + off; k = ad & 3; W = word(ad & ~3)
            if op == 'lwl':
                sh = 8 * (3 - k); s(args[0], ((W << sh) & M32) | (g(args[0]) & ((1 << sh) - 1)))
            else:
                sh = 8 * k; keep = M32 & ~(M32 >> sh); s(args[0], (W >> sh) | (g(args[0]) & keep))
        elif op == 'beqz':
            if g(args[0]) == 0: target = int(args[1].split()[0], 16)
            else: target = -1
        elif op == 'b': target = int(args[0].split()[0], 16)
        elif op == 'jalr': call = args[0]
        elif op == 'jr.hb': call = args[0]; done = True
        elif op == 'jr' and args[0] == 'ra': raise RuntimeError('plain return before the tail call')
        elif op == 'nop': pass
        else: raise RuntimeError('unhandled %s at %#x' % (op, a))
        if target is not None or call is not None:
            pc += 1                                  # delay slot
            a2, op2, args2 = ins[pc]
            # execute the delay slot by re-entering the loop body for one instruction
            saved = (target, call, done)
            ins_one = [(a2, op2, args2)]
            sub_idx = pc
            # minimal inline execution of the delay-slot instruction
            R2 = run_one(ins[pc], relocs, R, word, half, byte, MAIN, SEC)
            target, call, done = saved
            if call is not None:
                writes.append((g('a0'), g('a1')))
            if done: return writes
            if target is not None and target != -1: pc = idx[target]
            else: pc += 1
            continue
        pc = nxt

def run_one(insn, relocs, R, word, half, byte, MAIN, SEC):
    a, op, args = insn
    def g(r): return R.get(r, 0) & M32
    def s(r, v):
        if r != 'zero': R[r] = v & M32
    def memaddr(arg):
        m = re.match(r'(-?\d+)\((\w+)\)', arg); return int(m.group(1)), m.group(2)
    if op == 'addiu': s(args[0], g(args[1]) + int(args[2], 0))
    elif op == 'lui': s(args[0], int(args[1], 0) << 16)
    elif op == 'move': s(args[0], g(args[1]))
    elif op == 'li': s(args[0], int(args[1], 0))
    elif op == 'sll': s(args[0], g(args[1]) << int(args[2], 0))
    elif op == 'andi': s(args[0], g(args[1]) & int(args[2], 0))
    elif op == 'and': s(args[0], g(args[1]) & g(args[2]))
    elif op == 'or': s(args[0], g(args[1]) | g(args[2]))
    elif op in ('sw', 'nop'): pass
    elif op == 'lw':
        off, base = memaddr(args[1])
        if base == 'sp': pass
        elif relocs.get(a) == '.bss': s(args[0], {18916: MAIN, 18912: SEC}[off])
        else: s(args[0], word(g(base) + off))
    elif op in ('lhu', 'lbu'):
        off, base = memaddr(args[1]); ad = g(base) + off
        s(args[0], half(ad) if op == 'lhu' else byte(ad))
    elif op in ('lwl', 'lwr'):
        off, base = memaddr(args[1]); ad = g(base) + off; k = ad & 3; W = word(ad & ~3)
        if op == 'lwl':
            sh = 8 * (3 - k); s(args[0], ((W << sh) & M32) | (g(args[0]) & ((1 << sh) - 1)))
        else:
            sh = 8 * k; keep = M32 & ~(M32 >> sh); s(args[0], (W >> sh) | (g(args[0]) & keep))
    else: raise RuntimeError('unhandled delay-slot %s at %#x' % (op, a))

ins, relocs = parse(open(sys.argv[1]).read())
pats = patterns()
print('/* generated by oem_tstp.py from the OEM tisp_tstp_reg_cfg disassembly: {vinum, pattern, reg, value} */')
n = 0
for v in (0, 1):
    for p in range(len(pats)):
        # main and sec blocks get different patterns so the block selection is checked too
        w = run(ins, relocs, v, pats[p], pats[(p + 1) % len(pats)])
        for reg, val in w:
            print('{ %d, %d, 0x%05x, 0x%08x },' % (v, p, reg, val)); n += 1
print('/* %d writes */' % n)
