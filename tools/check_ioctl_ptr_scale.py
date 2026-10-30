#!/usr/bin/env python3
"""Find ioctl compare constants built with typed-pointer arithmetic in the lifted T40 driver.

The lifter declares MIPS registers as `uintptr_t *` / `uint32_t *` and keeps the
OEM `addiu rD, rS, imm` as C `rD = rS + imm;`.  C scales that by sizeof(*rS), so the
compiled constant is base + imm*sizeof instead of base + imm (issue #145, frame drop:
0xc004542c became 0xc005a4cc).  This tool does a textual constant propagation per
function, prints EFFECTIVE (what gcc compiles) and INTENDED (plain integer add) values
and, with --oem-dis, checks INTENDED against the constants the OEM function builds
(lui/ori/addiu simulation over `objdump -dr` of the vendor tx-isp-t40.ko).

usage: check_ioctl_ptr_scale.py driver/t40/tx_isp_t40_recovered.c
         [--func tx_isp_unlocked_ioctl ...] [--all-ioctl] [--oem-dis oem_ko.dis --oem-func NAME]
"""
import re, sys, argparse

M = 0xffffffff
PTR_SIZE = {"uintptr_t": 4, "uint32_t": 4, "int32_t": 4, "int": 4, "uint16_t": 2, "uint8_t": 1,
            "char": 1, "void": 1, "int64_t": 8, "uint64_t": 8, "long": 4}

def num(s):
    return int(s, 0)

def split_funcs(lines):
    funcs = {}
    i = 0
    while i < len(lines):
        m = re.match(r'^[A-Za-z_][\w \*]*?[ \*](\w+)\(([^;]*)\)\s*$', lines[i])
        if m and i + 1 < len(lines) and lines[i + 1].startswith('{'):
            j = i + 1
            while j < len(lines) and not lines[j].startswith('}'):
                j += 1
            funcs[m.group(1)] = (i + 1, j + 1)
            i = j
        i += 1
    return funcs

FIXES = []

def scan(lines, name, lo, hi, seq=None, lo_n=0x5400, hi_n=0x56ff):
    """pointer-typed `X = Y + N` statements whose N is request-space; returns rows."""
    ptr = {}
    for ln in lines[lo - 1:hi]:
        for m in re.finditer(r'\b(\w+)\s*(\*+)\s*(?:const\s+)?(\w+)\s*(?:=|;|,)', ln):
            if m.group(1) in PTR_SIZE:
                ptr[m.group(3)] = 4 if m.group(2) != '*' else PTR_SIZE[m.group(1)]
    cnt, rows = {}, []
    for n in range(lo, hi + 1):
        s = re.sub(r'/\*.*?\*/', '', lines[n - 1]).strip()
        m = re.match(r'^(?:int\s+)?(\w+)\s*=\s*(?:\(uintptr_t \*\)\(\(uintptr_t\)|\(uintptr_t\))?(\w+)\s*\+\s*(0x[0-9a-fA-F]+|\d+)\)?\s*;', s)
        if not m:
            continue
        x, y, k = m.group(1), m.group(2), num(m.group(3))
        byte = '(uintptr_t)' in s
        if not (lo_n <= k <= hi_n):
            continue
        i = cnt.get(k, 0); cnt[k] = i + 1
        sc = 1 if byte else ptr.get(y, 1)
        oemv = seq.get(k, []) if seq else []
        want = oemv[i] if i < len(oemv) else None
        rows.append((n, f"{x} = {y} + {k:#x}", k, sc, want))
        if sc != 1:
            FIXES.append((n, x, y, m.group(3), ptr.get(x)))
    return rows, cnt

def oem_seq(path, fname):
    """ordered OEM addiu results {imm16: [value,...]} via lui/ori/addiu simulation (function-wide, no CFG)."""
    regs, seq, on = {}, {}, False
    for ln in open(path):
        m = re.match(r'^[0-9a-f]+ <(\w+)>:', ln)
        if m:
            on = (m.group(1) == fname); regs = {}
            continue
        if not on:
            continue
        m = re.match(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(\w+)\s+(.*)$', ln)
        if not m:
            continue
        op, args = m.group(1), m.group(2).split('#')[0].strip().split(',')
        g = lambda r: 0 if r == 'zero' else regs.get(r)
        try:
            if op == 'lui':
                regs[args[0]] = (int(args[1], 0) << 16) & M
            elif op == 'ori' and g(args[1]) is not None:
                regs[args[0]] = g(args[1]) | int(args[2], 0)
            elif op in ('addiu', 'addi'):
                if g(args[1]) is not None:
                    regs[args[0]] = (g(args[1]) + int(args[2], 0)) & M
                    seq.setdefault(int(args[2], 0) & 0xffff, []).append(regs[args[0]])
                else:
                    regs.pop(args[0], None)
            elif op == 'move' and g(args[1]) is not None:
                regs[args[0]] = g(args[1])
        except (ValueError, IndexError):
            pass
    return seq

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('--func', action='append', default=[])
    ap.add_argument('--all-ioctl', action='store_true')
    ap.add_argument('--oem-dis')
    ap.add_argument('--fix', action='store_true', help='rewrite scaled additions as byte-exact (OEM addiu) arithmetic in place')
    a = ap.parse_args()
    lines = open(a.src).read().split('\n')
    funcs = split_funcs(lines)
    names = list(a.func)
    if a.all_ioctl:
        names += [f for f in funcs if 'ioctl' in f and not f.startswith('regtrace_') and f not in names]
    tot = bad = 0
    print("func | line | expression | OEM value | OEM imm | compiled imm | verdict")
    for f in names:
        if f not in funcs:
            print(f"# {f}: not found"); continue
        seq = oem_seq(a.oem_dis, f) if a.oem_dis else None
        rows, cnt = scan(lines, f, *funcs[f], seq=seq)
        for (n, ex, k, sc, want) in rows:
            tot += 1
            ok = sc == 1
            bad += not ok
            wv = f"{want:#010x}" if want is not None else "-"
            print(f"{f} | {n} | {ex} | {wv} | {k:#06x} | {k*sc:#06x} | {'OK' if ok else 'WRONG (x%d)' % sc}")
        if seq is not None:
            for k, v in seq.items():
                if 0x5400 <= k <= 0x56ff and cnt.get(k, 0) != len(v):
                    print(f"# {f}: imm {k:#x}: OEM has {len(v)} addiu, lifted source has {cnt.get(k,0)}")
    if a.fix:
        for (n, x, y, k, xs) in FIXES:
            ln = lines[n - 1]
            cast = f"(uintptr_t *)((uintptr_t){y} + {k})" if xs else f"(uintptr_t){y} + {k}"
            lines[n - 1] = re.sub(r'(\w+) = \w+ \+ (?:0x[0-9a-fA-F]+|\d+);',
                                  lambda m: f"{x} = {cast};  /* byte add (OEM addiu), not typed-pointer scale (#145) */", ln, count=1)
        open(a.src, 'w').write('\n'.join(lines))
    print(f"# checked {tot}, wrong {bad}", file=sys.stderr)

if __name__ == '__main__':
    main()
