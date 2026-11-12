#!/usr/bin/env python3
"""diff_t30_lift.py VENDOR.ko OPEN.o [--n N] [--only NAME]: T30 lift defects found by -Os (Lu-Fi/openimp#274).

Runs functions of the SDK-built vendor tx-isp-t30.ko (4ea12e1f) and of our driver/t30/tx_isp_t30_recovered.o
(the unlinked object keeps the local symbols) in Unicorn on the same pseudo-random inputs:

  sinter_strength / temper / temper_strength   return, *out, register writes, stab
  general_frame_start                           FSM bytes, adjust_exposure call
  sensor/cmos/color_matrix/iridix_fsm_switch_state  ordered callee log, FSM words
  crop_fsm_process_event                        return, state, callee log
  apical_sbus_write_u8                          bus callback (bus, addr, data, size)
  awb_normalise                                 +0x760..+0x76c, +0x18/+0x1a, stab[49]/[50]
  apical_event_queue_push                       queue head, _qlock1
  luts_fetch / AWB_mesh / AWB_mesh_LUT / mesh_AWB_getKnownSourceLight_weight_LUT / compute_weight
  awb_calc_avg_weighted_gr_gb_mesh              every FSM byte, fifo state (vendor rg_avg/gb_avg)
  ispcore/ncu_pad_event_handle, mscaler_frame_channel_streamoff   gp = thread_info: preempt_count, flags,
                                                unlock sequence, heap, GET_FORMAT buffer (#371)
  tx_isp_unlocked_ioctl                         user vs kernel argument: return, copies to user (addr_limit, #371)

Callees outside the function under test are hooked at their entry in both modules and logged; the
calibration getters return synthetic tables (no vendor data).  Return values are compared only where the
OEM function returns one.  Exit 1 on any difference.
"""
import sys, os, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu, HEAP, HEAP_SZ
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_SP

ALIAS = {"AWB_mesh": ("AWB_mesh.isra.1", "AWB_mesh_isra_1")}
CB = 0x71000000
M32 = 0xffffffff

def s32(v): return v - (1 << 32) if v & 0x80000000 else v

class Mod:
    def __init__(self, path, st):
        self.st = st; self.log = []; self.path = path
        e = self.e = KoEmu(path, hle={"printk": lambda e: 0, "isp_printf": lambda e: 0}, max_insns=3_000_000)
        e.uc.mem_map(CB, 0x1000)
        e.stubname[CB] = "bus_cb"; e.hle["bus_cb"] = self.rec("bus_cb", 4)
    def name(self, n):
        for c in ALIAS.get(n, (n,)):
            if c in self.e.symaddr: return c
        raise KeyError(n)
    def rec(self, n, nargs=2, ret=0):
        def h(e):
            a = [e.get_reg(r) for r in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3)][:nargs]
            self.log.append((n,) + tuple(a))
            return ret(e) if callable(ret) else ret
        return h
    def hook(self, n, fn):
        """hook n at its entry (defined in the module) and as an undefined-symbol stub"""
        self.e.hle[n] = fn
        if n in self.e.symaddr: self.e.stubname[self.e.symaddr[n]] = n
    def unhook(self, n):
        if n in self.e.symaddr: self.e.stubname.pop(self.e.symaddr[n], None)
        self.e.hle.pop(n, None)
    def call(self, n, *a, stack=()):
        del self.log[:]
        return self.e.call(self.name(n), *a, stack_args=stack)
    def stab(self): return self.e.symaddr["stab"]

def heapw(r):
    return [w for w in r.writes if HEAP <= w[0] < HEAP + HEAP_SZ]

# ---------------------------------------------------------------- register file (APICAL_READ/WRITE_32)
def regfile(m, regs, wlog):
    m.hook("APICAL_READ_32", lambda e: regs.get(e.get_reg(UC_MIPS_REG_A0), 0))
    def w(e):
        a, v = e.get_reg(UC_MIPS_REG_A0), e.get_reg(UC_MIPS_REG_A1); regs[a] = v; wlog.append((a, v)); return 0
    m.hook("APICAL_WRITE_32", w)

def t_nr(V, O, rnd, n):
    bad = 0
    for fn in ("sinter_strength", "temper", "temper_strength"):
        for k in range(n):
            sel = rnd.choice([0, 1, 0, 1, 2, 3, 0xff, 0x80])
            val = rnd.choice([3, 0x3b, 0x40, 5, 0, 0x80, 0xff, 0x100, 0x1234])
            r40 = rnd.choice([0, 0x200, 0x400, 0x600, rnd.getrandbits(32)])
            r280, r2c0 = rnd.getrandbits(32), rnd.getrandbits(32)
            sb = bytearray(rnd.getrandbits(8) for _ in range(60))
            for i in (10, 11): sb[i] = rnd.choice([0, 1, 1, 2])
            res = []
            for m in (V, O):
                regs = {0x40: r40, 0x280: r280, 0x2c0: r2c0}; wl = []
                regfile(m, regs, wl)
                m.e.heap_reset(); out = m.e.alloc(4, b"\xaa" * 4)
                m.e.write(m.stab(), sb)
                r = m.call(fn, 0, val, sel, out)
                res.append((r.stop, r.v0, m.e.read(out, 4), wl, m.e.read(m.stab(), 60)))
            if res[0] != res[1]:
                bad += 1
                if bad < 4: print("DIFF %s sel=%d val=%#x r40=%#x: vendor %s open %s" % (fn, sel, val, r40, res[0][:4], res[1][:4]))
    return bad

