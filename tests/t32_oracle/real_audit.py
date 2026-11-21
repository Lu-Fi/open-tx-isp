#!/usr/bin/env python3
"""real_audit.py [--tsv FILE] [--stubs FILE]: real-body proof audit of the T32 oracle references (openimp #645).

refs_audit.py counts which REFS names an oracle calls; this goes one step further, like tests/t33_binoracle/proof_audit.py:
a reference function counts as PROVEN only when an oracle that calls it also drives a driver/t32 function with a REAL body
(header C reached from the oracle's #include closure; effective lines through header callees >= MIN_LINES, default 4).
Per distinct REFS name:
  proven       called by an oracle whose driver side has a real body
  weak         called, but every such oracle's driver side is below MIN_LINES (wrapper / store / constant)
  named-only   named but never called in an oracle (pointer, declaration)
  callee-only  only linked as a callee of another reference function (compared through its caller's output)
Second part (--stubs): driver/t32 glue entry points `t32_tx_os_*` (the vendor tisp_* / tx_isp_* API) whose body is a stub:
  const        returns a constant / nothing and touches no argument
  store-only   only clamps and stores (assignments, memcpy, min/max), no call into ISP code and no register write
like the #686 SetFaceAe and #711 SetSharpness stubs.  Static source analysis only (no vendor module, no reference C)."""
import argparse, collections, glob, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DRV = os.path.join(HERE, '..', '..', 'driver', 't32')
sys.path.insert(0, os.path.join(HERE, '..', 't33_binoracle'))
from proof_audit import funcs_of, calls, eff_lines, strip  # noqa: E402

MIN_LINES = int(os.environ.get('MIN_LINES', 4))
# helpers that do not make a body "real" (clamp / copy / trace)
TRIVIAL = {'min', 'max', 'min_t', 'max_t', 'clamp', 'clamp_t', 'clamp_val', 'memcpy', 'memset', 'pr_info', 'pr_err', 'pr_debug',
           'pr_warn', 'printk', 'ISP_INFO', 'ISP_ERROR', 'ISP_WARNING', 'spin_lock_irqsave',
           'spin_unlock_irqrestore', 'mutex_lock', 'mutex_unlock', 'WARN_ON', 'BUG_ON', 'unlikely', 'likely'}


T32_UNDEF = {'TX_ISP_T33', 'T33_ATTRAWB_STATIC', 'T32_DBG_PRINTK'}


def t32_build(text):
    """the T32 build of a driver file: drops the TX_ISP_T33 branches (#ifdef / #ifndef / #if defined(), #else, #elif,
    nested); other conditions are kept as true.  Removed lines are blanked so offsets stay."""
    out = []; stack = []          # entries: (this branch active, some earlier branch taken, parent active)
    active = True
    for ln in text.split('\n'):
        m = re.match(r'\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)', ln)
        if m:
            k, arg = m.group(1), m.group(2)
            def ev(expr):
                names = re.findall(r'defined\s*\(?\s*(\w+)', expr)
                if not names or not any(n in T32_UNDEF for n in names):
                    return True
                v = not any(n in T32_UNDEF for n in names)
                return (not v) if expr.strip().startswith('!') else v
            if k in ('ifdef', 'ifndef', 'if'):
                if k == 'ifdef':
                    c = arg.split()[0] not in T32_UNDEF if arg.split() else True
                elif k == 'ifndef':
                    c = True      # T32_UNDEF names are not defined; any other name (include guards) is taken as unset
                else:
                    c = ev(arg)
                stack.append((c, c, active)); active = active and c
            elif k == 'elif' and stack:
                cur, taken, par = stack.pop(); c = (not taken) and ev(arg)
                stack.append((c, taken or c, par)); active = par and c
            elif k == 'else' and stack:
                cur, taken, par = stack.pop(); c = not taken
                stack.append((c, True, par)); active = par and c
            elif k == 'endif' and stack:
                cur, taken, par = stack.pop(); active = par
            out.append('')
            continue
        out.append(ln if active else '')
    return '\n'.join(out)


class Drv:
    """every function body in driver/t32 (*.c and *.h): name -> body"""
    def __init__(self):
        self.f = {}; self.where = {}
        for p in sorted(glob.glob(os.path.join(DRV, '*.c')) + glob.glob(os.path.join(DRV, '*.h'))):
            for n, b in funcs_of(t32_build(open(p, errors='replace').read())).items():
                if n not in self.f:
                    self.f[n] = b; self.where[n] = os.path.basename(p)

    def reach(self, body, seen=None):
        seen = set() if seen is None else seen
        n = 0
        for c in calls(body):
            if c in self.f and c not in seen:
                seen.add(c); n += eff_lines(self.f[c]); n += self.reach(self.f[c], seen)[1]
        return seen, n


