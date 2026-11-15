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
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu  # noqa: E402
import scenario  # noqa: E402
from emu import UC_MIPS_REG_SP, A, CALIB, HEAP, STACK, STACK_SIZE, u32  # noqa: E402

TOP = STACK + STACK_SIZE
VBASE_END = emu.OBASE

# functions not compared: scenario entry points (compared through their
# callees), environment, or functions whose replay needs the caller's context
SKIP = set(emu.ENV_FUNCS) | {
    'apical_init', 'apical_process', 'apical_cmd_process', 'apical_command',
    'apical_sbus_i2c_init', 'log2_fixed_to_fixed', 'line_offset',
}


def capture_points(n):
    """call numbers (1-based) recorded per function: the first 8, then
    every 10th up to 100 and every 50th after that (per-frame functions get
    ~30 samples spread over the whole scenario)"""
    return n <= 8 or (n <= 100 and n % 10 == 0) or n % 50 == 0


# Boundary replays: recorded calls re-run with inputs moved onto the
# decision thresholds the scenario does not hit by itself.  Each patch
# function gets the machine and the vendor-side a0 and edits vendor memory
# before the state is translated for our side.
def _ccm_edges():
    out = []
    for k in range(8):                  # thresholds at +196..+224
        for d in (-1, 0, 1):
            def p(m, a0, k=k, d=d):
                m.w32(m.r32(a0) + 3776, m.r32(a0 + 196 + 4 * k) + d)
            out.append(('thr%d%+d' % (k, d), p))
    return out


def _gain_edges():
    out = []
    for prev in (0, 0x8000, 0x30000):
        for d in (-0x1000, -0xfff, -1, 0, 1, 0xfff, 0x1000, 0x3000):
            def p(m, a0, prev=prev, d=d):
                m.w32(a0 + 460, prev)                   # previous gain
                m.w32(a0 + 472, max(0, prev + 0x1000 + d))  # target (-0x1000)
            out.append(('prev%x%+d' % (prev, d), p))
    return out


EDGES = {'color_matrix_update': _ccm_edges, 'cmos_analog_gain_update': _gain_edges}
EDGE_SNAPS = 3      # recorded calls per function used as edge-replay base


class Snap:
    __slots__ = ('fn', 'step', 'callno', 'args', 'sp', 'vw', 'heap', 'stack', 'calib', 'env', 'ow',
                 'patch', 'label')


def take(m, img_writable):
    return [zlib.compress(bytes(m.uc.mem_read(a, n)), 1) for a, n in img_writable]


def put(m, img_writable, blobs):
    for (a, n), b in zip(img_writable, blobs):
        m.uc.mem_write(a, zlib.decompress(b))