def t_general(V, O, rnd, n):
    bad = 0
    for k in range(n):
        fsm = bytearray(rnd.getrandbits(8) for _ in range(0x60))
        fsm[64] = rnd.choice([0, 1, 1, 1]); fsm[60] = rnd.choice([0, 1]); fsm[65] = rnd.choice([0, 19, 20, 21, rnd.getrandbits(8)])
        ctx = bytearray(0x1600)
        div = rnd.choice([1, 1000, 50000, rnd.getrandbits(20) + 1])
        scale = rnd.choice([0, 1, 10, 100, 1000])
        for i in range(60):
            struct.pack_into("<I", ctx, 0x338 + 4 * i, rnd.randrange(0, scale + 1))
        for i in range(10):
            struct.pack_into("<I", ctx, 0x710 + 4 * i, rnd.randrange(0, scale + 1))
        struct.pack_into("<I", ctx, 0x738, div); struct.pack_into("<I", ctx, 4, 0x4444)
        ctx[0x1527] = 0x77
        res = []
        for m in (V, O):
            m.hook("adjust_exposure", m.rec("adjust_exposure"))
            m.e.heap_reset(); c = m.e.alloc(len(ctx), ctx); f = m.e.alloc(0x60, fsm); m.e.write(f, struct.pack("<I", c))
            r = m.call("general_frame_start", f)
            res.append((r.stop, list(m.log), m.e.read(f, 0x60), m.e.read(c, len(ctx))))
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF general_frame_start mode=%d cnt=%d: vendor %s open %s" % (fsm[60], fsm[65], res[0][:2], res[1][:2]))
    return bad

FSM = {
    "sensor_fsm_switch_state": ["sensor_update_black", "apical_isp_raise_event", "sensor_stop_calibrate",
                                "sensor_start_calibrate", "sensor_hw_init", "sensor_sw_init"],
    "cmos_fsm_switch_state": ["cmos_update_wdr_mode", "apical_isp_raise_event", "cmos_long_exposure_update", "cmos_init",
                              "cmos_calc_target_gain", "cmos_analog_gain_update", "cmos_digital_gain_update",
                              "cmos_inttime_update", "cmos_antiflicker_update"],
    "color_matrix_fsm_switch_state": ["color_matrix_initialize", "color_matrix_update", "system_hw_interrupts_disable"],
    "iridix_fsm_switch_state": ["iridix_initialize", "iridix_update"],
}

# OEM callees without arguments (the a0 left over in the caller is not an argument)
VOIDF = {"sensor_start_calibrate", "sensor_stop_calibrate", "cmos_update_wdr_mode", "system_hw_interrupts_disable"}

def t_fsm(V, O, rnd, n):
    bad = 0
    for fn, callees in FSM.items():
        for k in range(n):
            old = rnd.choice(list(range(0, 10)) + [rnd.getrandbits(32)])
            new = rnd.choice(list(range(0, 10)) + [old])
            words = [rnd.getrandbits(32) for _ in range(0x40)]
            words[1] = old
            res = []
            for m in (V, O):
                for c in callees: m.hook(c, m.rec(c, 0 if c in VOIDF else 2))
                m.e.heap_reset()
                f = m.e.alloc(0x100, struct.pack("<64I", *words))
                inner = m.e.alloc(16, struct.pack("<4I", 0, 1, 0, 0))    # *(*(*fsm + 4) + 4) != 0: request_interrupt returns early
                top = m.e.alloc(16, struct.pack("<4I", 0, inner, 0, 0))
                m.e.write(f, struct.pack("<I", top))
                b = bytearray(m.e.read(f, 0x100)); b[0xde] = random.Random(k).choice([0, 1, 3]); b[0xf0] = random.Random(k + 7).choice([0, 1, 2])
                m.e.write(f, bytes(b))
                r = m.call(fn, f, new)
                norm = [tuple('fsm' if a == f else 'top' if a == top else a for a in x) for x in m.log]
                res.append((r.stop, norm, m.e.read(f + 4, 0xfc)))
            if res[0] != res[1]:
                bad += 1
                if bad < 6: print("DIFF %s %d->%d: vendor %s\n   open %s" % (fn, s32(old), s32(new), res[0][:2], res[1][:2]))
    return bad

def t_crop(V, O, rnd, n):
    bad = 0
    for k in range(n):
        ev = rnd.choice([6, 7, 0xf, 0x10, 0, 5, 8, 0x11, 0xffffffff])
        st = rnd.choice([0, 1, 2, 3, 4, 0xffffffff])
        res = []
        for m in (V, O):
            for c in ("crop_initialize", "crop_resolution_changed", "crop_fsm_process_state"):
                m.hook(c, m.rec(c, 1))
            m.e.heap_reset(); f = m.e.alloc(0x40, struct.pack("<II", 0x1234, st) + bytes(0x38))
            r = m.call("crop_fsm_process_event", f, ev)
            res.append((r.stop, r.v0, [tuple('f' if a == f else a for a in x) for x in m.log], m.e.read(f, 0x40)))
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF crop_fsm_process_event ev=%#x st=%#x: vendor %s open %s" % (ev, st, res[0], res[1]))
    return bad

def t_sbus(V, O, rnd, n):
    bad = 0
    for k in range(n):
        flags = rnd.getrandbits(12) | rnd.choice([0, 1, 1, 1])
        addr, data = rnd.getrandbits(32), rnd.getrandbits(32)
        res = []
        for m in (V, O):
            m.e.heap_reset(); bus = m.e.alloc(0x40, struct.pack("<I", flags) + bytes(16) + struct.pack("<I", CB) + bytes(0x28))
            r = m.call("apical_sbus_write_u8", bus, addr, data)
            res.append((r.stop, [tuple('bus' if a == bus else a for a in x) for x in m.log]))
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF apical_sbus_write_u8 flags=%#x addr=%#x: vendor %s open %s" % (flags, addr, res[0], res[1]))
    return bad

