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
        out:N                   pointer to N zero bytes, compared after the call
        kn:N:MAX                pointer to N ascending LE u32 in [0, MAX] (repeats allowed), compared
        s32s:N:LO:HI            pointer to N LE s32 in [LO, HI], compared
        info:VSYM:SIZE          a block state of SIZE bytes (random words: full, below 2^8, below 2^12 or edge values):
                                the vendor gets channel 0 and the block's address in its global pointer array VSYM[0]
                                (e.g. mdnsInfo), ours gets the block's address; the block is compared after the call.
        info:VSYM:SIZE:W<M,..   the same with block word W (hex index) below M (e.g. table indices the vendor
                                takes from 8-bit IQ fields and does not mask)
  VEC VENDOR V1 V2 ...          (line) fixed values for the scalar arguments of VENDOR (pointer arguments
                                generated as usual), run before the random cases
  More than four argument words go to the stack (o32).
  GLOBAL VSYM OSYM SIZE         (line) a module global of SIZE bytes: same random content before every call
                                on both sides, compared after
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
}
EDGE32 = [0, 1, 2, 3, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe, 0x100, 0x1000, 0x10000]


def parse(path):
    fns, gl, vec = [], [], {}
    for ln in open(path):
        t = ln.split('#', 1)[0].split()
        if not t:
            continue
        if t[0] == 'GLOBAL':
            gl.append((t[1], t[2], int(t[3], 0)))
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
    return a.split(':')[0] in ('buf', 'out', 'kn', 's32s')


def info_block(r, size, lim=''):
    """random block state words: full 32 bit, small (< 2^8), medium (< 2^12) or edge values (so that equal /
    neighbouring words and the usual limits are frequent), chosen per case; lim 'W<M,..'"""
    n = size // 4; m = r.choice((32, 8, 12, 12, 0))
    if m:
        w = [r.getrandbits(m) for _ in range(n)]
    else:
        w = [r.choice((0, 1, 2, 3, 0x40, 0x7f, 0x80, 0xff, 0x100, 0x101, 0xffc, 0xffd, 0xffe, 0xfff, 0x1000,
                       0xffffffff, r.getrandbits(12))) for _ in range(n)]
    for c in filter(None, lim.split(',')):
        i, mx = c.split('<')
        w[int(i, 16)] %= int(mx, 0)
    return struct.pack('<%dI' % n, *w) + bytes(size - 4 * n)


def run_one(e, fn, args, gl_init, gl_syms, vendor=False):
    """args: list of ('v', int) / ('v64', int) / ('p', bytes) / ('i', (vsym, bytes)); returns (v0, v1, [buffers],
    [globals], stop)"""
    e.heap_reset(); words = []; ptrs = []
    for k, v in args:
        if k == 'i':
            sym, blk = v
            ad = e.alloc(len(blk) + 16, blk + bytes(16)); ptrs.append((ad, len(blk)))
            if vendor:
                e.write(e.addr(sym), struct.pack('<I', ad)); words.append(0)
            else:
                words.append(ad)
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
    r = e.call(fn, *words[:4], stack_args=words[4:])
    bufs = [e.read(a, n + 16) for a, n in ptrs]
    gls = [e.read(e.addr(sym), size) for sym, size in gl_syms]
    return r.v0, r.v1, bufs, gls, r.stop


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
    for v, o, sz in gl:
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
                elif is_ptr(t):
                    args.append(('p', g.buf(t, case)))
                else:
                    v = fv.pop(0) if fv else g.value(t, case - len(fixed))
                    args.append(('v64' if t.startswith('u64') else 'v', v))
            gi = [bytes(rnd.getrandbits(8) for _ in range(sz)) for _, _, sz in gl]
            ro = run_one(oem, vfn, args, gi, [(v, sz) for v, _, sz in gl], vendor=True)
            ru = run_one(our, ofn, args, gi, [(o, sz) for _, o, sz in gl])
            total += 1
            if ro[4] or ru[4]:
                diff = 'stopped: vendor %s, ours %s' % (ro[4], ru[4])
            elif ret_of(ret, ro[0], ro[1]) != ret_of(ret, ru[0], ru[1]):
                diff = 'return vendor %s ours %s' % (ret_of(ret, ro[0], ro[1]), ret_of(ret, ru[0], ru[1]))
            elif ro[2] != ru[2]:
                i = next(k for k in range(len(ro[2])) if ro[2][k] != ru[2][k])
                j = next(k for k in range(len(ro[2][i])) if ro[2][i][k] != ru[2][i][k])
                diff = 'pointer arg %d byte +%#x vendor %02x ours %02x' % (i, j, ro[2][i][j], ru[2][i][j])
            elif ro[3] != ru[3]:
                i = next(k for k in range(len(ro[3])) if ro[3][k] != ru[3][k])
                diff = 'global %s differs' % gl[i][0]
            else:
                continue
            nf += 1; bad += 1
            if nf <= 5:
                shown = ['%#x' % v if k in ('v', 'v64') else '<%d B>' % len(v if k == 'p' else v[1]) for k, v in args]
                print('MISMATCH %s(%s): %s' % (vfn, ', '.join(shown), diff))
        print('%s: %d cases, %d mismatches' % (vfn, a.n + len(fixed), nf))
    print('t33_binoracle: %d cases, %d mismatches' % (total, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
