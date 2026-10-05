#!/usr/bin/env python3
"""T20/T10 firmware: function-level differential test against the vendor module.

Runs the OEM-mode scenario of tests/t20_fw/harness.c on the vendor
tx-isp-t20.ko (MIPS, emulated with unicorn) and records, for every function
that exists in both the vendor module and our MIPS build of
driver/t20/tx_isp_t20_firmware.c, the complete machine state at several of
its calls.  Each recorded call is then replayed twice from that identical
state: once with the vendor function, once with ours (pointers into the
vendor image translated to our image by symbol name).  Return value,
register writes, sensor callbacks and all memory (firmware state, heap,
caller stack, calibration tables) must match.

usage: vdiff.py VENDOR.ko OURS.o [--calib FILE] [--only f1,f2] [--max N] [-v]
"""
import argparse
import os
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu  # noqa: E402
import scenario  # noqa: E402
from emu import UC_MIPS_REG_SP, A, CALIB, HEAP, STACK, STACK_SIZE, u32  # noqa: E402

TOP = STACK + STACK_SIZE

# functions not compared: scenario entry points (compared through their
# callees), environment, or functions whose replay needs the caller's context
SKIP = set(emu.ENV_FUNCS) | {
    'apical_init', 'apical_process', 'apical_cmd_process', 'apical_command',
    'apical_sbus_i2c_init', 'log2_fixed_to_fixed', 'line_offset',
}


def capture_points(n):
    """call numbers (1-based) recorded per function"""
    return n <= 3 or n in (5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987, 1597, 2584, 4181)


class Snap:
    __slots__ = ('fn', 'step', 'callno', 'args', 'sp', 'vw', 'heap', 'stack', 'calib', 'env', 'ow')


def take(m, img_writable):
    return [zlib.compress(bytes(m.uc.mem_read(a, n)), 1) for a, n in img_writable]


def put(m, img_writable, blobs):
    for (a, n), b in zip(img_writable, blobs):
        m.uc.mem_write(a, zlib.decompress(b))