def log2ff(e):
    a, fi, fo = e.get_reg(UC_MIPS_REG_A0) & 0xffff, e.get_reg(UC_MIPS_REG_A1), e.get_reg(UC_MIPS_REG_A2)
    return (a * 0x9e37 + fi * 0x101 + fo * 7) & 0xfffff          # any deterministic function of the arguments

def t_normalise(V, O, rnd, n):
    bad = 0
    for k in range(n):
        fsm = bytearray(rnd.getrandbits(8) for _ in range(0x800))
        ctx = bytearray(rnd.getrandbits(8) for _ in range(0x100))
        sb = bytearray(rnd.getrandbits(8) for _ in range(60)); sb[12] = rnd.choice([0, 1])
        res = []
        for m in (V, O):
            m.hook("log2_fixed_to_fixed", log2ff)
            m.e.heap_reset(); c = m.e.alloc(0x100, ctx); f = m.e.alloc(0x800, fsm); m.e.write(f, struct.pack("<I", c))
            m.e.write(m.stab(), sb)
            r = m.call("awb_normalise", f)
            res.append((r.stop, m.e.read(f, 0x800), m.e.read(m.stab(), 60)))
        if res[0] != res[1]:
            bad += 1
            if bad < 4:
                d = [i for i in range(0x800) if res[0][1][i] != res[1][1][i]]
                print("DIFF awb_normalise stab12=%d stop=%s/%s fsm bytes differ at %s" % (sb[12], res[0][0], res[1][0], [hex(x) for x in d[:8]]))
    return bad

def t_queue(V, O, rnd, n):
    bad = 0
    for k in range(n):
        q = [rnd.choice([-1, 0, 3, 7, 15]), rnd.choice([-1, 0, 3, 7]), 0, rnd.choice([0, 8, 16])]
        newhead = rnd.choice([q[1], 4, 9]); ev = rnd.getrandbits(8); ql = rnd.getrandbits(16)
        res = []
        for m in (V, O):
            m.hook("apical_loop_buffer_write_u8", m.rec("write", 3, newhead & M32))
            qa = m.e.symaddr["_qlock1"]; m.e.write(qa, struct.pack("<I", ql))
            nb = m.e.read(qa + 4, 4)
            m.e.heap_reset(); b = m.e.alloc(16, struct.pack("<4i", *q))
            r = m.call("apical_event_queue_push", b, ev)
            res.append((r.stop, [tuple('q' if a == b else a for a in x) for x in m.log], m.e.read(b, 16), m.e.read(qa, 4), m.e.read(qa + 4, 4) == nb))
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF apical_event_queue_push: vendor %s open %s" % (res[0], res[1]))
    return bad

# ---------------------------------------------------------------- AWB mesh chain: synthetic calibrations
class Cal:
    """id -> (rows, cols, bytes); getters return guest pointers into the heap (same layout in both)"""
    def __init__(self, rnd, L, nz):
        self.t = {}; self.rnd = rnd
        ax = sorted(rnd.sample(range(40, 600), L)); ay = sorted(rnd.sample(range(40, 600), L))
        self.u16(0xe2, ax); self.u16(0xfb, ay)
        for i in (0xee, 0xd6, 0xd9):
            self.s16(i, [rnd.randrange(-50, 400) for _ in range(L * L)], L, L)
        rows = rnd.randrange(1, 6)
        mesh = []
        for _ in range(rows): mesh += [rnd.randrange(30, 620), rnd.randrange(30, 620), rnd.randrange(0, 99)]
        self.u16(0xef, mesh, rows, 3)
        ex = sorted(rnd.sample(range(10, 100000), 6)); self.u32(0x97, ex)
        self.u32(0xf3, [rnd.choice([60, 70, 90, 300, 800, 950, 1500, 2000, 6000]) for _ in range(6)])
        for i in (0xfa, 0xfc, 0xf4, 0xe7, 0xf6, 0xe3, 0xd7, 0xe1, 0xfe, 0xec, 0xf0, 0xe5):
            self.u16(i, [rnd.choice([0, 1, 5, 50, 100, 300, 1000, 2000])])
        for i in (0xfd, 0xd4, 0xf5, 0xf8):
            self.u16(i, [rnd.randrange(0, 120) for _ in range(rnd.randrange(1, 9))])
        self.u8(0x10b, [rnd.choice([0, 1, 1])]); self.u8(262, [rnd.choice([0, 1, 4, 8])])
        for i in (0xe0, 0xe4): self.u16(i, [rnd.randrange(50, 600) for _ in range(8)])
        for i in (0xdd, 0xf1, 0xd8): self.u16(i, [rnd.randrange(0, 8)])
        self.u16(0xf7, [rnd.choice([0, 500, 1000, 3000])]); self.u16(0xe6, [rnd.randrange(1, 300), rnd.randrange(1, 300), rnd.choice([0, 128, 256])])
    def put(self, i, data, rows, cols): self.t[i] = (rows, cols, data)
    def u16(self, i, v, rows=1, cols=None): self.put(i, struct.pack("<%dH" % len(v), *[x & 0xffff for x in v]), rows, cols or len(v))
    def s16(self, i, v, rows, cols): self.put(i, struct.pack("<%dh" % len(v), *v), rows, cols)
    def u32(self, i, v): self.put(i, struct.pack("<%dI" % len(v), *v), 1, len(v))
    def u8(self, i, v): self.put(i, bytes(v) + bytes(3), 1, len(v))
    def install(self, m):
        e = m.e; addr = {}
        for i in sorted(self.t): addr[i] = e.alloc(len(self.t[i][2]) + 8, self.t[i][2])
        a0 = lambda e: e.get_reg(UC_MIPS_REG_A0) & 0xffff
        ptr = lambda e: addr.get(a0(e), 0)
        for n in ("_GET_USHORT_PTR", "_GET_UINT_PTR", "_GET_UCHAR_PTR", "_GET_LUT_PTR", "_GET_MOD_ENTRY16_PTR"): m.hook(n, ptr)
        m.hook("_GET_LEN", lambda e: (lambda t: t[0] * t[1] if t else 0)(self.t.get(a0(e))))
        m.hook("_GET_ROWS", lambda e: (lambda t: t[0] if t else 0)(self.t.get(a0(e))))
        m.hook("_GET_COLS", lambda e: (lambda t: t[1] if t else 0)(self.t.get(a0(e))))
        m.hook("_GET_HDR_TABLE_INDEX", lambda e: e.get_reg(UC_MIPS_REG_A0) & 0xff)
        return addr

