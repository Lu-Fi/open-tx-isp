#!/usr/bin/env python3
"""t33_binoracle.py OEM.ko OUR.o SPEC [--n 2000] [--seed 1] [--only NAME]

Generic binary oracle for single functions of the vendor tx-isp-t33.ko (openimp #99): the vendor function and our
C (driver/t32/tx_isp_t33_*.h, wrapped with the vendor's calling convention and built for mipsel into OUR.o by
tests/t33_binoracle/run.sh) run in Unicorn (tools/ko_emu/ko_emu.py) on the same inputs; compared are the return
value, every pointer argument's buffer after the call and the listed globals (vendor symbol / our symbol).
Calls to undefined symbols (kernel imports) use the ko_emu HLE helpers; any other import stops the run and counts
as a failure, so a function that needs more of the kernel says so instead of passing by accident.

SPEC: one function per line (from the block's .conf, FUNCS=):
    VENDOR OURS RET ARG ...
  RET:  v0 | u8 | u16 | s16 | v0v1 | none          (compared part of $v0 / $v0:$v1)
  ARG:  u32                     random word, edge values first (0, 1, 2^31-1, 2^31, 2^32-1, ...)
        r:LO:HI                 uniform integer in [LO, HI] (signed range allowed), edges LO / HI / 0
        sq:MAX                  n * n + n + d (d = -1, 0, 1) up to MAX: the rounding edges of square roots
        u64 | u64:BITS          64-bit value in an even register pair (o32), up to BITS bits
        buf:N                   pointer to N random bytes, compared after the call
        st:N:OFF=V1,V2:OFF=V..  like buf:N, but the LE u32 at each byte OFF is one of the listed values (struct flags,
                                sizes, modes; V may be hex), so the interesting branches are taken
        out:N                   pointer to N zero bytes, compared after the call
        fn:NAME                 a code address (the same on both sides) that runs the CALLS entry NAME: callbacks the
                                function under test stores or calls through a pointer (e.g. a reg_init)
        kn:N:MAX                pointer to N ascending LE u32 in [0, MAX] (repeats allowed), compared
        s32s:N:LO:HI            pointer to N LE s32 in [LO, HI], compared
        info:VSYM:SIZE          a block state of SIZE bytes (random words: full, below 2^8, below 2^12 or edge values):
                                the vendor gets channel 0 and the block's address in its global pointer array VSYM[0]
                                (e.g. mdnsInfo), ours gets the block's address; the block is compared after the call.
        info:VSYM:SIZE:W<M,..   the same with block word W (hex index) below M (e.g. table indices the vendor
                                takes from 8-bit IQ fields and does not mask)
        info:VSYM:SIZE:W@N,..   block word W (hex index) points at N random bytes (same address on both sides,
                                compared after the call): the section / parameter pointers a block state holds
  CALLS NAME[:NARGS][@ARG:LEN]..[=RET] ...        (line) functions the code under test calls (vendor: a function of the module or a kernel
                                import; ours: an `extern` declaration in the wrapper with the SAME name): neither side runs
                                them, every call is recorded as (name, first NARGS of $a0..$a3; default 4) and returns RET (default 0; R1,R2,.. cycles through the values, by case number and call index, identical on both sides); @ARG:LEN (repeatable) records the LEN bytes argument ARG points to instead of the pointer value; the call
                                sequences are compared like the buffers.  Use it for callees that are ported elsewhere
                                or need hardware (register access, rcfg trigger, printk).
        nest:N:OFF=[SPEC]:OFF=V1,V2..   pointer chains (OFF=V..: word values as in st): N random bytes whose LE u32 at each OFF points at the block SPEC
                                describes (buf / out / st / nest again, in brackets); every block of the chain is
                                compared after the call (functions that dereference a pointer stored in an argument)
  CALLS ... !ARG:LEN[:OFF=V1,V2..]   (after the NAME item, repeatable) the callee FILLS the LEN bytes ARG points to with
                                deterministic pseudo-random bytes (the LE u32 at each OFF one of the values), the same
                                on both sides: copy_from_user style callees whose result the code under test reads
  VEC VENDOR V1 V2 ...          (line) fixed values for the scalar arguments of VENDOR (pointer arguments
                                generated as usual), run before the random cases
  More than four argument words go to the stack (o32).
  GLOBAL VSYM OSYM SIZE [w:OFF:V1,V2,..] [p:OFF:N]
                                (line) a module global of SIZE bytes: same random content before every call
                                on both sides, compared after.  Optional w:OFF:V1,V2,.. (several allowed): the LE
                                u32 at byte OFF is one of V1,V2,.. (random choice) instead of random, so mode
                                words / flags take their meaningful values.  p:OFF:N (several allowed): the LE u32 at
                                byte OFF is a pointer to N random bytes (allocated per case, same address on both
                                sides, compared after the call like the pointer arguments): for info structs that
                                point at parameter blocks
                                q:OFF:N[:z][:bB=V1,V2..][:@O2=N2]..  (several allowed): like p:, z = zero bytes instead
                                of random (large list pages); bB=..: byte B of the block one of the values (flags);
                                @O2=N2[/B=V1,V2..]: the LE u32 at byte O2 of the block points to N2 random bytes (nested, compared; /B=..: byte B of the nested block one of the values);
                                @O2=N2f: the same from one randbytes call (fast, for large blocks such as a register window)
Exit 0 = all cases equal, 1 = mismatch (first 5 printed), 2 = setup error.
Nothing of the vendor module is stored here; point it at your own copy.
"""
import argparse, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu  # noqa: E402