def includes(src):
    return set(re.findall(r'#\s*include\s+"([^"]+)"', src))


def oracle_driver_side(drv, src, hdrs):
    """driver functions the oracle calls that are defined in its header closure, and their real lines"""
    names = set()
    todo = [h for h in hdrs if os.path.exists(os.path.join(DRV, h))]; seen = set()
    while todo:
        h = todo.pop()
        if h in seen:
            continue
        seen.add(h); x = t32_build(open(os.path.join(DRV, h), errors='replace').read())
        names |= set(funcs_of(x)); todo += [i for i in includes(x) if os.path.exists(os.path.join(DRV, i))]
    called = calls(strip(src)) & names
    lines = 0
    for c in called:
        lines += eff_lines(drv.f.get(c, '')) + drv.reach(drv.f.get(c, ''))[1]
    return called, lines


def stub_kind(body):
    b = strip(body)
    real_calls = {c for c in calls(b) if c not in TRIVIAL}
    regw = re.search(r'\b(?:t32_reg_write|system_reg_write|tisp_reg_write|writel|iowrite32|t32_wr(?:8|16|32))\b', b)
    if real_calls or regw:
        return None
    if eff_lines(b) <= 2 and not re.search(r'=(?!=)', b):
        return 'const'
    return 'store-only'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tsv'); ap.add_argument('--stubs')
    a = ap.parse_args()
    drv = Drv()
    by = collections.defaultdict(lambda: {'real': set(), 'weak': set(), 'named': set(), 'linked': set()})
    noracle = 0
    for c in sorted(glob.glob(os.path.join(HERE, 't32_*_oracle.conf'))):
        n = c[:-5]
        if not os.path.exists(n + '.c'):
            continue
        noracle += 1
        t = open(c).read(); srcraw = open(n + '.c').read(); src = strip(srcraw)
        m = re.search(r'REFS="([^"]*)"', t) or re.search(r"REFS='([^']*)'", t)
        hm = re.search(r'^HDR=["\']?([^"\'\n]*)', t, re.M)
        hdrs = set((hm.group(1).split() if hm else [])) | includes(srcraw)
        called, lines = oracle_driver_side(drv, srcraw, hdrs)
        real = lines >= MIN_LINES
        o = os.path.basename(n)
        for r in (m.group(1).split() if m else []):
            nm = re.sub(r'^fn_[0-9a-f]+_', '', r[:-2] if r.endswith('.c') else r)
            pat = r'\b(?:ref_)?' + re.escape(nm) + r'\b'
            e = by[nm]; e['linked'].add(o)
            if re.search(pat + r'\s*\(', src):
                e['real' if real else 'weak'].add(o)
            elif re.search(pat, src):
                e['named'].add(o)
    st = lambda e: 'proven' if e['real'] else 'weak' if e['weak'] else 'named-only' if e['named'] else 'callee-only'
    cnt = collections.Counter(st(e) for e in by.values())
    print('T32 oracles %d, distinct REFS names %d: proven %d, weak %d, named-only %d, callee-only %d (MIN_LINES %d)'
          % (noracle, len(by), cnt['proven'], cnt['weak'], cnt['named-only'], cnt['callee-only'], MIN_LINES))
    if a.tsv:
        with open(a.tsv, 'w') as f:
            f.write('function\tstatus\tproving_oracles\tlinked_in\n')
            for k in sorted(by):
                e = by[k]
                f.write('%s\t%s\t%s\t%s\n' % (k, st(e), ','.join(sorted(e['real'])), ','.join(sorted(e['linked']))))
    stubs = []
    for n in sorted(drv.f):
        if not n.startswith('t32_tx_os_'):
            continue
        k = stub_kind(drv.f[n])
        if k:
            vend = n[len('t32_tx_os_'):]
            stubs.append((n, k, drv.where[n], st(by[vend]) if vend in by else 'not-in-REFS'))
    print('t32_tx_os_* glue entry points %d, stub bodies %d (const %d, store-only %d)' % (
        sum(1 for n in drv.f if n.startswith('t32_tx_os_')), len(stubs),
        sum(1 for s in stubs if s[1] == 'const'), sum(1 for s in stubs if s[1] == 'store-only')))
    if a.stubs:
        with open(a.stubs, 'w') as f:
            f.write('function\tkind\tfile\tvendor_name_in_REFS\n')
            for s in stubs:
                f.write('\t'.join(s) + '\n')


if __name__ == '__main__':
    sys.exit(main())