def fifo_syms(m):
    for r, b in (("t30_awb_fifo_r", "t30_awb_fifo_b"), ("rg_avg", "gb_avg")):   # open first: the .o has other rg_avg statics
        if r in m.e.symaddr: return m.e.symaddr[r], m.e.symaddr[b]
    raise KeyError("fifo state")

def run_pair(V, O, setup, fn, args_of, stack_of=lambda m, a: (), cmp=None):
    res = []
    for m in (V, O):
        m.e.heap_reset(); a = setup(m)
        r = m.call(fn, *args_of(m, a), stack=stack_of(m, a))
        res.append((r.stop,) + tuple(cmp(m, a, r)))
    return res

def t_mesh(V, O, rnd, n):
    bad = 0
    for k in range(n):
        L = rnd.randrange(2, 7); seed = rnd.getrandbits(32)
        x, y = rnd.randrange(0, 700), rnd.randrange(0, 700)
        lx, ly = rnd.randrange(0, 700), rnd.randrange(0, 700)
        def setup(m):
            c = Cal(random.Random(seed), L, 0); return c.install(m)
        nfd = len(Cal(random.Random(seed), L, 0).t[0xfd][2]) // 2
        step = (k % 4) * 17
        for fn, args, st in (
            ("AWB_mesh", lambda m, a: (x, y, a[0xe2], a[0xfb]), lambda m, a: (a[0xee],)),
            ("mesh_AWB_getKnownSourceLight_weight_LUT", lambda m, a: (0, x, y, lx), lambda m, a: (ly,)),
            ("luts_fetch", lambda m, a: (x, 100, 400, step), lambda m, a: (a[0xfd], nfd)),
        ):
            res = run_pair(V, O, setup, fn, args, st, cmp=lambda m, a, r: (r.v0, heapw(r)))
            if res[0][0] and res[0][0] == res[1][0]: continue       # both trap (OEM divide by zero)
            if res[0] != res[1]:
                bad += 1
                if bad < 6: print("DIFF %s L=%d x=%d y=%d: vendor %s open %s" % (fn, L, x, y, res[0][:2], res[1][:2]))
        # AWB_mesh_LUT directly (grid with random base/step)
        xb, xs, yb, ys = rnd.randrange(0, 300), rnd.choice([0, 1, 7, 20, -5]), rnd.randrange(0, 300), rnd.choice([0, 3, 15, 30])
        xm, ym = xb + 60, yb + 60
        sx = lambda v: v & M32
        res = run_pair(V, O, setup, "AWB_mesh_LUT", lambda m, a: (0, x, y, sx(xb)),
                       lambda m, a: (sx(xm), sx(xs), sx(yb), sx(ym), sx(ys), a[0xd9]), cmp=lambda m, a, r: (r.v0, heapw(r)))
        if not (res[0][0] and res[0][0] == res[1][0]) and res[0] != res[1]:
            bad += 1
            if bad < 6: print("DIFF AWB_mesh_LUT L=%d x=%d y=%d xs=%d ys=%d: vendor %s open %s" % (L, x, y, xs, ys, res[0][:2], res[1][:2]))
    return bad

def awb_fsm(rnd, nz):
    f = bytearray(rnd.getrandbits(8) for _ in range(0x800))
    for i in range(nz):
        struct.pack_into("<HHI", f, 0x58 + 8 * i, rnd.randrange(20, 650), rnd.randrange(20, 650),
                         rnd.choice([0, 0x80, 0x100, 0x101, 0x5000, rnd.getrandbits(32)]))
    struct.pack_into("<i", f, 0x784, rnd.randrange(1000, 20000))     # [0x1e1] max temperature
    struct.pack_into("<i", f, 0x788, rnd.randrange(0, 4000))         # [0x1e2] min temperature
    for o in (0x18, 0x1a): struct.pack_into("<H", f, o, rnd.choice([0, 1, 200, 600]))
    for o in (0x38, 0x3c): struct.pack_into("<I", f, o, rnd.choice([0, 1, 256, 700]))
    return f

def t_weight(V, O, rnd, n):
    bad = 0
    for k in range(n):
        nz = rnd.randrange(0, 40); fsm = awb_fsm(rnd, nz); struct.pack_into("<H", fsm, 0x10, nz)
        a1, a2 = rnd.randrange(0, 600), rnd.choice([0x100, 0, 100, 0x180, 0x200, 600])
        mask = bytes(rnd.choice([0, 1]) for _ in range(228)); wt = bytes(rnd.getrandbits(8) for _ in range(228))
        w = struct.pack("<228I", *[rnd.getrandbits(16) for _ in range(228)])
        def setup(m):
            e = m.e; return (e.alloc(0x800, fsm), e.alloc(228, mask), e.alloc(912, w), e.alloc(228, wt))
        res = run_pair(V, O, setup, "compute_weight", lambda m, a: (a1, a2, a[1], a[2]), lambda m, a: (a[3], a[0]),
                       cmp=lambda m, a, r: (heapw(r),))
        if not (res[0][0] and res[0][0] == res[1][0]) and res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF compute_weight a1=%d a2=%d n=%d: stop %s/%s" % (a1, a2, nz, res[0][0], res[1][0]))
    return bad

