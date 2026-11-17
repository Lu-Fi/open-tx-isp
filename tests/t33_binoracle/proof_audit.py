#!/usr/bin/env python3
"""proof_audit.py [--block B] [--tsv FILE] [--check]: audit of the T33 binary-oracle blocks (openimp #643).

A function counts as proven only when its block compiles and runs a REAL ported body of it (driver/t32 header C, reached
through the bo_ wrapper) against the vendor function.  Per conf (FUNCS first column / PROVES names) the status is:
  ok       the bo_ wrapper (or, for PROVES, a header function) has a real body: logic, not only a constant or an extern call
  stub     body is empty / returns a constant / only touches nothing of its arguments
  mock     the wrapper only forwards to an extern that is also vendor code (callee-only), or holds the logic itself in the .c
  missing  no bo_ definition / no header body found for the name
A STUBS='name ...' line in the conf lists FUNCS/PROVES names that are known not to be real bodies yet: they stay in the oracle
(regression value) but are NOT counted as proven.  --check fails when a stub/mock/missing name is not listed in STUBS, or a
STUBS name is ok (the list must stay exact), or a block runs 0 mutations / loses mutation lines to a stray quote (openimp #99).  No vendor module needed (static source analysis)."""
import argparse, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DRV = os.path.join(HERE, '..', '..', 'driver', 't32')
SPECIAL = ('VEC', 'GLOBAL', 'CALLS', 'NOP')
KW = {'if', 'for', 'while', 'switch', 'return', 'sizeof', 'else', 'do', 'case', 'defined'}
BUILTIN = {'memcpy', 'memset', 'memcmp', 'strlen', 'strcpy', 'strncpy', 'strcmp'}


def strip(t):
    t = re.sub(r'/\*.*?\*/', ' ', t, flags=re.S)
    return re.sub(r'//[^\n]*', '', t)


def funcs_of(text, withdoc=False):
    """name -> body text of every function definition with a body (brace matched); withdoc: name -> (body, comment above)"""
    out = {}
    # blank the comments but keep offsets, so a function's preceding comment can be cut from the raw text
    t = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    t = re.sub(r'//[^\n]*', lambda m: ' ' * len(m.group(0)), t)
    for m in re.finditer(r'(?m)^[A-Za-z_][^;{}()]*?\b([A-Za-z_]\w*)\s*\(([^;{}]*?)\)\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?\{', t):
        i = m.end(); d = 1
        while i < len(t) and d:
            d += (t[i] == '{') - (t[i] == '}'); i += 1
        pre = text[:m.start()].rstrip()
        doc = pre[pre.rfind('/*'):] if pre.endswith('*/') else ''
        out.setdefault(m.group(1), (t[m.end():i - 1], doc) if withdoc else t[m.end():i - 1])
    return out


def calls(body):
    return {c for c in re.findall(r'\b([A-Za-z_]\w*)\s*\(', body) if c not in KW}


def eff_lines(body):
    return len([l for l in body.split('\n') if re.sub(r'[\s{};]', '', l)])


class Tree:
    """header functions visible to one wrapper: the #include "..." closure of the wrapper source in driver/t32"""
    def __init__(self, srctext=''):
        self.hdr = {}; self.doc = {}; self.text = ''; seen = set(); todo = re.findall(r'#\s*include\s+"([^"]+)"', srctext)
        while todo:
            h = todo.pop()
            p = os.path.join(DRV, h)
            if h in seen or not os.path.exists(p):
                continue
            seen.add(h); x = open(p, errors='replace').read(); self.text += x
            todo += re.findall(r'#\s*include\s+"([^"]+)"', x)
            for n, (b, d) in funcs_of(x, True).items():
                self.hdr.setdefault(n, b); self.doc.setdefault(n, d)

    def real(self, body, seen=None):
        """(header-defined functions reached, effective lines of them) through header-defined callees"""
        seen = set() if seen is None else seen
        n = 0
        for c in calls(body):
            if c in self.hdr and c not in seen:
                seen.add(c); n += eff_lines(self.hdr[c]); n += self.real(self.hdr[c], seen)[1]
        return seen, n


