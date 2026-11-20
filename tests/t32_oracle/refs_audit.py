#!/usr/bin/env python3
"""refs_audit.py [--tsv FILE]: recount of the T32 oracle reference functions (openimp #645).

Every t32_*_oracle.conf lists the reference C functions it links in REFS.  Per function name this reports whether any
oracle that links it actually calls it in its .c (comments stripped):
  called       `name(` or `ref_name(` appears in the oracle source: the oracle drives the reference function itself
  named-only   the name appears, but never as a call (function pointer, declaration, table entry)
  callee-only  the name never appears: the function is only linked as a callee of another reference function
This is a source-level count, not the real-body proof of tests/t33_binoracle/proof_audit.py: it does not show that a driver
function with a real body is compared against the reference.  It only bounds the claim "N reference functions proven".
No reference C needed (the conf files are enough); runs in a few seconds."""
import argparse, collections, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def strip(t):
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tsv')
    a = ap.parse_args()
    by = collections.defaultdict(lambda: [0, 0, set()])
    for c in sorted(glob.glob(os.path.join(HERE, 't32_*_oracle.conf'))):
        n = c[:-5]
        if not os.path.exists(n + '.c'):
            continue
        t = open(c).read()
        m = re.search(r'REFS="([^"]*)"', t) or re.search(r"REFS='([^']*)'", t)
        src = strip(open(n + '.c').read())
        for r in (m.group(1).split() if m else []):
            nm = re.sub(r'^fn_[0-9a-f]+_', '', r[:-2] if r.endswith('.c') else r)
            e = by[nm]
            pat = r'\b(?:ref_)?' + re.escape(nm) + r'\b'
            if re.search(pat + r'\s*\(', src):
                e[0] += 1
            elif re.search(pat, src):
                e[1] += 1
            e[2].add(os.path.basename(n))
    st = lambda v: 'called' if v[0] else 'named-only' if v[1] else 'callee-only'
    cnt = collections.Counter(st(v) for v in by.values())
    print('distinct REFS names %d: called %d, named-only %d, callee-only %d'
          % (len(by), cnt['called'], cnt['named-only'], cnt['callee-only']))
    if a.tsv:
        with open(a.tsv, 'w') as f:
            f.write('function\tstatus\toracles\n')
            for k in sorted(by):
                f.write('%s\t%s\t%s\n' % (k, st(by[k]), ','.join(sorted(by[k][2]))))


if __name__ == '__main__':
    sys.exit(main())