def t_calc(V, O, rnd, n):
    bad = skip = 0
    for k in range(n):
        seed = rnd.getrandbits(32); L = rnd.randrange(2, 7)
        rows, cols = rnd.choice([(1, 1), (3, 4), (5, 5), (8, 8), (15, 15), (15, 16)])
        nz = rows * cols; fsm = awb_fsm(rnd, min(nz, 225))
        ctx = bytearray(0x1600); struct.pack_into("<I", ctx, 0x11c, rnd.choice([0, 5, 50, 5000, 60000, 200000]))
        sb12 = rnd.choice([0, 0, 1]); fifo = rnd.choice([(0, 0), (300, 500), (rnd.getrandbits(20), rnd.getrandbits(20))])
        res = []
        for m in (V, O):
            m.e.heap_reset(); cal = Cal(random.Random(seed), L, nz); cal.install(m)
            m.hook("APICAL_READ_32", lambda e: (rows << 8) | cols if e.get_reg(UC_MIPS_REG_A0) == 0x870 else 0)
            c = m.e.alloc(0x1600, ctx); f = m.e.alloc(0x800, fsm); m.e.write(f, struct.pack("<I", c))
            sb = bytearray(m.e.read(m.stab(), 60)); sb[12] = sb12; m.e.write(m.stab(), bytes(sb))
            fr, fb = fifo_syms(m); m.e.write(fr, struct.pack("<I", fifo[0])); m.e.write(fb, struct.pack("<I", fifo[1]))
            r = m.call("awb_calc_avg_weighted_gr_gb_mesh", f)
            res.append((r.stop, m.e.read(f, 0x800), m.e.read(fr, 4) + m.e.read(fb, 4)))
        if res[0][0] and res[0][0] == res[1][0]: skip += 1; continue       # both trap (OEM divide by zero)
        if res[0] != res[1]:
            bad += 1
            if bad < 6:
                d = [i for i in range(0x800) if res[0][1][i] != res[1][1][i]]
                print("DIFF awb_calc nz=%d L=%d stab12=%d stop=%s/%s fifo %s/%s bytes %s" % (nz, L, sb12, res[0][0], res[1][0],
                      res[0][2].hex(), res[1][2].hex(), [hex(x) for x in d[:10]]))
    if skip: print("  awb_calc: %d of %d cases trap in both (OEM divide by zero), skipped" % (skip, n))
    return bad

# ---------------------------------------------------------------- gp = current_thread_info() (#371)
# The lift resolved the OEM gp-relative thread_info accesses (gp+0 task, +8 flags, +20 preempt_count, +24 addr_limit)
# to .sbss statics or 0.  Both modules run with $28 pointing at a fake thread_info; spin_unlock_irqrestore (OEM
# private_spin_unlock_irqrestore, ours inline irqrestore + preempt_enable) is compared as one "unlock".
GP_NB, GP_BS, USER_DS = 12, 0x400, 0x80000000
def gp_world(m, rnd):
    m.e.heap_reset(); ti = m.e.alloc(0x40); m.e.gp = ti
    blk = [m.e.alloc(GP_BS) for _ in range(GP_NB)]
    for b in blk:
        w = []
        for i in range(GP_BS // 4):
            c = rnd.random()
            if c < 0.3: w.append(0)
            elif c < 0.45: w.append(rnd.choice([1, 2, 3, 4, 5, 8, 0x20]))
            elif c < 0.85: w.append(rnd.choice(blk) + 4 * rnd.randrange(0, 64))
            else: w.append(b + 4 * i)                                       # empty list head
        m.e.write(b, struct.pack("<%dI" % len(w), *w))
    m.e.write(ti, struct.pack("<8I", 0x5a5a0000, 0, rnd.choice([0, 4]), 0, 0, rnd.choice([0, 1, 3]), USER_DS, 0))
    return ti, blk

def gp_hooks(m):
    lg = m.log
    m.hook("arch_local_irq_save", lambda e: (lg.append(("irqsave",)), 0x55)[1])
    m.hook("arch_local_irq_restore", lambda e: (lg.append(("unlock", e.get_reg(UC_MIPS_REG_A0))), 0)[1])
    def unlock(e):
        ti = m.e.gp; pc = struct.unpack("<i", e.read(ti + 20, 4))[0]
        e.write(ti + 20, struct.pack("<i", pc - 1)); lg.append(("unlock", e.get_reg(UC_MIPS_REG_A1))); return 0
    m.hook("private_spin_unlock_irqrestore", unlock)
    def mset(e):
        a, c, k = (e.get_reg(r) for r in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2))
        lg.append(("memset", a, c, k)); e.write(a, bytes([c & 0xff]) * k); return a
    m.hook("memset", mset)
    for f in ("printk", "isp_printf", "__might_sleep", "msleep", "private_msleep", "seq_printf", "preempt_schedule"):
        m.hook(f, lambda e: 0)
    m.hook("tx_isp_reg_set", m.rec("tx_isp_reg_set", 4))
    m.hook("tx_isp_send_event_to_remote", m.rec("tx_isp_send_event_to_remote", 2))
    regfile(m, {}, [])