def parse_conf(path):
    t = open(path).read()
    g = lambda k: (re.search(r"^%s=(['\"])(.*?)\1" % k, t, re.S | re.M) or [None, None, ''])[2]
    funcs = []
    for ln in g('FUNCS').splitlines():
        w = ln.split()
        if w and not w[0].startswith('#') and w[0] not in SPECIAL and len(w) > 1:
            funcs.append((w[0], w[1]))
    calls_ = set()
    for ln in g('FUNCS').splitlines():
        w = ln.split()
        if w and w[0] == 'CALLS':
            calls_ |= {x.split(':')[0].split('@')[0].split('=')[0] for x in w[1:]}
    src = re.search(r'^SRC=(\S+)', t, re.M)
    run = re.search(r'^RUN=(\S+)', t, re.M)
    return {'funcs': funcs, 'proves': g('PROVES').split(), 'stubs': g('STUBS').split(), 'calls': calls_,
            'src': src.group(1) if src else None, 'run': run.group(1) if run else None}


def audit_block(blk, c):
    rows = []
    if c['src'] is None:        # RUN= / .sh blocks: wrapper source is <blk>.c if present
        cand = os.path.join(HERE, blk + '.c')
        if blk == 'lcepf':
            cand = os.path.join(HERE, '..', 't33_lcepf', 'lcepf_emu.c')
        srctext = open(cand).read() if os.path.exists(cand) else ''
    else:
        srctext = open(os.path.join(HERE, c['src'])).read()
    wrap = funcs_of(srctext)
    tree = Tree(srctext)
    # externs declared in the wrapper = vendor code that is not ours
    ext = set(re.findall(r'(?m)^extern\s+[^;(]*?\b(\w+)\s*\(', strip(srctext)))
    for v, bo in c['funcs']:
        b = wrap.get(bo)
        if b is None:
            rows.append((v, 'missing', 'no definition of %s in %s' % (bo, c['src'] or blk + '.c'))); continue
        if v in ext or v in c['calls']:
            rows.append((v, 'mock', '%s is declared extern / a CALLS entry of its own block: the vendor function runs on both sides' % v)); continue
        cs = {x for x in calls(b) if x not in BUILTIN}
        hs, hn = tree.real(b)
        # wrapper calls a header function of ours: evidence = its name + size
        if hs:
            if hn <= 1 and eff_lines(b) <= 2 and not re.search(r'\b(if|for|while|switch|\?|<<|>>|[-+*/%&|^~])', ' '.join(tree.hdr[h] for h in hs)):
                rows.append((v, 'stub', 'header %s: %d line(s), no operation' % (','.join(sorted(hs)), hn)))
            else:
                rows.append((v, 'ok', 'wrapper %s -> header %s (%d lines)' % (bo, ','.join(sorted(hs)), hn)))
        elif cs and all(x in ext or x in c['calls'] for x in cs):
            rows.append((v, 'mock', 'wrapper only forwards to extern %s (vendor code both sides)' % ','.join(sorted(cs))))
        elif eff_lines(b) >= 1:
            # logic in the wrapper itself (not driver code), or inline use of a header macro/struct
            hm = re.findall(r'\b(t33_\w+|TX_ISP_\w+)\b', b)
            rows.append((v, 'ok' if hm else 'mock', ('wrapper uses %s' % ','.join(sorted(set(hm)))) if hm else 'logic lives in the wrapper %s (%d lines), not in driver/t32' % (bo, eff_lines(b))))
        else:
            rows.append((v, 'stub', 'empty wrapper %s' % bo))
    reach = set()        # header functions the wrapper object compiles in: every wrapper function is a root
    for bo, b in wrap.items():
        reach |= tree.real(b)[0]
    for p in c['proves']:
        if any(p == v for v, _ in c['funcs']):
            continue
        norm = re.sub(r'^(tisp_|func_|isp_)', '', p)
        pat = re.compile(r'\b%s\b' % re.escape(p))
        hits = [n for n in reach | set(tree.hdr) if (n in tree.hdr and (pat.search(tree.doc.get(n, '')) or n in (p, 't33_' + p, 't33_' + norm)))]
        hits = sorted([n for n in hits if n in reach], key=lambda n: len(tree.hdr[n]))
        if p in ext or p in c['calls']:
            rows.append((p, 'mock', 'declared extern/CALLS in the wrapper: the vendor function runs on both sides'))
        elif hits and eff_lines(tree.hdr[hits[0]]) >= 1:
            rows.append((p, 'ok', 'header %s (%d lines) in the wrapper call tree' % (hits[0], eff_lines(tree.hdr[hits[0]]))))
        elif hits:
            rows.append((p, 'stub', 'header %s is empty' % hits[0]))
        else:
            rows.append((p, 'missing', 'no header function of the wrapper call tree documents or is named like %s (callee only: the vendor runs it)' % p))
    return rows


