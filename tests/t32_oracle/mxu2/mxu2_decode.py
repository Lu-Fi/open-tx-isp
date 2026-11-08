#!/usr/bin/env python3
"""MXU2 decoder built from the Ingenic XBurst MXU2 Programming Manual tables.

usage: mxu2_decode.py <XBurst_ISA_MXU2_PM.min.md> <module.ko> <objdump> <symbol>...

The manual lists, per instruction, the encoding as a row of fields
("010010 | 10001 | vrt | vrs | vrd | 0001 | fmt"); literal fields give the
mask/match, named fields their position.  The module's MXU2 words show up in
objdump as c2 / udiN / .word.  Output per symbol: the mnemonics used and how
often (a word matching nothing or several entries is reported).
"""
import collections, json, re, subprocess, sys

FIXED = {'vrt': 5, 'vrs': 5, 'vrd': 5, 'vrr': 5, 'base': 5, 'index': 5, 'fs/vrs': 5, 'vrd/fd': 5,
         'rt': 5, 'rs': 5, 'rd': 5, 'fs': 5, 'fd': 5, 'mcsrd': 5, 'mcsrs': 5}


def parse_pm(path):
    L = open(path).read().split('\n')
    ins = []
    for i, l in enumerate(L):
        if not l.startswith('Bit 31 30 29'):
            continue
        toks = [t.strip() for t in L[i + 1].split(' | ')]
        if len(toks) < 5 or not re.fullmatch(r'[01]{6}', toks[0]):
            continue
        m = re.match(r'^[\d.]+ \| (.*)$', L[i - 2])
        j = i + 2
        syn, op = [], []
        while j < len(L) and not L[j].startswith('Syntax'):
            j += 1
        j += 1
        while j < len(L) and not L[j].startswith('Description'):
            syn.append(L[j].split(' | ')[0].strip())
            j += 1
        while j < len(L) and not L[j].startswith('Operation'):
            j += 1
        j += 1
        while j < len(L) and not L[j].startswith('Exceptions'):
            op.append(L[j])
            j += 1
        ins.append(dict(name=m.group(1) if m else L[i - 2], toks=toks, syn=[s for s in syn if s], op=op))
    for x in ins:
        w, unk = [], []
        for t in x['toks']:
            if re.fullmatch(r'[01]+', t):
                w.append(len(t))
            elif t in FIXED:
                w.append(FIXED[t])
            else:
                w.append(None)
                unk.append(t)
        if len(unk) == 1:
            w[w.index(None)] = 32 - sum(v for v in w if v)
        else:
            for k, t in enumerate(x['toks']):
                if w[k] is None and t == 'fmt':
                    last = k == len(x['toks']) - 1
                    w[k] = (1 if len(x['syn']) < 4 else 2) if last else 2
            rest = 32 - sum(v for v in w if v)
            for k in range(len(w)):
                if w[k] is None:
                    w[k] = rest
        assert sum(w) == 32, x
        x['w'] = w
    return ins


def table(ins):
    tab = []
    for x in ins:
        pos, mask, match, fields = 32, 0, 0, {}
        for t, w in zip(x['toks'], x['w']):
            pos -= w
            if re.fullmatch(r'[01]+', t):
                mask |= ((1 << w) - 1) << pos
                match |= int(t, 2) << pos
            else:
                fields[t] = (pos, w)
        tab.append((mask, match, fields, x))
    return tab


def decode(tab, w):
    r = []
    for mask, match, fields, x in tab:
        if w & mask == match:
            name = x['name']
            if 'fmt' in fields:
                p, wd = fields['fmt']
                v = (w >> p) & ((1 << wd) - 1)
                name = x['syn'][v] if len(x['syn']) > v else name + '/fmt%d' % v
            elif x['syn']:
                name = x['syn'][0]
            r.append(name)
    return r


def main():
    pm, ko, objdump, syms = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
    tab = table(parse_pm(pm))
    st = subprocess.check_output([objdump, '-t', ko]).decode()
    dis = subprocess.check_output([objdump, '-d', ko]).decode().split('\n')
    for s in syms:
        m = re.search(r'^([0-9a-f]+) .* F \.text\s+([0-9a-f]+) ' + re.escape(s) + '$', st, re.M)
        a, sz = int(m.group(1), 16), int(m.group(2), 16)
        c, bad = collections.Counter(), 0
        for l in dis:
            p = l.split('\t')
            if len(p) < 3 or not re.match(r'^\s*[0-9a-f]+:$', p[0]):
                continue
            ad = int(p[0].strip().rstrip(':'), 16)
            mn = p[2].split()[0] if p[2].strip() else ''
            if a <= ad < a + sz and (mn == 'c2' or mn.startswith('udi') or mn.startswith('.word')):
                d = decode(tab, int(p[1].strip(), 16))
                if len(d) == 1:
                    c[d[0]] += 1
                else:
                    bad += 1
                    print('%s: %x %s -> %s' % (s, ad, p[1].strip(), d))
        print('%s: %d words, %d distinct, %d undecoded/ambiguous: %s' % (s, sum(c.values()), len(c), bad, ' '.join('%s=%d' % kv for kv in sorted(c.items()))))


if __name__ == '__main__':
    main()