def gp_case(m, r, kind):
    ti, b = gp_world(m, r)
    W = lambda a, o, v: m.e.write(a + o, struct.pack("<I", v & M32))
    B = lambda a, o, v: m.e.write(a + o, bytes([v]))
    if kind in ("isp_qbuf", "isp_free"):          # ispcore QUEUE_BUFFER / FREE_BUFFERS: lock + list ops (OEM c5dc)
        B(b[0], 5, 1); W(b[0], 20, r.choice([0, 0, 0x20])); W(b[0], 32, b[1])
        for o in (112, 120): W(b[1], o, b[1] + o); W(b[1], o + 4, b[1] + o)
        return "ispcore_pad_event_handle", (b[0], 0x3000005 if kind == "isp_qbuf" else 0x3000007, b[2]), True
    if kind == "isp_fmt":                           # GET_FORMAT: OEM c684..c694 store to the a2 buffer
        B(b[0], 5, 1); W(b[0], 32, b[1]); W(b[1], 88, b[3]); W(b[3], 352, 1); W(b[3], 336, r.randrange(0, 4))
        return "ispcore_pad_event_handle", (b[0], 0x3000001, b[2]), True
    if kind == "ncu_qbuf":                          # ncu QUEUE_BUFFER (OEM 12f1c): preempt_count, 64-bit frame count
        B(b[0], 5, 1); W(b[0], 0, b[4]); W(b[0], 32, b[1]); W(b[4], 208, b[5]); W(b[5], 20, r.choice([0, 0x20]))
        W(b[1], 412, r.choice([0, b[6]])); W(b[1], 372, r.choice([4, 3]))
        return "ncu_pad_event_handle", (b[0], 0x3000005, b[2]), True
    if kind in ("ncu_dq", "ncu_qb2"):              # ncu list head +404 (#375: the lift used s0 + 404 on a typed s0)
        W(b[1], 372, r.choice([4, 4, 3])); W(b[1], 184, b[3]); W(b[3], 4, r.choice([0x10000, 0x10000, 0]))
        W(b[1], 416, r.choice([0, 0, 1])); h = b[1] + 404
        if r.random() < 0.5: W(b[1], 404, h); W(b[1], 408, h)
        else: W(b[1], 404, b[7]); W(b[1], 408, b[7]); W(b[7], 0, h); W(b[7], 4, h)
        B(b[0], 5, 1); W(b[0], 32, b[1])
        if kind == "ncu_dq":
            W(b[0], 20, r.choice([0, 0, 0x20]))
            return "ncu_pad_event_handle", (b[0], 0x3000006, r.choice([b[2], 0])), True
        W(b[0], 0, b[4]); W(b[4], 208, b[5]); W(b[5], 20, r.choice([0, 0x20])); W(b[1], 412, r.choice([0, b[6]]))
        return "ncu_pad_event_handle", (b[0], 0x3000005, b[2]), True
    # mscaler_frame_channel_streamoff (OEM 1677c, void): lock, poll, list drain, memset tail call
    B(b[0], 7, r.choice([4, 4, 3])); W(b[0], 32, b[1]); W(b[1], 100, b[2]); W(b[1], 80, r.choice([4, 3]))
    W(b[1], 76, r.randrange(0, 3)); W(b[2], 184, b[3]); W(b[1], 124, b[1] + 124)
    W(b[1], 12, r.choice([0x34524742, 0x50424752, 0]))
    return "mscaler_frame_channel_streamoff.isra.0", (b[0],), False

GP_KINDS = ("isp_qbuf", "isp_free", "isp_fmt", "ncu_qbuf", "msc_off", "ncu_dq", "ncu_qb2")
def t_gp_lock(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})          # fresh modules: no hooks/state of the other groups
    bad = 0
    for k in range(n):
        kind = GP_KINDS[k % len(GP_KINDS)]; seed = rnd.getrandbits(32); res = []
        for m in (V, O):
            r0 = random.Random(seed); fn, args, has_ret = gp_case(m, r0, kind); gp_hooks(m)
            nm = fn if fn in m.e.symaddr else fn.replace(".", "_"); del m.log[:]
            r = m.e.call(nm, *args)
            res.append((r.stop, r.v0 if has_ret and not r.stop else None, list(m.log), m.e.read(m.e.gp, 0x20),
                        m.e.read(HEAP, 0x40 + GP_NB * GP_BS)))
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF %s: vendor %s open %s" % (kind, res[0][:4], res[1][:4]))
    return bad

# tx_isp_unlocked_ioctl with a user / kernel argument (addr_limit USER_DS): OEM access_ok() on gp+24
UACCESS_CMDS = (0x40045626, 0x80045612, 0x80045613, 0x800456d0, 0x800456d3, 0x800456d4, 0x8038564f, 0x805056c1,
                0x805056c2, 0xc0045627, 0xc0045628, 0xc0385650, 0xc050561a)