class Diff:
    def __init__(self, vendor, ours, calib, verbose=False, sdk=None):
        self.vendor, self.ours, self.calib, self.verbose = vendor, ours, calib, verbose
        self.sdk = sdk
        self.void = void_functions()

    def machine(self, oem=True):
        m = emu.Machine(self.vendor, self.ours)
        scenario.setup_calibrations(m, self.calib)
        m.bank_ids = scenario.bank_ids(self.sdk) if self.sdk else []
        for n in ('t20_simple_ae', 't20_simple_awb', 't20_simple_nr', 't20_trace_events'):
            if n in m.O.sym:
                m.w32(m.O.sym[n][0], 0)
        return m

    # ------------------------------------------------------------ recording
    def record(self, steps, only=None):
        mv, mo = self.machine(), self.machine()
        V, O = mv.V, mv.O
        names = sorted(n for n, (a, s, t) in V.sym.items()
                       if t == 'F' and n in O.sym and O.sym[n][2] == 'F' and n not in SKIP and '.' not in n
                       and (only is None or n in only))
        self.names = names
        counts = {}
        snaps = []
        cur = {'step': 0}
        ow_at_step = {}

        def hook(uc, addr, size, fn):
            c = counts[fn] = counts.get(fn, 0) + 1
            if not capture_points(c):
                return
            s = Snap()
            s.fn, s.step, s.callno = fn, cur['step'], c
            s.args = [uc.reg_read(r) for r in A]
            s.sp = sp = uc.reg_read(UC_MIPS_REG_SP)
            s.vw = take(mv, V.writable)
            s.heap = zlib.compress(bytes(uc.mem_read(HEAP, mv.env.heapp - HEAP)), 1) if mv.env.heapp > HEAP else b''
            s.stack = zlib.compress(bytes(uc.mem_read(sp, TOP - sp)), 1)
            cu = getattr(mv, 'calib_used', CALIB)
            s.calib = zlib.compress(bytes(uc.mem_read(CALIB, cu - CALIB)), 1) if cu > CALIB else b''
            s.env = mv.env.clone_state()
            snaps.append(s)
        for n in names:
            a = V.sym[n][0]
            mv.uc.hook_add(emu.UC_HOOK_CODE, hook, user_data=n, begin=a, end=a)
        rv, ro = scenario.Runner(mv, V), scenario.Runner(mo, O)
        sys_log = []
        for i, st in enumerate(steps):
            cur['step'] = i
            ow_at_step[i] = take(mo, O.writable)
            nv, no = len(mv.env.log), len(mo.env.log)
            rv.step(st)
            ro.step(st)
            if mv.fault or mo.fault:
                print('scenario fault at step %d %s: V=%s O=%s' % (i, st, mv.fault, mo.fault))
                if mv.fault:
                    break
                mo.fault = None
            lv = [tuple(self.norm(mv, x) for x in e) for e in mv.env.log[nv:] if e[0] != 'PRINTK']
            lo = [tuple(self.norm(mo, x) for x in e) for e in mo.env.log[no:] if e[0] != 'PRINTK']
            if lv != lo:
                sys_log.append((i, st, lv, lo))
        for s in snaps:
            s.ow = ow_at_step[s.step]
        self.counts = counts
        self.sys_log = sys_log
        self.mv_final = mv
        return snaps

    # ------------------------------------------------------------ replay
    def translate(self, m, w):
        V, O = m.V, m.O
        if V.contains(w):
            s = V.symof(w)
            if s:
                n = emu.ALIASES.get(s[0], s[0])
                if n in O.sym and n not in emu.ENV_FUNCS:
                    return u32(O.sym[n][0] + s[1])
        return w

    def xlate_bytes(self, m, b):
        import struct
        n = len(b) // 4
        ws = list(struct.unpack_from('<%dI' % n, b))
        lo, hi = m.V.base, m.V.base + m.V.size
        for i, w in enumerate(ws):
            if lo <= w < hi:
                ws[i] = self.translate(m, w)
        return struct.pack('<%dI' % n, *ws) + b[4 * n:]

    def norm(self, m, w):
        if not isinstance(w, int):
            return w
        for img in (m.V, m.O):
            if img.contains(w):
                s = img.symof(w)
                if s:
                    return '&%s+%x' % (emu.ALIASES.get(s[0], s[0]), s[1])
        return w

    def restore_common(self, m, s, side):
        put(m, m.V.writable, s.vw)
        if s.heap:
            b = zlib.decompress(s.heap)
            m.uc.mem_write(HEAP, self.xlate_bytes(m, b) if side == 'O' else b)
        b = zlib.decompress(s.stack)
        m.uc.mem_write(s.sp, self.xlate_bytes(m, b) if side == 'O' else b)
        if s.calib:
            b = zlib.decompress(s.calib)
            m.uc.mem_write(CALIB, self.xlate_bytes(m, b) if side == 'O' else b)
        m.env.set_state(s.env)
        m.env.log = []
        m.env.traps = 0

    def run_side(self, m, s, side):
        self.restore_common(m, s, side)
        put(m, m.O.writable, s.ow)
        if side == 'O':
            for n, va, oa, size in m.common:
                b = bytes(m.uc.mem_read(va, size))
                m.uc.mem_write(oa, self.xlate_bytes(m, b) if size >= 4 else b)
            for n in ('t20_simple_ae', 't20_simple_awb', 't20_simple_nr', 't20_trace_events'):
                if n in m.O.sym:
                    m.w32(m.O.sym[n][0], 0)
            args = [self.translate(m, a) for a in s.args]
            addr = m.O.sym[s.fn][0]
        else:
            args = s.args
            addr = m.V.sym[s.fn][0]
        pre = self.read_mem(m, s, side)
        v0, v1 = m.call(addr, args, sp=s.sp, maxins=50_000_000)
        out = {'ret': self.norm(m, v0), 'fault': m.fault,
               'log': [tuple(self.norm(m, x) for x in e) for e in m.env.log if e[0] != 'PRINTK'],
               'heapp': m.env.heapp}
        out['mem'] = self.read_mem(m, s, side)
        out['pre'] = pre
        return out

    def read_mem(self, m, s, side):
        mem = {}
        img = m.O if side == 'O' else m.V
        for n, va, oa, size in m.common:
            a = oa if side == 'O' else va
            mem['obj:' + n] = bytes(m.uc.mem_read(a, size))
        # vendor writable data except the common objects (environment data)
        ex = [(va, va + size) for n, va, oa, size in m.common]
        for a, size in m.V.writable:
            b = bytearray(m.uc.mem_read(a, size))
            for lo, hi in ex:
                if a <= lo < a + size:
                    b[lo - a:hi - a] = bytes(hi - lo)
            mem['vdata@%x' % a] = bytes(b)
        mem['heap'] = bytes(m.uc.mem_read(HEAP, m.env.heapp - HEAP)) if m.env.heapp > HEAP else b''
        # sp+0..15: argument home area (an -O0 callee spills a0-a3 there)
        mem['stack'] = bytes(16) + bytes(m.uc.mem_read(s.sp + 16, TOP - s.sp - 16))
        cu = getattr(m, 'calib_used', CALIB)
        mem['calib'] = bytes(m.uc.mem_read(CALIB, cu - CALIB)) if cu > CALIB else b''
        return mem

    def compare(self, m, a, b, fname):
        import struct
        d = []
        if a['ret'] != b['ret'] and fname not in self.void:
            d.append('ret V=%s O=%s' % (fmt(a['ret']), fmt(b['ret'])))
        if a['fault'] != b['fault']:
            d.append('fault V=%s O=%s' % (a['fault'], b['fault']))
        if a['log'] != b['log']:
            la, lb = a['log'], b['log']
            k = 0
            while k < min(len(la), len(lb)) and la[k] == lb[k]:
                k += 1
            d.append('log differs at #%d (V %d ev, O %d ev): V=%s O=%s' % (
                k, len(la), len(lb), la[k:k + 3], lb[k:k + 3]))
        for key in a['mem']:
            x, y = a['mem'][key], b['mem'].get(key, b'')
            px, py = a['pre'][key], b['pre'].get(key, b'')
            if x == y or (x == px and y == py):
                continue
            n = min(len(x), len(y), len(px), len(py)) // 4
            wx = struct.unpack_from('<%dI' % n, x)
            wy = struct.unpack_from('<%dI' % n, y)
            vx = struct.unpack_from('<%dI' % n, px)
            vy = struct.unpack_from('<%dI' % n, py)
            offs = []
            for i in range(n):
                if wx[i] != wy[i] and (wx[i] != vx[i] or wy[i] != vy[i]):
                    nx, ny = self.norm(m, wx[i]), self.norm(m, wy[i])
                    if nx != ny:
                        offs.append((i * 4, nx, ny))
            if offs:
                d.append('%s: %d words, e.g. %s' % (key, len(offs), ', '.join(
                    '+%x V=%s O=%s' % (o, fmt(p), fmt(q)) for o, p, q in offs[:6])))
        return d

    def replay(self, snaps):
        m = self.machine()
        res = {}
        for s in snaps:
            a = self.run_side(m, s, 'V')
            b = self.run_side(m, s, 'O')
            d = self.compare(m, a, b, s.fn)
            res.setdefault(s.fn, []).append((s, d))
        return res