def vendor_sizes():
    """name -> size of the vendor function symbols (T33_OEM_KO + pyelftools), {} without them"""
    ko = os.environ.get('T33_OEM_KO', '')
    try:
        from elftools.elf.elffile import ELFFile
        with open(ko, 'rb') as f:
            e = ELFFile(f); st = e.get_section_by_name('.symtab')
            return {re.sub(r'(\.(part|constprop|isra|cold|clone)\.\d+)+$', '', s.name): s['st_size']
                    for s in st.iter_symbols() if s['st_info']['type'] == 'STT_FUNC'}
    except Exception:
        return {}


def mutation_check(cf):
    """(raw, got, run): MUTATIONS lines written in the conf vs lines the shell really sees after sourcing it.  A stray quote
    (an apostrophe inside a mutation text) ends the MUTATIONS string early: fewer mutations run, in the worst case none (openimp #99, r13E)."""
    import subprocess
    raw = 0; inb = False
    for ln in open(cf, errors='replace').read().split('\n'):
        if not inb and ln.startswith("MUTATIONS='"):
            inb = True; rest = ln[len("MUTATIONS='"):]
            if rest.strip() and rest.strip() != "'":
                raw += 1
            if rest.endswith("'"):
                inb = False
            continue
        if inb:
            if ln == "'":
                inb = False
            elif ln.strip():
                raw += 1
    r = subprocess.run(['sh', '-c', '. "$1"; printf "%s\\n" "$MUTATIONS" | grep -c .; echo "RUN=$RUN"', 'sh', cf], capture_output=True, text=True)
    o = r.stdout.split('\n')
    try:
        got = int(o[0])
    except (ValueError, IndexError):
        got = 0
    return raw, got, (o[1][4:] if len(o) > 1 else '')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--block'); ap.add_argument('--tsv'); ap.add_argument('--check', action='store_true')
    ap.add_argument('--list-real', action='store_true')
    ap.add_argument('--mutation-conf', help='check only the mutation count of this conf (used by the t10 / t31 runners)')
    a = ap.parse_args()
    if a.mutation_conf:
        raw, got, run = mutation_check(a.mutation_conf)
        if got == 0 or raw != got:
            print('proof_audit FAIL: %s writes %d mutation lines, %d reach the shell (0 or a stray quote in MUTATIONS)' % (a.mutation_conf, raw, got)); sys.exit(1)
        sys.exit(0)
    bad = 0; out = []; real = set(); vs = vendor_sizes()
    for cf in sorted(glob.glob(os.path.join(HERE, '*.conf'))):
        blk = os.path.basename(cf)[:-5]
        if a.block and blk != a.block:
            continue
        c = parse_conf(cf)
        if a.check:
            raw, got, run = mutation_check(cf)
            if not run and got == 0:
                print('proof_audit FAIL: block %s has no mutations (a block that runs 0 mutations proves nothing)' % blk); bad += 1
            elif not run and raw != got:
                print('proof_audit FAIL: block %s writes %d mutation lines but only %d reach the shell (stray quote in MUTATIONS?)' % (blk, raw, got)); bad += 1
        for fn, st, ev in audit_block(blk, c):
            if st == 'stub':       # a trivial body only is a stub when the vendor function is not trivial too
                sz = vs.get(fn)
                if sz is None:
                    st, ev = 'ok', ev + '; trivial body, vendor size unchecked (no T33_OEM_KO)'
                elif sz <= 56:
                    st, ev = 'ok', ev + '; vendor function is trivial too (%d bytes)' % sz
                else:
                    ev += '; vendor function is %d bytes' % sz
            out.append((blk, fn, st, ev))
            listed = fn in c['stubs']
            if st == 'ok' and not listed:
                real.add(fn)
            if a.check and ((st not in ('ok',) and not listed) or (st == 'ok' and listed)):
                if st != 'unverified':
                    print('proof_audit FAIL: block %s function %s is %s (%s)%s' % (blk, fn, st, ev, '' if not listed else ' but listed in STUBS'))
                    bad += 1
    if a.tsv:
        with open(a.tsv, 'w') as f:
            f.write('block\tfunction\tstatus\tevidence\n')
            for r in out:
                f.write('\t'.join(r) + '\n')
    if a.list_real:
        print('\n'.join(sorted(real)))
    if a.check:
        print('proof_audit: %d function(s) audited, %d failure(s)' % (len(out), bad))
        sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