def t_uaccess(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})          # fresh modules: no hooks/state of the other groups
    bad = 0
    for k in range(n):
        cmd = UACCESS_CMDS[k % len(UACCESS_CMDS)]; kind = rnd.choice(["user", "kernel"])
        ub0 = bytes(rnd.getrandbits(8) for _ in range(0x100)); res = []
        for m in (V, O):
            m.e.heap_reset(); ti = m.e.alloc(0x40, struct.pack("<8I", 0x5a5a0000, 0, 0, 0, 0, 0, USER_DS, 0)); m.e.gp = ti
            cp = []
            def cu(e, cp=cp):
                to, frm, cnt = (e.get_reg(r) for r in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2))
                try: e.write(to, e.read(frm, cnt)); e.set_reg(UC_MIPS_REG_A2, 0)
                except Exception: e.set_reg(UC_MIPS_REG_A2, cnt)
                if HEAP <= to < HEAP + HEAP_SZ: cp.append((to - HEAP, cnt))       # copies to user memory
                return 0
            m.hook("__copy_user", cu)
            for f in ("printk", "isp_printf", "__might_sleep"): m.hook(f, lambda e: 0)
            dev = m.e.alloc(0x800); filp = m.e.alloc(0x100); m.e.write(filp + 112, struct.pack("<I", dev + 12))
            ub = m.e.alloc(0x100, ub0)
            r = m.call("tx_isp_unlocked_ioctl", filp, cmd, ub if kind == "user" else 0x80001000)
            res.append((r.stop, s32(r.v0) if not r.stop else None, cp, m.e.read(ti, 0x20)))   # data: our ioctl wrappers
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF tx_isp_unlocked_ioctl cmd=%#x %s: vendor %s open %s" % (cmd, kind, res[0][:3], res[1][:3]))
    return bad

# ---------------------------------------------------------------- typed-pointer byte arithmetic (#375)
def fname(m, n): return n if n in m.e.symaddr else n.replace(".", "_")

def t375_world(m, rnd, regs):
    m.e.heap_reset(); ti = m.e.alloc(0x40, struct.pack("<8I", 0x5a5a0000, 0, 0, 0, 0, 0, USER_DS, 0)); m.e.gp = ti
    wl = []; regfile(m, regs, wl); del m.log[:]
    for f in ("printk", "isp_printf", "__might_sleep"): m.hook(f, lambda e: 0)
    def cu(e):
        to, frm, cnt = (e.get_reg(r) for r in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2))
        try: e.write(to, e.read(frm, cnt)); e.set_reg(UC_MIPS_REG_A2, 0)
        except Exception: e.set_reg(UC_MIPS_REG_A2, cnt)
        m.log.append(("copy", to, cnt)); return 0
    m.hook("__copy_user", cu)
    return wl

def c_roi(m, r, k):                     # ae_roi.part.1 / awb_roi.part.2: zone weight window
    fn, sreg, zb = (("ae_roi.part.1", 0x830, 0x1e00), ("awb_roi.part.2", 0x870, 0x1f00))[k & 1]
    rows, cols = r.randrange(1, 16), r.randrange(1, 16)
    regs = {sreg: (rows << 8) | cols | (r.getrandbits(16) << 16)}
    for a in range(zb, zb + 0x100, 4): regs[a] = r.getrandbits(32)
    wl = t375_world(m, r, regs)
    ctx = m.e.alloc(0x40, bytes(r.choice([0, 0, 1]) if i in (0x12, 0x14) else 0 for i in range(0x40)))
    api = m.e.alloc(0x1000, bytes(0x1000)); m.e.write(api + 4, struct.pack("<I", ctx)); st = m.e.alloc(4, b"\0" * 4)
    x0, x1, y0, y1 = (r.randrange(0, 256) for _ in range(4))
    if r.random() < 0.8: x0, x1, y0, y1 = min(x0, x1), max(x0, x1) + 1, min(y0, y1), max(y0, y1) + 1
    arg = ((x0 & 0xff) << 24 | (y0 & 0xff) << 16 | (x1 & 0xff) << 8 | (y1 & 0xff))
    rr = m.e.call(fname(m, fn), api, arg, st)
    return rr.stop, rr.v0, wl, m.e.read(api, 0x1000), m.e.read(st, 4)

def c_vflip(m, r, k):                   # orientation_vflip: line offsets 0xb08..0xb20
    regs = {0xb04: r.getrandbits(32), 0xb1c: r.getrandbits(32), 0xb20: r.choice([r.getrandbits(32), r.randrange(0, 64), -r.randrange(1, 64) & M32])}
    wl = t375_world(m, r, regs)
    ctx = m.e.alloc(0x40, bytes(r.choice([0, 1]) if i == 0x14 else 0 for i in range(0x40)))
    api = m.e.alloc(0x40, bytes(0x40)); m.e.write(api + 4, struct.pack("<I", ctx)); out = m.e.alloc(4, b"\xaa" * 4)
    rr = m.e.call("orientation_vflip", api, r.choice([0xc, 0xd, 0xc, 0xd, 7]), r.choice([0, 0, 0, 1, 2]), out)
    return rr.stop, rr.v0, wl, m.e.read(ctx, 0x40), m.e.read(out, 4)

def c_gain(m, r, k):                    # gain_log2: (g0 + g1 + g2 + 0x7fff) >> 16
    t375_world(m, r, {})
    api = m.e.alloc(0x300, bytes(r.getrandbits(8) for _ in range(0x300))); out = m.e.alloc(4, b"\xaa" * 4)
    rr = m.e.call("gain_log2", api, 0, r.choice([0, 1, 1, 2, 0x101]), out)
    return rr.stop, rr.v0, m.e.read(out, 4)

def c_apibuf(m, r, k):                  # get_apical_api_buffer: &apical_api_buffer
    t375_world(m, r, {})
    rr = m.e.call("get_apical_api_buffer")
    return rr.stop, rr.v0 == m.e.symaddr["apical_api_buffer"]