M32 = 0xffffffff


def _r(e, n):
    from unicorn.mips_const import UC_MIPS_REG_A0
    return e.get_reg(UC_MIPS_REG_A0 + n)


def _q(e, n):
    return _r(e, n) | _r(e, n + 1) << 32


def _udiv(n, d):
    q = n // d if d else 0
    return (q & M32, (q >> 32) & M32)


# kernel 64-bit division helpers the vendor module imports (linux/math64.h), besides ko_emu's libgcc set
KERNEL_HLE = {
    'div64_u64': lambda e: _udiv(_q(e, 0), _q(e, 2)),
    'div_u64': lambda e: _udiv(_q(e, 0), _r(e, 2)),
    # an error message only (e.g. isp_rdns_itp_update_ks with a zero range): no state, returns 0; list printk in
    # CALLS to compare it as a call instead
    'printk': lambda e: (0, 0),
}
EDGE32 = [0, 1, 2, 3, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe, 0x100, 0x1000, 0x10000]


calls = {}   # CALLS: name -> (return values, nargs, derefs)
caseno = [0]
calllog = []


def parse(path):
    fns, gl, vec = [], [], {}
    calls.clear()
    for ln in open(path):
        t = ln.split('#', 1)[0].split()
        if not t:
            continue
        if t[0] == 'GLOBAL':
            gl.append((t[1], t[2], int(t[3], 0), [(w.split(':')[0], int(w.split(':')[1], 0), w.split(':', 2)[2]) if w.startswith('q:') else
                                                  (w.split(':')[0], int(w.split(':')[1], 0), [int(x, 0) for x in w.split(':')[2].split(',')]) for w in t[4:]]))
        elif t[0] == 'CALLS':
            for x in t[1:]:
                x, *fills = x.split('!')
                n, _, rv = x.partition('=')
                n, *deref = n.split('@')
                n, _, na = n.partition(':')
                fl = []
                for f in fills:
                    q = f.split(':')
                    fl.append((int(q[0], 0), int(q[1], 0), [(int(o.split('=')[0], 0), [int(v, 0) for v in o.split('=')[1].split(',')]) for o in q[2:]]))
                calls[n] = ([int(v, 0) for v in rv.split(',')] if rv else [0], int(na) if na else 4, [tuple(int(v, 0) for v in d.split(':')) for d in deref], fl)
        elif t[0] == 'VEC':
            vec.setdefault(t[1], []).append([int(x, 0) for x in t[2:]])
        else:
            fns.append((t[0], t[1], t[2], t[3:]))
    return fns, gl, vec