class Diff:
    def __init__(self, vendor, ours, calib, verbose=False, sdk=None):
        self.vendor, self.ours, self.calib, self.verbose = vendor, ours, calib, verbose
        self.sdk = sdk
        self.seq = True
        self.void = void_functions()
        self.skip_mut = set()
        self.traps = False
        self.maxins = 50_000_000
        self.mut_budget = 120
        self.stack_args = stack_arg_counts()

    def machine(self, oem=True):
        m = emu.Machine(self.vendor, self.ours, self.traps)
        scenario.setup_calibrations(m, self.calib)
        m.bank_ids = scenario.bank_ids(self.sdk) if self.sdk else []
        if self.seq:
            scenario.setup_sequence(m, self.sdk)
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
            s.patch = s.label = None
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

    def vonly_static(self, m):
        """vendor address ranges of the statics of STATIC_LAYOUT functions (iir_filter_v2:
        sp/fsp/csp in .sbss; ours keeps the same state in iir_filter_v2_state, no
        same-name partner): their callers' replays must not compare them"""
        if getattr(m, '_vonly', None) is None:
            m._vonly = [(a, a + n) for k, (a, n, t) in m.V.sym.items()
                        if t == 'O' and k in VONLY_STATICS]
        return m._vonly

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
        if getattr(s, 'patch', None):
            s.patch(m, s.args[0])
        if side == 'O':
            for n, va, oa, size in m.common:
                b = bytes(m.uc.mem_read(va, size))
                m.uc.mem_write(oa, self.xlate_bytes(m, b) if size >= 4 else b)
            for on, parts in SPLIT_STATICS.items():      # vendor statics kept in one array of ours
                if on in m.O.sym:
                    for vn, word in parts:
                        if vn in m.V.sym:
                            va, vs, _ = m.V.sym[vn]
                            m.uc.mem_write(m.O.sym[on][0] + 4 * word, bytes(m.uc.mem_read(va, vs)))
            for n in ('t20_simple_ae', 't20_simple_awb', 't20_simple_nr', 't20_trace_events'):
                if n in m.O.sym:
                    m.w32(m.O.sym[n][0], 0)
            args = [self.translate(m, a) for a in s.args]
            addr = m.O.sym[s.fn][0]
        else:
            args = s.args
            addr = m.V.sym[s.fn][0]
        pre = self.read_mem(m, s, side)
        v0, v1 = m.call(addr, args, sp=s.sp, maxins=self.maxins)
        out = {'ret': self.norm(m, v0), 'fault': m.fault,
               'log': [tuple(self.norm(m, x) for x in e) for e in m.env.log if e[0] != 'PRINTK'],
               'heapp': m.env.heapp, 'trap_at': getattr(m, 'trap_at', None)}
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
        # sp+0..15: argument home area (an -O0 callee spills a0-a3 there);
        # the stack-passed arguments after it belong to the callee as well
        # (o32), the vendor build reuses them as scratch
        home = 16 + 4 * self.stack_args.get(s.fn, 0)
        mem['stack'] = bytes(home) + bytes(m.uc.mem_read(s.sp + home, TOP - s.sp - home))
        cu = getattr(m, 'calib_used', CALIB)
        mem['calib'] = bytes(m.uc.mem_read(CALIB, cu - CALIB)) if cu > CALIB else b''
        return mem

    def compare(self, m, a, b, fname):
        import struct
        d = []
        if a['fault'] and b['fault']:
            if (a['fault'] == 'trap') != (b['fault'] == 'trap'):
                d.append('fault V=%s O=%s' % (a['fault'] if a['fault'] != 'trap' else 'trap(%s)' % a.get('trap_at'),
                                              b['fault'] if b['fault'] != 'trap' else 'trap(%s)' % b.get('trap_at')))
            return d            # both ran into wild accesses (mutated state): addresses differ per image
        if a['ret'] != b['ret'] and fname not in self.void:
            d.append('ret V=%s O=%s' % (fmt(a['ret']), fmt(b['ret'])))
        if a['fault'] != b['fault']:
            d.append('fault V=%s O=%s' % (a['fault'] if a['fault'] != 'trap' else 'trap(%s)' % a.get('trap_at'),
                                          b['fault'] if b['fault'] != 'trap' else 'trap(%s)' % b.get('trap_at')))
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
            kb = int(key[6:], 16) if key.startswith('vdata@') else None
            for i in range(n):
                if kb is not None and any(lo <= kb + 4 * i < hi for lo, hi in self.vonly_static(m)):
                    continue        # vendor-only static of a STATIC_LAYOUT function (ours keeps it elsewhere)
                if wx[i] != wy[i] and (wx[i] != vx[i] or wy[i] != vy[i]):
                    nx, ny = self.norm(m, wx[i]), self.norm(m, wy[i])
                    if nx != ny:
                        offs.append((i * 4, nx, ny))
            if offs:
                d.append('%s: %d words, e.g. %s' % (key, len(offs), ', '.join(
                    '+%x V=%s O=%s' % (o, fmt(p), fmt(q)) for o, p, q in offs[:6])))
        return d

    def edge_snaps(self, snaps):
        out = []
        for fn, gen in EDGES.items():
            base = [s for s in snaps if s.fn == fn][:EDGE_SNAPS]
            for s in base:
                for label, p in gen():
                    e = Snap()
                    for k in Snap.__slots__:
                        setattr(e, k, getattr(s, k, None))
                    e.fn, e.patch, e.label = fn, p, label
                    out.append(e)
        return out

    # ------------------------------------------------------------ mutation fuzz
    def probe_machine(self):
        """separate machine whose vendor code is traced instruction by instruction: every load
        (lb/lh/lw/lbu/lhu) is recorded as (address, width)"""
        import struct as st
        from unicorn import UC_HOOK_CODE
        from unicorn import mips_const as mc
        m = self.machine()
        com = [(va, va + size) for n, va, oa, size in m.common]
        # vendor objects our code reaches by name (extern, e.g. stab): shared by both images
        com += [(a, a + sz) for n, (a, sz, t) in m.V.sym.items() if t == 'O' and sz and n in m.O.undef]
        m.seen = {}
        regs = [getattr(mc, 'UC_MIPS_REG_%d' % i) for i in range(32)]
        width = {0x20: 1, 0x24: 1, 0x21: 2, 0x25: 2, 0x23: 4}
        vb = m.V.base

        def hook(uc, addr, size, ud):
            w = st.unpack_from('<I', m.V.mem, addr - vb)[0] if False else int.from_bytes(uc.mem_read(addr, 4), 'little')
            n = width.get(w >> 26)
            if n is None:
                return
            off = w & 0xffff
            if off & 0x8000:
                off -= 0x10000
            a = (uc.reg_read(regs[(w >> 21) & 31]) + off) & 0xffffffff
            if any(x <= a < y for x, y in com) or HEAP <= a < HEAP + emu.HEAP_SIZE or CALIB <= a < CALIB + emu.CALIB_SIZE:
                m.seen[(a, n)] = 1
        m.uc.hook_add(UC_HOOK_CODE, hook, begin=vb, end=vb + m.V.size - 1)
        return m

    def read_set(self, pm, s):
        """(addr, width) of the firmware-state words and (reg, width) of the ISP registers
        the vendor function reads when replayed from snapshot s"""
        pm.seen = {}
        pm.read_trace = []
        self.run_side(pm, s, 'V')
        regs = pm.read_trace
        pm.read_trace = None
        return sorted(pm.seen), sorted(set(regs))

    @staticmethod
    def mutate_value(r, old, w):
        mx = (1 << (8 * w)) - 1
        k = r.choice((0, 1, 2, 3, 7, 10)) if os.environ.get('MUTMOD') else r.randrange(12)
        if k == 0:
            v = 0
        elif k == 1:
            v = 1
        elif k == 2:
            v = old + 1
        elif k == 3:
            v = old - 1
        elif k == 4:
            v = mx
        elif k == 5:
            v = mx >> 1
        elif k == 6:
            v = (mx >> 1) + 1
        elif k == 7:
            v = r.randrange(256)
        elif k == 8:
            v = old ^ (1 << r.randrange(8 * w))
        elif k == 9:
            v = r.randrange(1 << r.randint(1, 8 * w))
        elif k == 10:
            v = old + r.randint(-16, 16)
        else:
            v = r.getrandbits(8 * w)
        return v & mx

    @staticmethod
    def known_mut(fn, d):
        """issue number when every difference of this mutation is a filed finding (KNOWN_MUT)"""
        txt = ' ; '.join(d)
        for k, n in KNOWN_MUT.items():
            if fn == k or ('trap(teq V:%s+' % k) in txt:
                return n
        return None

    @staticmethod
    def isolated(fn):
        """run fn in a forked child and return its (picklable) result; None when the child died
        (unicorn segfaults on some mutated states: a crash costs one iteration, not the run)"""
        import pickle
        sys.stdout.flush()
        sys.stderr.flush()
        rd, wr = os.pipe()
        pid = os.fork()
        if pid == 0:
            try:
                os.close(rd)
                data = pickle.dumps(fn())
                with os.fdopen(wr, 'wb') as f:
                    f.write(data)
            except BaseException:
                import traceback
                traceback.print_exc()
            finally:
                os._exit(0)
        os.close(wr)
        with os.fdopen(rd, 'rb') as f:
            data = f.read()
        os.waitpid(pid, 0)
        return pickle.loads(data) if data else None

    def mutfuzz(self, snaps, n, seed=5, only=None, per_fn_snaps=4):
        """mutation fuzz: replay recorded calls with a few of the words the vendor function reads
        (firmware state, calibration, ISP registers) replaced by boundary/random values"""
        import random
        m = self.machine()
        pm = self.probe_machine()
        by = {}
        for s in snaps:
            by.setdefault(s.fn, []).append(s)
        res = {}
        for fn in sorted(by):
            if only and fn not in only:
                continue
            if fn in self.skip_mut:
                continue
            print('MUT start %s' % fn, file=sys.stderr, flush=True)
            ss = by[fn]
            pick = sorted({0, 1, len(ss) // 2, len(ss) - 1} & set(range(len(ss))))[:per_fn_snaps]
            bases = []
            for i in pick:
                mem, regs = self.read_set(pm, ss[i])
                bases.append((ss[i], mem, regs))
            argmode = not any(b[1] or b[2] for b in bases)
            if argmode and not any(s.args):
                print('MUTSKIP %s (reads no state, no arguments)' % fn, flush=True)
                continue
            r = random.Random(seed)
            out = []
            ncrash = 0
            nknown = {}
            self.maxins = 3_000_000
            t0 = time.time()
            for it in range(n):
                if time.time() - t0 > self.mut_budget:
                    break
                s0, mem, regs = bases[it % len(bases)]
                edits = []
                for _ in range(r.choice((1, 1, 1, 2, 2, 3, 5)) if (mem or regs) else 0):
                    if regs and (not mem or r.random() < 0.3):
                        edits.append(('R',) + r.choice(regs) + (r.getrandbits(30),))
                    else:
                        edits.append(('M',) + r.choice(mem) + (r.getrandbits(30),))
                # integer arguments (not pointers): replaced by boundary/random values
                newargs, aed = list(s0.args), []
                if argmode or r.random() < 0.25:
                    ints = [k for k, v in enumerate(newargs[:4]) if not (0x10000000 <= v < 0x80000000)]
                    for k in (r.sample(ints, min(len(ints), r.choice((1, 1, 2)))) if ints else []):
                        newargs[k] = self.mutate_value(random.Random(r.getrandbits(30)), newargs[k], 4)
                        aed.append('A%d=%x' % (k, newargs[k]))
                if not edits and not aed:
                    continue

                if os.environ.get('MUTIT') and it != int(os.environ['MUTIT']):
                    continue                # replay one iteration only (crash hunting)

                applied = []

                def patch(mm, a0, edits=edits, applied=applied):
                    for kind, a, w, sd in edits:
                        if os.environ.get('MUTONLY') and kind != os.environ['MUTONLY']:
                            continue
                        rr = random.Random(sd)
                        if kind == 'R':
                            old = int.from_bytes(mm.env.regs[a:a + w], 'little')
                            nv = self.mutate_value(rr, old, w)
                            applied.append('R%x:%x->%x' % (a, old, nv))
                            mm.env.regs[a:a + w] = nv.to_bytes(w, 'little')
                        else:
                            old = int.from_bytes(bytes(mm.uc.mem_read(a, w)), 'little')
                            v = self.mutate_value(rr, old, w)
                            if w == 4 and any(emu.VBASE <= x < emu.OBASE + 0x1000000 or HEAP <= x < STACK + STACK_SIZE
                                              or emu.STUB <= x < emu.RET + 0x1000 or emu.CALIB <= x < emu.HEAP
                                              for x in (old, v)):
                                continue            # never fabricate or destroy pointers (incl. the sensor callback stubs)
                            applied.append('M%x:%x->%x' % (a, old, v))
                            mm.uc.mem_write(a, v.to_bytes(w, 'little'))
                e = Snap()
                for k in Snap.__slots__:
                    setattr(e, k, getattr(s0, k, None))
                e.patch, e.label = patch, 'mut%d:%s' % (it, ' '.join(['%s%x/%d' % x[:3] for x in edits] + aed))
                e.args = newargs
                if os.environ.get('MUTTRACE'):
                    print('MUTIT %s %s' % (fn, e.label), file=sys.stderr, flush=True)
                if os.environ.get('MUTWATCH'):      # MUTWATCH=0xaddr,0xaddr: print every write to these (V and O)
                    from unicorn import UC_HOOK_MEM_WRITE
                    for wa in os.environ['MUTWATCH'].split(','):
                        wa = int(wa, 0)
                        m.uc.hook_add(UC_HOOK_MEM_WRITE, lambda uc, ac, ad, sz, val, ud: print(
                            'WR %08x <- %x (%d) at %s' % (ad, val & 0xffffffff, sz, m.where(uc.reg_read(emu.UC_MIPS_REG_PC))),
                            file=sys.stderr, flush=True), begin=wa, end=wa + 3)
                if os.environ.get('MUTPC'):         # instruction trace of the crashing iteration
                    from unicorn import UC_HOOK_CODE
                    m.uc.hook_add(UC_HOOK_CODE, lambda uc, ad, sz, ud: print('PC %s' % m.where(ad), file=sys.stderr, flush=True))
                def one():
                    del applied[:]
                    dd = self.compare(m, self.run_side(m, e, 'V'), self.run_side(m, e, 'O'), fn)
                    return dd, list(applied)
                d = self.isolated(one)
                if d is not None:
                    d, ap = d
                    if d:
                        e.label += ' {%s}' % ' '.join(sorted(set(ap)))
                if d and not all(x.startswith('ret ') for x in d):      # a return value alone is not counted
                    kn = self.known_mut(fn, d)
                    if kn:
                        if os.environ.get('MUTALL'):
                            print('   known #%d: %s | %s' % (kn, e.label, ' ; '.join(d[:3])[:300]), flush=True)
                        nknown[kn] = nknown.get(kn, 0) + 1
                        d = []
                if d is None:
                    ncrash += 1
                    d = []
                    print('MUTCRASH %s %s (unicorn died, iteration skipped)' % (fn, e.label), flush=True)
                out.append((e, d))
            self.maxins = 50_000_000
            res[fn + '[mut]'] = out
            nb = sum(1 for e, d in out if d and not all(x.startswith('ret ') for x in d))
            print('MUTRES %s %d/%d differing%s%s' % (fn, nb, len(out), (', %d crashed' % ncrash) if ncrash else '',
                  (', known: ' + ' '.join('#%d x%d' % kv for kv in sorted(nknown.items()))) if nknown else ''), flush=True)
            if os.environ.get('MUTALL'):
                for e, d in out:
                    if d and not all(x.startswith('ret ') for x in d):
                        print('   diff: %s | %s' % (e.label, ' ; '.join(d[:3])[:400]), flush=True)
            if nb:
                e, d = [(e, d) for e, d in out if d and not all(x.startswith('ret ') for x in d)][0]
                print('   first: %s | %s' % (e.label, ' ; '.join(d[:3])[:300]), flush=True)
        return res

    def replay(self, snaps):
        m = self.machine()
        res = {}
        for s in snaps + self.edge_snaps(snaps):
            a = self.run_side(m, s, 'V')
            b = self.run_side(m, s, 'O')
            d = self.compare(m, a, b, s.fn)
            res.setdefault(s.fn + ('[edges]' if s.label else ''), []).append((s, d))
        return res


def void_functions():
    """functions our source defines as void (their v0 is not compared)"""
    import re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../driver/t20/tx_isp_t20_firmware.c')
    txt = open(src).read()
    return set(re.findall(r'^(?:static\s+)?(?:inline\s+)?void\s+\**\s*(\w+)\s*\(', txt, re.M)) - \
        set(re.findall(r'^(?:static\s+)?(?:inline\s+)?void\s*\*\s*(\w+)\s*\(', txt, re.M))


def stack_arg_counts():
    """number of stack-passed argument words (arguments past a0-a3) of the
    functions our source defines with more than four arguments"""
    import re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../driver/t20/tx_isp_t20_firmware.c')
    out = {}
    for m in re.finditer(r'^(?:static\s+)?(?:inline\s+)?[\w\s\*]+?\b(\w+)\s*\(([^;{)]*)\)\s*\{',
                         open(src).read(), re.M):
        args = [a for a in m.group(2).split(',') if a.strip() and a.strip() != 'void']
        if len(args) > 4:
            out[m.group(1)] = len(args) - 4
    return out


def fmt(v):
    return ('0x%x' % v) if isinstance(v, int) else str(v)


# findings the DIS-on scenario reaches (open issues): reported, not counted
KNOWN_GEN = {}     # #602 fixed (was: adjust_exposure dereferenced NULL)
KNOWN_DIS = {}     # #582 fixed (was: gauss v2, dis_analyze, dis_analyze_stats, apical_fw_process, apical_isp_process_events)
# findings of the mutation fuzz (--mut) with a filed issue: a diff of that function, or a vendor trap located in it
# (reached through apical_fw_process / apical_isp_process_events), is listed as known and not counted
KNOWN_MUT = {'iridix_control_strength_calculate': 598, 'validate_complexity_test': 599,
             'awb_range_low': 600}     # #601 fixed (was: cmos_long_exposure_update)
# replay of a function whose state lives in partner-less statics (vendor sbss vs our array): not a finding
STATIC_LAYOUT = ()     # was iir_filter_v2: its statics are now paired (SPLIT_STATICS)
VONLY_STATICS = ('sp.9007', 'fsp.9008', 'csp.9009')     # vendor .sbss statics of iir_filter_v2
# ours keeps them in one array: replays copy the vendor values in (word index in our array)
SPLIT_STATICS = {'iir_filter_v2_state': (('fsp.9008', 0), ('csp.9009', 2), ('sp.9007', 5))}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('vendor')
    ap.add_argument('ours')
    ap.add_argument('--calib')
    ap.add_argument('--sdk', help='external/ingenic-sdk/3.10.14/isp/t20 (night calibration bank ids)')
    ap.add_argument('--only')
    ap.add_argument('--short', action='store_true')
    ap.add_argument('--sys', action='store_true', help='(default, kept for compatibility)')
    ap.add_argument('--fuzz', type=int, default=3000, help='random calls per pure helper (0: off)')
    ap.add_argument('--no-scenario', action='store_true')
    ap.add_argument('--skip-mut', default='', help='comma list of functions to leave out of the mutation fuzz')
    ap.add_argument('--mut', type=int, default=0, help='mutation-fuzz replays per function (0: off)')
    ap.add_argument('--dis', action='store_true',
                    help='append the DIS-on scenario (dis_steps); functions with a filed finding are listed as KNOWN '
                         'and not counted (KNOWN_DIS), remove the entry when the issue is fixed')
    ap.add_argument('--gen', action='store_true',
                    help='append the general-FSM scenario (gen_steps): general_frame_start -> adjust_exposure')
    ap.add_argument('--traps', action='store_true',
                    help='model the MIPS trap instructions (default only with --mut / --gen: the hooks double the run time)')
    ap.add_argument('--no-seq', action='store_true',
                    help='apical_custom_sequence() returns NULL (no ISP init sequence)')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    emu.selftest_traps()
    D = Diff(a.vendor, a.ours, a.calib, a.v, a.sdk)
    D.seq = not a.no_seq
    D.traps = bool(a.mut or a.traps or a.gen)
    D.skip_mut = set(filter(None, a.skip_mut.split(',')))
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
        api.update(fuzz.fuzz_sbus(m, a.fuzz))
        api.update(fuzz.fuzz_crop(m, a.fuzz))
        api.update(fuzz.fuzz_dis(m, a.fuzz))
        on = lambda n: not a.only or n in a.only.split(',')
        if on('sharpening_update'):
            api.update(fuzz.fuzz_sharp(m, max(100, a.fuzz // 10)))
        if on('au_read_histogram'):
            api.update(fuzz.fuzz_hist(m, max(20, a.fuzz // 20)))
        api.update(fuzz.fuzz_exp(m, a.fuzz))
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
    steps = scenario.scenario(not a.short, a.dis, a.gen)
    dis0 = next((i for i, st in enumerate(steps) if st[0] == 'mark'), len(steps))
    gen0 = next((i for i, st in enumerate(steps) if st[:2] == ('mark', 'gen')), len(steps))
    snaps = D.record(steps, only)
    if a.dis and KNOWN_DIS:
        known = [x for x in D.sys_log if x[0] >= dis0]
        D.sys_log = [x for x in D.sys_log if x[0] < dis0]
        print('SYS steps diverging in the DIS scenario (KNOWN %s): %d' % (sorted(set(KNOWN_DIS.values())), len(known)))
    if a.gen and KNOWN_GEN:
        known = [x for x in D.sys_log if x[0] >= gen0]
        D.sys_log = [x for x in D.sys_log if x[0] < gen0]
        print('SYS steps diverging in the general-FSM scenario (KNOWN %s): %d' % (sorted(set(KNOWN_GEN.values())), len(known)))
    for i, st, lv, lo in D.sys_log[:(400 if a.v else 8)]:
        k = 0
        while k < min(len(lv), len(lo)) and lv[k] == lo[k]:
            k += 1
        print('SYS step %d %s: first diff at event %d/%d: V=%s O=%s' % (i, st, k, len(lv), lv[k:k + 4], lo[k:k + 4]))
    print('SYS steps with divergent register/sensor traffic: %d' % len(D.sys_log))
    res = D.replay(snaps)
    if a.mut:
        res.update(D.mutfuzz(snaps, a.mut, only=only))
    ndiff = 0
    for fn in sorted(res):
        rs = res[fn]
        # a differing return value alone is reported but not counted: the
        # vendor leaves stale registers in v0 of void functions, and a
        # return value that matters shows up in the caller's comparison
        bad = [(s, d) for s, d in rs if d and not all(x.startswith('ret ') for x in d)]
        if a.gen and bad and fn in KNOWN_GEN:
            print('KNOWN %-44s differing=%d (#%d)' % (fn, len(bad), KNOWN_GEN[fn]))
            continue
        if a.dis and bad and (fn in KNOWN_DIS or fn in STATIC_LAYOUT):
            print('KNOWN %-44s differing=%d (%s)' % (fn, len(bad), '#%d' % KNOWN_DIS[fn] if fn in KNOWN_DIS else 'state in partner-less statics'))
            continue
        soft = [(s, d) for s, d in rs if d and all(x.startswith('ret ') for x in d)]
        tag = 'DIFF' if bad else ('ret ' if soft else 'same')
        print('%s %-44s calls=%-6d compared=%-3d differing=%d' % (tag, fn, D.counts.get(fn, 0), len(rs), len(bad)))
        if bad or (soft and a.v):
            ndiff += bool(bad)
            for s, d in (bad or soft)[: (99 if a.v else 2)]:
                print('    step %d call #%d%s args=%s' % (s.step, s.callno, ' edge ' + s.label if s.label else '',
                                                  [hex(x) for x in s.args]))
                for line in d[:8]:
                    print('      ' + line)
    never = [n for n in D.names if n not in res]
    print('functions compared: %d, differing: %d, never called by the scenario: %d' % (len(res), ndiff, len(never)))
    if a.v:
        print('never called: ' + ' '.join(never))
    mt = D.machine() if D.traps else None
    if mt:
        print('trap model: %d trap instructions hooked (vendor %d, ours %d)' % (
            len(mt.trapat), sum(1 for x in mt.trapat if mt.V.contains(x)), sum(1 for x in mt.trapat if mt.O.contains(x))))
    print('SUMMARY scenario steps diverging: %d, replayed functions differing: %d, fuzzed helpers differing: %d'
          % (len(D.sys_log), ndiff, nfz))
    return 1 if ndiff or nfz or D.sys_log else 0


if __name__ == '__main__':
    sys.exit(main())