def c_release(m, r, k):                 # ispcore_sensor_ops_release_all_sensor: child callbacks
    t375_world(m, r, {}); core = m.e.alloc(0x80, bytes(0x80))
    for i in range(16):
        if r.random() < 0.5: continue
        sd = m.e.alloc(0x100, bytes(0x100)); ops = m.e.alloc(0x10, bytes(0x10)); so = m.e.alloc(0x10, bytes(0x10))
        m.e.write(sd + 0xc4, struct.pack("<I", ops)); m.e.write(core + 0x38 + 4 * i, struct.pack("<I", sd))
        if r.random() < 0.8: m.e.write(ops + 0xc, struct.pack("<I", so))
        if r.random() < 0.8:
            cb = CB + 0x100 + 0x10 * i; ret = r.choice([0, 0, -515, -515, -22, 5])
            m.e.stubname[cb] = "rel%d" % i; m.e.hle["rel%d" % i] = m.rec("rel%d" % i, 1, ret)
            m.e.write(so, struct.pack("<I", cb))
    rr = m.e.call("ispcore_sensor_ops_release_all_sensor", core)
    return rr.stop, None if rr.stop else s32(rr.v0), list(m.log)

def c_weight(m, r, k):                  # apical_isp_ae_weight_s_attr: 15 x 15 zone bytes from user memory
    regs = {a: r.getrandbits(32) for a in range(0x1e00, 0x1f00, 4)}
    wl = t375_world(m, r, regs)
    ub = m.e.alloc(0x100, bytes(r.getrandbits(8) for _ in range(0x100)))
    rr = m.e.call(fname(m, "apical_isp_ae_weight_s_attr.isra.0"), ub)
    return rr.stop, wl

def c_dis(m, r, k):                     # dis_update_stats: rows mask 0x1f00, event to the FSM owner
    regs = {0x908: r.getrandbits(32)}
    for a in range(0x8000 + 0x570 * 4, 0x8000 + 0x570 * 4 + 0x800, 4): regs[a] = r.getrandbits(32)
    t375_world(m, r, regs)
    for f in ("apical_event_queue_push", "up", "system_hw_interrupts_disable", "system_hw_interrupts_enable"):
        m.hook(f, m.rec(f, 2))
    sb = bytearray(m.e.read(m.stab(), 4)); sb[0] = 0; m.e.write(m.stab(), bytes(sb))
    ctx = m.e.alloc(0x3000, bytes(0x3000)); pr = m.e.alloc(0x20, struct.pack("<8I", 0, r.choice([0, 1]), 0, 0x1234, 0, 0, 0, 0))
    m.e.write(ctx + 4, struct.pack("<I", pr))
    f = m.e.alloc(0x1000, bytes(0x1000)); m.e.write(f, struct.pack("<11I", ctx, 0, 0, 0, 0, 0, 0, 0, r.choice([0, 0, 1]), 7, r.choice([0, 4, 50, 1000])))
    rr = m.e.call("dis_update_stats", f)
    return rr.stop, m.e.read(f, 0x1000), [x for x in m.log if x[0] != "copy"]

GCTRL_IDS = ([0x8000000, 0x8000002, 0x8000003, 0x8000009, 0x800001f, 0x8000021, 0x8000030, 0x8000040, 0x8000041],
             [0x980916, 0x980917, 0x980919, 0x98091a, 0x98091c, 0x98091d, 0x98091e],
             [0x8000160, 0x8000163, 0x8000165, 0x8000167, 0x8000168],
             [0x8000083, 0x80000a1])
def c_gctrl(m, r, k):                   # apical_isp_core_ops_g_ctrl: jump-table arms + typed offsets (copy to user)
    t375_world(m, r, {0x40: r.getrandbits(32)})
    tun = m.e.alloc(0x4400, bytes(r.getrandbits(8) for _ in range(0x4400)))
    ub = m.e.alloc(0x100, b"\x55" * 0x100)
    ids = GCTRL_IDS[k % len(GCTRL_IDS)]; cid = ids[r.randrange(len(ids))]
    ctl = m.e.alloc(8, struct.pack("<II", cid, ub))
    rr = m.e.call("apical_isp_core_ops_g_ctrl", tun, ctl)
    return rr.stop, s32(rr.v0), m.e.read(ctl, 8), m.e.read(ub, 0x100), cid

C375 = (("roi", c_roi), ("vflip", c_vflip), ("gain_log2", c_gain), ("api_buffer", c_apibuf), ("release_all", c_release),
        ("ae_weight", c_weight), ("dis_stats", c_dis), ("g_ctrl", c_gctrl))
def t_ptr_scale(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})          # fresh modules: no hooks/state of the other groups
    bad = 0
    for k in range(n):
        for nm, c in C375:
            seed = rnd.getrandbits(32)
            res = [c(m, random.Random(seed), k) for m in (V, O)]
            if res[0] != res[1]:
                bad += 1
                if bad < 8: print("DIFF %s k=%d: vendor %s open %s" % (nm, k, str(res[0])[:300], str(res[1])[:300]))
    return bad

TESTS = [("nr_selectors", t_nr), ("general_frame_start", t_general), ("fsm_switch_state", t_fsm), ("crop_event", t_crop),
         ("sbus_write_u8", t_sbus), ("awb_normalise", t_normalise), ("event_queue_push", t_queue),
         ("awb_mesh_chain", t_mesh), ("compute_weight", t_weight), ("awb_calc_mesh", t_calc),
         ("gp_lock_paths", t_gp_lock), ("ioctl_uaccess", t_uaccess),
         ("ptr_scale", t_ptr_scale)]

def main():
    vko, oo = sys.argv[1], sys.argv[2]
    n = int(sys.argv[sys.argv.index("--n") + 1]) if "--n" in sys.argv else 200
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv else None
    V, O = Mod(vko, {}), Mod(oo, {})
    total = 0
    for name, t in TESTS:
        if only and only != name: continue
        b = t(V, O, random.Random(274 + len(name)), n)
        print("%-22s %s" % (name, "ok" if b == 0 else "%d DIFF" % b)); total += b
    sys.exit(1 if total else 0)

if __name__ == "__main__":
    main()