class Gen:
    def __init__(s, rnd):
        s.r = rnd

    def value(s, a, case):
        r = s.r
        if a == 'u32':
            return EDGE32[case] if case < len(EDGE32) else (r.getrandbits(32) if r.random() < .5 else r.getrandbits(r.randint(1, 31)))
        if a.startswith('r:'):
            lo, hi = (int(x, 0) for x in a[2:].split(':'))
            ed = [lo, hi, 0, 1, -1, lo + 1, hi - 1]
            ed = [x for x in ed if lo <= x <= hi]
            return ed[case] if case < len(ed) else r.randint(lo, hi)
        if a.startswith('sq:'):
            n = r.randint(0, int(int(a[3:], 0) ** .5))
            return n * n + n + r.choice((-1, 0, 1))
        if a.startswith('u64'):
            b = int(a[4:]) if ':' in a else 64
            e = [0, 1, 2, (1 << b) - 1, 1 << (b - 1)]
            return e[case] if case < len(e) else r.getrandbits(r.randint(1, b))
        raise ValueError(a)

    def buf(s, a, case):
        r = s.r; p = a.split(':')
        if p[0] in ('buf', 'out'):
            n = int(p[1], 0)
            return bytes(n) if p[0] == 'out' else bytes(r.getrandbits(8) for _ in range(n))
        if p[0] == 'st':     # st:N:OFF=V1,V2:OFF=V..   N random bytes, the LE u32 at each OFF one of the listed values
            n = int(p[1], 0); b = bytearray(r.getrandbits(8) for _ in range(n))
            for f in p[2:]:
                off, vals = f.split('=')
                b[int(off, 0):int(off, 0) + 4] = struct.pack('<I', r.choice([int(x, 0) for x in vals.split(',')]) & M32)[:max(0, n - int(off, 0))]
            return bytes(b[:n])
        if p[0] == 'kn':
            n, mx = int(p[1], 0), int(p[2], 0)
            v = sorted(r.randint(0, mx) for _ in range(n))
            if case % 7 == 3 and n > 2:          # repeated knots (zero-width intervals)
                v[n // 2] = v[n // 2 - 1]
            return struct.pack('<%dI' % n, *v)
        if p[0] == 's32s':
            n, lo, hi = int(p[1], 0), int(p[2], 0), int(p[3], 0)
            return struct.pack('<%di' % n, *(r.randint(lo, hi) for _ in range(n)))
        raise ValueError(a)


def is_ptr(a):
    return a.split(':')[0] in ('buf', 'out', 'kn', 's32s', 'st')


def split_top(s, sep):
    """split s at sep outside of [...]"""
    out, d, cur = [], 0, ''
    for ch in s:
        if ch == '[':
            d += 1
        elif ch == ']':
            d -= 1
        if ch == sep and d == 0:
            out.append(cur); cur = ''
        else:
            cur += ch
    out.append(cur)
    return out


def nest_tree(g, spec, case):
    """nest:N:OFF=[SPEC]:OFF=[SPEC] -> (bytes, [(off, tree)]); any other kind -> (bytes, [])"""
    if not spec.startswith('nest:'):
        return g.buf(spec, case), []
    f = split_top(spec[5:], ':')
    b = bytearray(g.buf('buf:' + f[0], case)); subs = []
    for x in f[1:]:
        off, _, child = x.partition('=')
        if child.startswith('['):
            subs.append((int(off, 0), nest_tree(g, child[1:-1], case)))
        else:                                   # OFF=V1,V2: the LE u32 at OFF is one of the values
            o = int(off, 0); b[o:o + 4] = struct.pack('<I', g.r.choice([int(v, 0) for v in child.split(',')]) & M32)
    return bytes(b), subs


def fill_bytes(case, idx, ln, offs):
    r = random.Random('fill:%d:%d' % (case, idx)); b = bytearray(r.getrandbits(8) for _ in range(ln))
    for o, vals in offs:
        b[o:o + 4] = struct.pack('<I', r.choice(vals) & M32)[:max(0, ln - o)]
    return bytes(b)


def info_block(r, size, lim=''):
    """random block state words: full 32 bit, small (< 2^8), medium (< 2^12) or edge values (so that equal /
    neighbouring words and the usual limits are frequent), chosen per case; lim 'W<M,..'"""
    n = size // 4; m = r.choice((32, 8, 12, 12, 0))
    if m:
        w = [r.getrandbits(m) for _ in range(n)]
    else:
        w = [r.choice((0, 1, 2, 3, 0x40, 0x7f, 0x80, 0xff, 0x100, 0x101, 0xffc, 0xffd, 0xffe, 0xfff, 0x1000,
                       0xffffffff, r.getrandbits(12))) for _ in range(n)]
    sub = []
    for c in filter(None, lim.split(',')):
        if '@' in c:            # W@N: word W is a pointer to N random bytes (patched in run_one)
            i, ln = c.split('@'); sub.append((int(i, 16), bytes(r.getrandbits(8) for _ in range(int(ln, 0)))))
            continue
        i, mx = c.split('<')
        w[int(i, 16)] %= int(mx, 0)
    return struct.pack('<%dI' % n, *w) + bytes(size - 4 * n), sub


def run_one(e, fn, args, gl_init, gl_syms, gl_ptrs=(), vendor=False):
    """args: list of ('v', int) / ('v64', int) / ('p', bytes) / ('i', (vsym, bytes)); returns (v0, v1, [buffers],
    [globals], stop)"""
    e.heap_reset(); words = []; ptrs = []; del calllog[:]
    for k, v in args:
        if k == 'i':
            sym, (blk, sub) = v
            blk = bytearray(blk); subp = []
            for wi, data in sub:
                pa = e.alloc(len(data) + 16, data + bytes(16)); subp.append((pa, len(data)))
                blk[4 * wi:4 * wi + 4] = struct.pack('<I', pa)
            blk = bytes(blk)
            ad = e.alloc(len(blk) + 16, blk + bytes(16)); ptrs.append((ad, len(blk))); ptrs += subp
            if vendor:
                e.write(e.addr(sym), struct.pack('<I', ad)); words.append(0)
            else:
                words.append(ad)
        elif k == 'n':
            def put(tree):
                data, subs = tree; data = bytearray(data); kids = []
                for off, st in subs:
                    ca = put(st); data[off:off + 4] = struct.pack('<I', ca)
                ad = e.alloc(len(data) + 16, bytes(data) + bytes(16)); ptrs.append((ad, len(data)))
                return ad
            words.append(put(v))
        elif k == 'p':
            ad = e.alloc(max(len(v), 4) + 16, v + bytes(16)); ptrs.append((ad, len(v))); words.append(ad)
        elif k == 'v64':
            if len(words) & 1:
                words.append(0)
            words += [v & M32, (v >> 32) & M32]
        else:
            words.append(v & M32)
    for (sym, size), data in zip(gl_syms, gl_init):
        e.write(e.addr(sym), data)
    for sym, off, data, *sub in gl_ptrs:
        data = bytearray(data)
        for o2, d2 in (sub[0] if sub else ()):      # q: nested blocks first, their address into the outer block
            a2 = e.alloc(len(d2) + 16, d2 + bytes(16)); ptrs.append((a2, len(d2)))
            data[o2:o2 + 4] = struct.pack('<I', a2)
        data = bytes(data)
        ad = e.alloc(len(data) + 16, data + bytes(16)); ptrs.append((ad, len(data)))
        e.write(e.addr(sym) + off, struct.pack('<I', ad))
    r = e.call(fn, *words[:4], stack_args=words[4:])
    bufs = [e.read(a, n + 16) for a, n in ptrs]
    gls = [e.read(e.addr(sym), size) for sym, size in gl_syms]
    return r.v0, r.v1, bufs, gls, r.stop, list(calllog)


def ret_of(ret, v0, v1):
    return {'v0': v0, 'u8': v0 & 0xff, 'u16': v0 & 0xffff, 's16': v0 & 0xffff, 'v0v1': (v0, v1), 'none': None}[ret]


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('oem'); ap.add_argument('our'); ap.add_argument('spec')
    ap.add_argument('--n', type=int, default=2000); ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--only', default=None)
    a = ap.parse_args()
    fns, gl, vec = parse(a.spec)
    try:
        oem = KoEmu(a.oem, hle=KERNEL_HLE, max_insns=20_000_000); our = KoEmu(a.our, hle=KERNEL_HLE, max_insns=20_000_000)
    except Exception as ex:  # noqa: BLE001
        print('t33_binoracle: setup error: %s' % ex); return 2
    from unicorn.mips_const import UC_MIPS_REG_A0 as A0
    fnames = sorted({t[3:] for _f in fns for t in _f[3] if t.startswith('fn:')})
    for emu in (oem, our):
        for i, n in enumerate(fnames):          # fn: callbacks at one fixed address in the stub page on both sides
            emu.fnaddr = getattr(emu, 'fnaddr', {}); emu.fnaddr[n] = 0x70000f00 + 0x10 * i; emu.stubname[emu.fnaddr[n]] = n
    for emu, side in ((oem, 'vendor'), (our, 'ours')):
        for n, (rv, na, dr, fl) in calls.items():
            if n in fnames:
                pass
            elif n not in emu.symaddr and n not in emu.stubs:
                if side == 'ours' and n in emu.stubs:
                    continue
                print('t33_binoracle: CALLS %s not found on the %s side' % (n, side)); return 2
            if n in emu.symaddr and n not in emu.stubs:      # a function of the module: trap its entry
                ad = emu.symaddr[n]; emu.stubname[ad] = n
            def mk(nm, r, k, d, fl):
                def h(e):
                    calllog.append((nm,) + tuple('*' if i in [x for x, _ in d] else _r(e, i) for i in range(k)) + tuple(bytes(e.read(_r(e, i), ln)).hex() for i, ln in d))
                    for fa, fln, fo in fl:
                        e.write(_r(e, fa), fill_bytes(caseno[0], len(calllog), fln, fo))
                    return r[(caseno[0] + len(calllog)) % len(r)]
                return h
            emu.hle[n] = mk(n, rv, na, dr, fl)
    for v, o, sz, _w in gl:
        if v not in oem.symaddr or o not in our.symaddr:
            print('t33_binoracle: global %s / %s missing' % (v, o)); return 2
    bad = 0; total = 0
    for vfn, ofn, ret, argt in fns:
        if a.only and vfn != a.only:
            continue
        if vfn not in oem.symaddr or ofn not in our.symaddr:
            print('t33_binoracle: %s / %s missing' % (vfn, ofn)); return 2
        rnd = random.Random('%d:%s' % (a.seed, vfn)); g = Gen(rnd); nf = 0
        fixed = vec.get(vfn, [])
        for case in range(a.n + len(fixed)):
            args = []; fv = list(fixed[case]) if case < len(fixed) else None
            for t in argt:
                if t.startswith('info:'):
                    _, sym, sz, lim = (t + ':').split(':')[:4]
                    if sym not in oem.symaddr:
                        print('t33_binoracle: %s missing' % sym); return 2
                    args.append(('i', (sym, info_block(rnd, int(sz, 0), lim))))
                elif t.startswith('fn:'):
                    args.append(('v', 0x70000f00 + 0x10 * fnames.index(t[3:])))
                elif t.startswith('nest:'):
                    args.append(('n', nest_tree(g, t, case)))
                elif is_ptr(t):
                    args.append(('p', g.buf(t, case)))
                else:
                    v = fv.pop(0) if fv else g.value(t, case - len(fixed))
                    args.append(('v64' if t.startswith('u64') else 'v', v))
            caseno[0] = case; gi = []; gpv = []; gpo = []
            for (vs, os_, sz, ws) in gl:
                gb = bytearray(rnd.getrandbits(8) for _ in range(sz))
                for kind, off, vals in ws:
                    if kind == 'q':
                        f = vals.split(':'); n = int(f[0], 0); z = 'z' in f[1:]
                        pd = bytearray(n) if z else bytearray(rnd.getrandbits(8) for _ in range(n)); sub = []
                        for x in f[1:]:
                            if x.startswith('b'):
                                b, vv = x[1:].split('='); pd[int(b, 0)] = rnd.choice([int(v, 0) for v in vv.split(',')]) & 0xff
                            elif x.startswith('@'):
                                o2, n2 = x[1:].split('=')
                                n2, *nb = n2.split('/')
                                d2 = rnd.randbytes(int(n2[:-1], 0)) if n2.endswith('f') else bytes(rnd.getrandbits(8) for _ in range(int(n2, 0)))
                                if nb:
                                    d2 = bytearray(d2)
                                    for y in nb:
                                        bb, vv = y.split('='); d2[int(bb, 0)] = rnd.choice([int(v, 0) for v in vv.split(',')]) & 0xff
                                    d2 = bytes(d2)
                                sub.append((int(o2, 0), d2))
                        gpv.append((vs, off, bytes(pd), sub)); gpo.append((os_, off, bytes(pd), sub))
                        continue
                    if kind == 'p':
                        pd = bytes(rnd.getrandbits(8) for _ in range(vals[0])); gpv.append((vs, off, pd)); gpo.append((os_, off, pd))
                        continue
                    gb[off:off + 4] = struct.pack('<I', rnd.choice(vals) & M32)[:max(0, sz - off)]
                    del gb[sz:]
                gi.append(bytes(gb))
            ro = run_one(oem, vfn, args, gi, [(v, sz) for v, _, sz, _w in gl], gpv, vendor=True)
            ru = run_one(our, ofn, args, gi, [(o, sz) for _, o, sz, _w in gl], gpo)
            total += 1
            if ro[4] or ru[4]:
                diff = 'stopped: vendor %s, ours %s' % (ro[4], ru[4])
            elif ret_of(ret, ro[0], ro[1]) != ret_of(ret, ru[0], ru[1]):
                diff = 'return vendor %s ours %s' % (ret_of(ret, ro[0], ro[1]), ret_of(ret, ru[0], ru[1]))
            elif ro[2] != ru[2]:
                i = next(k for k in range(len(ro[2])) if ro[2][k] != ru[2][k])
                j = next(k for k in range(len(ro[2][i])) if ro[2][i][k] != ru[2][i][k])
                diff = 'pointer arg %d byte +%#x vendor %02x ours %02x' % (i, j, ro[2][i][j], ru[2][i][j])
            elif ro[5] != ru[5]:
                diff = 'calls vendor %s ours %s' % (['%s(%s)' % (c[0], ','.join(('%#x' % x) if isinstance(x, int) else x for x in c[1:])) for c in ro[5]],
                                                    ['%s(%s)' % (c[0], ','.join(('%#x' % x) if isinstance(x, int) else x for x in c[1:])) for c in ru[5]])
            elif ro[3] != ru[3]:
                i = next(k for k in range(len(ro[3])) if ro[3][k] != ru[3][k])
                diff = 'global %s differs' % gl[i][0]
            else:
                continue
            nf += 1; bad += 1
            if nf <= 5:
                shown = ['%#x' % v if k in ('v', 'v64') else '<%d B>' % len(v if k == 'p' else (v[0] if k == 'n' else v[1][0])) for k, v in args]
                print('MISMATCH %s(%s): %s' % (vfn, ', '.join(shown), diff))
        print('%s: %d cases, %d mismatches' % (vfn, a.n + len(fixed), nf))
    print('t33_binoracle: %d cases, %d mismatches' % (total, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