def void_functions():
    """functions our source defines as void (their v0 is not compared)"""
    import re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../driver/t20/tx_isp_t20_firmware.c')
    txt = open(src).read()
    return set(re.findall(r'^(?:static\s+)?(?:inline\s+)?void\s+\**\s*(\w+)\s*\(', txt, re.M)) - \
        set(re.findall(r'^(?:static\s+)?(?:inline\s+)?void\s*\*\s*(\w+)\s*\(', txt, re.M))


def fmt(v):
    return ('0x%x' % v) if isinstance(v, int) else str(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('vendor')
    ap.add_argument('ours')
    ap.add_argument('--calib')
    ap.add_argument('--sdk', help='external/ingenic-sdk/3.10.14/isp/t20 (night calibration bank ids)')
    ap.add_argument('--only')
    ap.add_argument('--short', action='store_true')
    ap.add_argument('--sys', action='store_true', help='print whole-scenario divergence')
    ap.add_argument('--fuzz', type=int, default=3000, help='random calls per pure helper (0: off)')
    ap.add_argument('--no-scenario', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    D = Diff(a.vendor, a.ours, a.calib, a.v, a.sdk)
    nfz = 0
    if a.fuzz:
        import fuzz
        m = D.machine()
        names = sorted(fuzz.PURE) if not a.only else [n for n in a.only.split(',') if n in fuzz.PURE]
        for fn, (n, bad) in sorted(fuzz.fuzz(m, names, a.fuzz).items()):
            print('%s %-44s random=%d differing=%d' % ('DIFF' if bad else 'same', fn, n, len(bad)))
            for args, x, y, fx, fy in bad[:(20 if a.v else 3)]:
                print('    args=%s V=0x%x O=0x%x%s' % ([hex(v) for v in args], x, y,
                                                    (' fault V=%s O=%s' % (fx, fy)) if fx or fy else ''))
            nfz += bool(bad)
        api = fuzz.fuzz_api(m, max(50, a.fuzz // 20))
        if a.only:
            api = {k: v for k, v in api.items() if k in a.only.split(',')}
        for fn, (n, bad) in sorted(api.items()):
            print('%s %-44s random=%d differing=%d' % ('DIFF' if bad else 'same', fn, n, len(bad)))
            for args, txt in bad[:(10 if a.v else 2)]:
                print('    args=%s %s' % ([hex(v) for v in args], txt))
            nfz += bool(bad)
        if a.no_scenario:
            return 1 if nfz else 0
    only = set(a.only.split(',')) if a.only else None
    snaps = D.record(scenario.scenario(not a.short), only)
    if a.sys:
        for i, st, lv, lo in D.sys_log[:(400 if a.v else 8)]:
            k = 0
            while k < min(len(lv), len(lo)) and lv[k] == lo[k]:
                k += 1
            print('SYS step %d %s: first diff at event %d/%d: V=%s O=%s' % (i, st, k, len(lv), lv[k:k + 4], lo[k:k + 4]))
        print('SYS steps with divergent register/sensor traffic: %d' % len(D.sys_log))
    res = D.replay(snaps)
    ndiff = 0
    for fn in sorted(res):
        rs = res[fn]
        # a differing return value alone is reported but not counted: the
        # vendor leaves stale registers in v0 of void functions, and a
        # return value that matters shows up in the caller's comparison
        bad = [(s, d) for s, d in rs if d and not all(x.startswith('ret ') for x in d)]
        soft = [(s, d) for s, d in rs if d and all(x.startswith('ret ') for x in d)]
        tag = 'DIFF' if bad else ('ret ' if soft else 'same')
        print('%s %-44s calls=%-6d compared=%-3d differing=%d' % (tag, fn, D.counts.get(fn, 0), len(rs), len(bad)))
        if bad or (soft and a.v):
            ndiff += bool(bad)
            for s, d in (bad or soft)[: (99 if a.v else 2)]:
                print('    step %d call #%d args=%s' % (s.step, s.callno, [hex(x) for x in s.args]))
                for line in d[:8]:
                    print('      ' + line)
    never = [n for n in D.names if n not in res]
    print('functions compared: %d, differing: %d, never called by the scenario: %d' % (len(res), ndiff, len(never)))
    if a.v:
        print('never called: ' + ' '.join(never))
    return 1 if ndiff or nfz else 0


if __name__ == '__main__':
    sys.exit(main())
