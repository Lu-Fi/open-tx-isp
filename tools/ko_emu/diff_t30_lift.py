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
  awb_detect_light_source                       light source +0x77d from the colour temperature +0x778 (#450)
  apical_event_queue_push                       queue head, _qlock1
  luts_fetch / AWB_mesh / AWB_mesh_LUT / mesh_AWB_getKnownSourceLight_weight_LUT / compute_weight
  awb_calc_avg_weighted_gr_gb_mesh              every FSM byte, fifo state (vendor rg_avg/gb_avg)
  ispcore/ncu_pad_event_handle, mscaler_frame_channel_streamoff   gp = thread_info: preempt_count, flags,
                                                unlock sequence, heap, GET_FORMAT buffer (#371)
  ldc_pad_event_handle / ldc_frame_channel_streamoff / tx_isp_ldc_show   same, LDC fifos (#365)
  tx_isp_unlocked_ioctl                         user vs kernel argument: return, copies to user (addr_limit, #371)
  frame_channel_unlocked_ioctl(_raw)            __vb2_queue_free(queue, count) arguments (#369)
  VIDIOC_DQBUF + pending signal                 EXPECTED difference (beyond vendor, #365): vendor spins, ours returns -ERESTARTSYS
  tx_isp_sync_ldc / tx_isp_sync_ncu             frame-end watchdog: reg_set calls, counters, registers (#377)
  ldc_core_ops_init                             spin lock address, param file size check, checksum loop step, memcpy (#418)
  cmos_set_exposure_target                      isp+0x11c hysteresis, +0x2fc gate, ratio clamp (#428)
  sensor_fsm_clear / sensor_init_output / sensor_sw_init   whole random buffer: byte/half stores at +0xde..+0xf2,
                                                u16 +0x46/+0x48/+0xe6, callback +0xb4(fsm + 0x28) (#463, lift indexed void **)
  every *_fsm_clear / *_initialize           random FSM + ISP buffer in, whole buffers out, every callee stubbed (#463)
  #463 class (fsm callees stubbed)            cmos_antiflicker_update, general_frame_end, image_crop_yoffset, awb_range_high,
                                                scene_mode, defect_pixel_table_write, mscaler_core_ops_init,
                                                get_gmv_gauss_method_fast_v2, dis_analyze: buffers, registers, return
  flash_processing                            phase byte +0x11, counters +0x14/+0x18/+0x1c, stab 12/49/50 (#463)
  AWB gain registers                            vendor cmos_fsm_process_interrupt(0) WB stage vs our
                                                tx_isp_t30_awb_gain_registers: 0x300..0x30c from AWB +0x760,
                                                exp2 precision, 0xfff clamp, WDR black-level factor (#459)

Callees outside the function under test are hooked at their entry in both modules and logged; the
calibration getters return synthetic tables (no vendor data).  Return values are compared only where the
OEM function returns one.  Exit 1 on any difference.
"""
import sys, os, random, struct, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu, HEAP, HEAP_SZ
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import UC_MIPS_REG_V0, UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_SP

ALIAS = {"AWB_mesh": ("AWB_mesh.isra.1", "AWB_mesh_isra_1")}
CB = 0x71000000
JUNK = 0x72000000
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

def t_detect_ls(V, O, rnd, n):          # awb_detect_light_source: +0x77d from the CT +0x778 (#450: lift stored 2 for every CT >= 3426)
    bad = 0
    edges = [0, 3425, 3426, 4500, 4501, 5714, 0x7fffffff, 0x80000000, 0xffffffff]
    for k in range(n):
        fsm = bytearray(rnd.getrandbits(8) for _ in range(0x800))
        ct = edges[k] if k < len(edges) else rnd.choice([rnd.randrange(0, 12000), rnd.getrandbits(32)])
        struct.pack_into("<I", fsm, 0x778, ct)
        res = []
        for m in (V, O):
            m.e.heap_reset(); f = m.e.alloc(0x800, fsm)
            r = m.call("awb_detect_light_source", f)
            res.append((r.stop, r.v0 & 0xff if hasattr(r, "v0") else None, m.e.read(f, 0x800)))
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF awb_detect_light_source ct=%d: vendor src %d ret %s, open src %d ret %s" % (ct, res[0][2][0x77d], res[0][1], res[1][2][0x77d], res[1][1]))
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
        e.write(ti + 20, struct.pack("<i", pc - 1)); lg.append(("unlock", e.get_reg(UC_MIPS_REG_A1)))
        if struct.unpack("<I", e.read(ti + 8, 4))[0] & 4: lg.append(("preempt_schedule",))   # its preempt_enable()
        return 0
    m.hook("private_spin_unlock_irqrestore", unlock)
    def mset(e):
        a, c, k = (e.get_reg(r) for r in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2))
        lg.append(("memset", a, c, k)); e.write(a, bytes([c & 0xff]) * k); return a
    m.hook("memset", mset)
    for f in ("printk", "isp_printf", "__might_sleep", "msleep", "private_msleep", "seq_printf", "seq_vprintf"):
        m.hook(f, lambda e: 0)
    m.hook("preempt_schedule", m.rec("preempt_schedule", 0))   # resched check after the preempt_count-- (#365)
    m.hook("tx_isp_reg_set", m.rec("tx_isp_reg_set", 4))
    # a2 too: ncu QUEUE_BUFFER passes the held buffer (OEM 0x1372c lw a2,412(s1); review M1, #365)
    m.hook("tx_isp_send_event_to_remote", m.rec("tx_isp_send_event_to_remote", 3))
    for f in ("private_mutex_lock", "private_mutex_unlock", "isp_free_buffer", "ldc_restart_module"):
        m.hook(f, m.rec(f, 1))
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
    if kind in ("ldc_qbuf", "ldc_done", "ldc_free"):   # ldc_pad_event_handle fifos (OEM 0x152dc / 0x151c0 / 0x15380, #365)
        B(b[0], 5, 1); W(b[0], 20, r.choice([0, 0, 0x20])); W(b[0], 32, b[1])
        tg = [r.randrange(1, 1 << 20) << 8 for _ in range(4)]
        for j in range(4): W(b[3], 16 * j + 8, tg[j])
        W(b[1], 0x1c0, r.randrange(0, 5)); W(b[1], 0x1b8, b[3]); W(b[1], 0xb8, b[4]); W(b[4], 4, r.choice([0, 2, 4, 6]))
        W(b[1], 0x178, r.choice([4, 4, 3])); W(b[1], 0x188, r.choice([0, 0, 1]))
        for o in (0x1c4, 0x1d0):                    # fifo heads: empty or one node
            if r.random() < 0.5: W(b[1], o, b[1] + o); W(b[1], o + 4, b[1] + o)
            else:
                nd = b[7] if o == 0x1d0 else b[8]
                W(b[1], o, nd); W(b[1], o + 4, nd); W(nd, 0, b[1] + o); W(nd, 4, b[1] + o)
        W(b[2], 8, r.choice(tg + [0x1234500]))
        ev = {"ldc_qbuf": 0x3000005, "ldc_done": 0x3000006, "ldc_free": 0x3000007}[kind]
        return "ldc_pad_event_handle", (b[0], ev, r.choice([b[2], b[2], 0])), True
    if kind == "ldc_off":                           # ldc_frame_channel_streamoff (OEM 0x13c8c): two lock sections
        B(b[0], 5, 2); W(b[0], 32, b[1]); W(b[1], 0xd0, b[5]); s4 = r.choice([4, 4, 3]); B(b[5], 7, s4); B(b[0], 7, r.choice([s4, s4, 3]))
        W(b[1], 0xb8, b[4]); W(b[4], 0, r.choice([0, 0, 4])); W(b[1], 0x1c0, r.randrange(0, 4)); W(b[1], 0x1b8, b[3])
        W(b[1], 0x80, r.choice([0, 1])); W(b[1], 0x88, CB)
        return "ldc_frame_channel_streamoff", (b[0],), True
    if kind == "ldc_show":                          # tx_isp_ldc_show (OEM 0x13a38): lock around the fifo dump
        W(b[0], 0x3c, b[1]); W(b[1], 0xd4, b[2]); W(b[2], 0x178, r.choice([4, 4, 3]))
        for o in (0x1c4, 0x1d0): W(b[2], o, b[2] + o); W(b[2], o + 4, b[2] + o)
        W(b[2], 0x1cc, r.choice([0, b[3]])); W(b[2], 0x1d8, r.choice([0, b[3]]))
        return "tx_isp_ldc_show", (b[0],), False
    # mscaler_frame_channel_streamoff (OEM 1677c, void): lock, poll, list drain, memset tail call
    B(b[0], 7, r.choice([4, 4, 3])); W(b[0], 32, b[1]); W(b[1], 100, b[2]); W(b[1], 80, r.choice([4, 3]))
    W(b[1], 76, r.randrange(0, 3)); W(b[2], 184, b[3]); W(b[1], 124, b[1] + 124)
    W(b[1], 12, r.choice([0x34524742, 0x50424752, 0]))
    return "mscaler_frame_channel_streamoff.isra.0", (b[0],), False

GP_KINDS = ("isp_qbuf", "isp_free", "isp_fmt", "ncu_qbuf", "msc_off", "ncu_dq", "ncu_qb2",
            "ldc_qbuf", "ldc_done", "ldc_free", "ldc_off", "ldc_show")
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

GCTRL_IDS = ([0x8000000, 0x8000002, 0x8000003, 0x8000009, 0x800001f, 0x8000020, 0x8000021, 0x800002c, 0x8000030, 0x8000040, 0x8000041],
             [0x980916, 0x980917, 0x980919, 0x98091a, 0x98091c, 0x98091d, 0x98091e],
             [0x8000160, 0x8000163, 0x8000165, 0x8000167, 0x8000168],
             [0x8000083, 0x80000a1])
def c_gctrl(m, r, k):                   # apical_isp_core_ops_g_ctrl: jump-table arms + typed offsets (copy to user)
    t375_world(m, r, {0x40: r.getrandbits(32)})
    tun = m.e.alloc(0x4400, bytes(r.getrandbits(8) for _ in range(0x4400)))
    ub = m.e.alloc(0x100, b"\x55" * 0x100)
    m.hook("apical_command", m.rec("apical_command", 4))      # #379: 0x8000020 calls it three times, result unused
    m.e.write(m.stab(), bytes(r.getrandbits(8) for _ in range(0x3c)))        # #379: 0x800002c copies the system table
    ids = GCTRL_IDS[k % len(GCTRL_IDS)]; cid = ids[r.randrange(len(ids))]
    ctl = m.e.alloc(8, struct.pack("<II", cid, ub))
    rr = m.e.call("apical_isp_core_ops_g_ctrl", tun, ctl)
    return (rr.stop, s32(rr.v0), m.e.read(ctl, 8), m.e.read(ub, 0x100), cid, list(m.log),
            m.e.read(tun + 0x828, 0xf0) if cid == 0x8000020 else b"")

def c_sensor_clear(m, r, k):             # sensor_fsm_clear (#463): byte/half stores at +0xde..+0xf2, whole buffer
    t375_world(m, r, {})
    b = m.e.alloc(0x800, bytes(r.getrandbits(8) for _ in range(0x800)))
    rr = m.e.call("sensor_fsm_clear", b)
    return rr.stop, rr.v0 & 0xffff, m.e.read(b, 0x800)

def c_sensor_out(m, r, k):               # sensor_init_output (#463): u16 +0xe6 from u16 +0x48 and arg2, whole buffer + v0
    t375_world(m, r, {})
    d = bytearray(r.getrandbits(8) for _ in range(0x800))
    d[0x48:0x4a] = struct.pack("<H", r.choice([480, 720, 1080, 1296, r.getrandbits(16)]))
    b = m.e.alloc(0x800, bytes(d))
    rr = m.e.call("sensor_init_output", b, r.choice([0, 720, 1080, r.getrandbits(32)]))
    return rr.stop, rr.v0, m.e.read(b, 0x800)

def c_sensor_sw(m, r, k):                # sensor_sw_init (#463): u16 +0x46/+0x48/+0xe6, callback +0xb4(fsm + 0x28)
    regs = {a: r.getrandbits(32) for a in (0x10, 0x14, 0x110, 0x118, 0x124, 0x130)}
    wl = t375_world(m, r, regs)
    m.hook("apical_wdr_fs_isp_setup", m.rec("apical_wdr_fs_isp_setup", 3)); m.hook("sensor_update_black", m.rec("sensor_update_black", 1))
    m.e.stubname[CB + 0x200] = "sw_cb"; m.e.hle["sw_cb"] = m.rec("sw_cb", 3, 0x5a5a)
    if JUNK not in m.e.stubname:          # every word of the buffer is a callable trap: a call through a wrong offset is logged
        m.e.uc.mem_map(JUNK, 0x10000); m.e.hle["junk_cb"] = m.rec("junk_cb", 3)
        for a in range(JUNK, JUNK + 0x10000, 4): m.e.stubname[a] = "junk_cb"
    d = bytearray(struct.pack("<%dI" % (0x800 // 4), *(JUNK | (r.getrandbits(16) & 0xfffc) for _ in range(0x800 // 4))))
    for o in (0x46, 0x48, 0xe6): d[o:o + 2] = struct.pack("<H", r.choice([480, 720, 1080, 1296, r.getrandbits(16)]))
    d[0xb4:0xb8] = struct.pack("<I", CB + 0x200)
    b = m.e.alloc(0x800, bytes(d))
    rr = m.e.call("sensor_sw_init", b)
    log = [(x[0],) + tuple(v - b if b <= v < b + 0x800 else v for v in x[1:]) for x in m.log]
    for f in ("apical_wdr_fs_isp_setup", "sensor_update_black"): m.unhook(f)
    return rr.stop, wl, log, m.e.read(b, 0x800)

def c_flash_proc(m, r, k):               # flash_processing (#463): state +0x10, phase byte +0x11, +0x14/+0x18 exposure, +0x1c count
    t375_world(m, r, {})
    for f in ("write_to_flash_output_port", "apical_isp_raise_event"): m.hook(f, m.rec(f, 2 if f[0] == "a" else 1))
    tb = m.e.alloc(0x10, bytes(r.getrandbits(8) for _ in range(0x10)))
    m.hook("_GET_UCHAR_PTR", lambda e: tb + (e.get_reg(UC_MIPS_REG_A0) & 7))
    sb = bytearray(m.e.read(m.stab(), 0x40)); sb[12], sb[49], sb[50] = r.getrandbits(8), r.getrandbits(8), r.getrandbits(8)
    m.e.write(m.stab(), bytes(sb))
    isp = m.e.alloc(0x1000, bytes(r.getrandbits(8) for _ in range(0x1000)))
    if r.random() < 0.5: m.e.write(isp + 0x31c, struct.pack("<i", r.randrange(-3000000, 3000000)))
    d = bytearray(r.getrandbits(8) for _ in range(0x40)); d[0:4] = struct.pack("<I", isp)
    d[0x10] = r.choice([0x59, 0x5a, 0x5a, 0x5a, 0x5a, r.getrandbits(8)]); d[0x11] = r.choice([0, 1, 1, 2, 5, 6, r.getrandbits(8)])
    if r.random() < 0.5: d[0x14:0x1c] = struct.pack("<ii", r.randrange(-3000000, 3000000), r.randrange(-3000000, 3000000))
    d[0x1c:0x1e] = struct.pack("<H", r.choice([0, 15, 16, r.getrandbits(16)]))
    b = m.e.alloc(0x40, bytes(d))
    rr = m.e.call("flash_processing", b)
    for f in ("write_to_flash_output_port", "apical_isp_raise_event", "_GET_UCHAR_PTR"): m.unhook(f)
    st = m.e.read(m.stab(), 0x40)
    return rr.stop, m.e.read(b, 0x40), m.e.read(isp, 0x1000), (st[12], st[49], st[50]), [x if x[0][0] == "w" else x[:2] for x in m.log if x[0] != "copy"]

C375 = (("roi", c_roi), ("vflip", c_vflip), ("gain_log2", c_gain), ("api_buffer", c_apibuf), ("release_all", c_release),
        ("ae_weight", c_weight), ("dis_stats", c_dis), ("g_ctrl", c_gctrl),
        ("sensor_fsm_clear", c_sensor_clear), ("sensor_init_output", c_sensor_out), ("sensor_sw_init", c_sensor_sw),
        ("flash_processing", c_flash_proc))
def t_ptr_scale(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})          # fresh modules: no hooks/state of the other groups
    bad = 0
    for k in range(n):
        for nm, c in C375:
            seed = rnd.getrandbits(32)
            res = [c(m, random.Random(seed), k) for m in (V, O)]
            if res[0] != res[1]:
                bad += 1
                if bad < 8: print("DIFF %s k=%d: %s" % (nm, k, diffdesc(res[0], res[1])))
    return bad

# ---------------------------------------------------------------- FSM clear/initialize: random buffers in, whole buffers out (#463)
SLOT = 0x73000000
FSM_BUF_SKIP = ("iridix_initialize",          # intended: statistics reset defaults in 0x3c0 (beyond the recovered sequence)
                "color_matrix_initialize")    # intended extra initial recalculate/write/modulate: own case cm_initialize (#478)
def fsm_buf_names(V, O):
    """every *_fsm_clear / *_initialize defined in both modules"""
    fn = lambda m: {sy.name for sy in m.e.elf.get_section_by_name(".symtab").iter_symbols()
                    if sy["st_info"]["type"] == "STT_FUNC" and sy["st_shndx"] != "SHN_UNDEF"}
    return sorted(n for n in fn(V) & fn(O) if n.endswith("_fsm_clear") or n.endswith("_initialize"))

SMALL = ("_GET_LEN", "_GET_ROWS", "_GET_COLS", "_GET_WIDTH")
def fsm_buf_world(m):
    """every other function and every import is a logged stub that returns a per-name readable slot
    (_GET_LEN/_ROWS/_COLS/_WIDTH: a small count); m.ret overrides single stubs for one case"""
    if getattr(m, "fsm_world", False): return
    m.fsm_world = True; m.e.uc.mem_map(SLOT, 0x400000); m.slot_seed = 0; m.slot_done = set(); m.ret = {}
    names = {sy.name for sy in m.e.elf.get_section_by_name(".symtab").iter_symbols()
             if sy.name and sy["st_info"]["type"] in ("STT_FUNC", "STT_NOTYPE")} | set(m.e.stubs)
    def mk(n):
        sl = SLOT + (zlib.crc32(n.encode()) % 0x400) * 0x1000
        def h(e):
            if n == "APICAL_READ_32":
                a = e.get_reg(UC_MIPS_REG_A0); return m.regs.get(a, random.Random(m.slot_seed * 65537 + a).getrandbits(32))
            if n == "APICAL_WRITE_32":
                m.regs[e.get_reg(UC_MIPS_REG_A0)] = e.get_reg(UC_MIPS_REG_A1); m.regw[e.get_reg(UC_MIPS_REG_A0)] = e.get_reg(UC_MIPS_REG_A1); return 0
            m.log.append(n)
            if n in m.ret: return m.ret[n](e) if callable(m.ret[n]) else m.ret[n]
            if n in SMALL: return 2 + random.Random(m.slot_seed ^ zlib.crc32(n.encode()) ^ e.get_reg(UC_MIPS_REG_A0)).randrange(15)
            if sl not in m.slot_done:
                m.slot_done.add(sl); rr = random.Random(m.slot_seed ^ zlib.crc32(n.encode()))
                e.write(sl, bytes(rr.getrandbits(8) for _ in range(0x1000)))
            return sl
        return h
    m.fsm_hooks = {n: mk(n) for n in names}

def stub_call(m, fn, seed, build, ret=None):
    """fn with every other function stubbed; build(m, r) -> (args, stack, [(addr, len) compared])"""
    fsm_buf_world(m)
    for n, h in m.fsm_hooks.items():
        if n != fn: m.hook(n, h)
    m.unhook(fn); m.e.stubname.pop(m.e.symaddr[fn], None)
    m.slot_seed = seed; m.slot_done = set(); m.regs = {}; m.regw = {}; m.ret = ret or {}; del m.log[:]
    r = random.Random(seed); m.e.heap_reset()
    m.e.gp = m.e.alloc(0x40, struct.pack("<8I", 0x5a5a0000, 0, 0, 0, 0, 0, USER_DS, 0))
    args, stack, cmp = build(m, r)
    rr = m.e.call(fn, *args, stack_args=stack)
    return rr.stop, [m.e.read(a, n) for a, n in cmp], sorted(m.regw.items()), sorted(set(m.log)), rr.v0   # callees as a set: getters may be called once or per use

def c_fsm_buf(m, fn, seed):
    def build(m, r):
        isp = m.e.alloc(0x8000, bytes(r.getrandbits(8) for _ in range(0x8000)))
        m.e.write(isp + 0x14e8, struct.pack("<I", isp))    # general FSM owner word = the ISP context, as on the device (#478)
        d = bytearray(r.getrandbits(8) for _ in range(0x2000)); d[0:4] = struct.pack("<I", isp)
        b = m.e.alloc(0x2000, bytes(d))
        return (b,), (), [(b, 0x2000), (isp, 0x8000)]
    return stub_call(m, fn, seed, build)[:4]          # no return value: most of them are void in the vendor ko

CM_INIT_REAL = ("tx_isp_t30_color_select_mesh_page", "tx_isp_t30_color_write_mesh_blend")   # open helpers = OEM inline 0x394/0x398 writes
CM_INIT_CMP = ("color_matrix_change_CCMs", "shading_mesh_load")
def c_cm_init(m, seed):
    """color_matrix_initialize: FSM buffer, final 0x394/0x398, the OEM callees and the shading_mesh_load mode (WDR mode byte
    through the general FSM owner isp+0x14e8, #478); our extra recalculate/write/modulate calls are not compared"""
    def build(m, r):
        isp = m.e.alloc(0x8000, bytes(r.getrandbits(8) for _ in range(0x8000)))
        gen = m.e.alloc(0x2000, bytes(r.getrandbits(8) for _ in range(0x2000)))   # what isp+0x14e8 points to
        m.e.write(isp + 0x14e8, struct.pack("<I", gen))
        d = bytearray(r.getrandbits(8) for _ in range(0x100)); d[0:4] = struct.pack("<I", isp)
        b = m.e.alloc(0x100, bytes(d))
        return (b,), (), [(b, 0xec)]
    fsm_buf_world(m)
    for h in CM_INIT_REAL: m.fsm_hooks.pop(h, None); m.unhook(h)   # fresh module per case: run them for real
    ret = {"shading_mesh_load": lambda e: (m.log.append("shading_mesh_load(%d)" % (e.get_reg(UC_MIPS_REG_A0) & 0xff)), 0)[1]}
    st, bufs, regw, calls, _ = stub_call(m, "color_matrix_initialize", seed, build, ret)
    return st, bufs, [w for w in regw if w[0] in (0x394, 0x398)], [c for c in calls if c.split("(")[0] in CM_INIT_CMP]

def t_cm_init(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})
    bad = 0
    for k in range(max(4, n // 20)):
        seed = rnd.getrandbits(32)
        res = [c_cm_init(m, seed) for m in (V, O)]
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF color_matrix_initialize k=%d: %s" % (k, diffdesc(res[0], res[1])))
    return bad

def t_fsm_buffers(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})          # fresh modules: every callee is stubbed here
    bad = 0
    for fn in fsm_buf_names(V, O):
        if fn in FSM_BUF_SKIP: continue
        for k in range(max(1, n // 20)):
            seed = rnd.getrandbits(32)
            res = [c_fsm_buf(m, fn, seed) for m in (V, O)]
            if res[0] != res[1]:
                bad += 1
                print("DIFF %s k=%d: %s" % (fn, k, diffdesc(res[0], res[1]))); break
    return bad

# ---------------------------------------------------------------- #463 bug class: cast-index / inline deref / pointer counters
def rbuf(m, r, n): b = m.e.alloc(n, bytes(r.getrandbits(8) for _ in range(n))); return b

def b_antiflicker(m, r):
    b = rbuf(m, r, 0x800); q = r.choice([0, 1, 0x200, r.getrandbits(16), r.getrandbits(32)])
    m.ret["get_quantised_integration_time"] = q
    return (b,), (), [(b, 0x800)]

def b_gfe(m, r):
    ctx = rbuf(m, r, 0x1000); b = bytearray(r.getrandbits(8) for _ in range(0x80)); b[0:4] = struct.pack("<I", ctx)
    if r.random() < 0.3: b[0x3c] = b[0x3f]
    fsm = m.e.alloc(0x80, bytes(b)); m.e.write(ctx + 0xc8, struct.pack("<I", CB + 0x300))
    m.e.stubname[CB + 0x300] = "wdr_cb"; m.e.hle["wdr_cb"] = lambda e: (m.log.append("wdr_cb %#x %#x" % (e.get_reg(UC_MIPS_REG_A0) - ctx, e.get_reg(UC_MIPS_REG_A1))), 0)[1]
    m.ret["color_matrix_change_CCMs"] = lambda e: (m.log.append("ccm %#x" % (e.get_reg(UC_MIPS_REG_A0) - ctx)), 0)[1]
    m.ret["general_set_wdr_mode"] = lambda e: (m.log.append("wdr %#x" % (e.get_reg(UC_MIPS_REG_A0) - fsm)), 0)[1]
    return (fsm,), (), [(fsm, 0x80), (ctx, 0x1000)]

def b_crop_y(m, r):
    b = rbuf(m, r, 0x6000); out = m.e.alloc(4, b"\xaa" * 4)
    return (b, (r.choice([19, 20, 22, 21, 0, 19, 20, 22]) << 16) | r.getrandbits(16), r.choice([0, 0, 1, 1, 2]), out), (), [(b, 0x6000), (out, 4)]

def b_awb_high(m, r):
    b = rbuf(m, r, 0x1000); out = m.e.alloc(4, b"\xaa" * 4)
    tabs = {0xeb: sorted(r.sample(range(100, 20000), 8)), 0xe0: [r.randrange(1, 0x10000) for _ in range(8)],
            0xe4: [r.randrange(0, 0x10000) for _ in range(8)]}
    ta = {k: m.e.alloc(16, struct.pack("<8H", *v)) for k, v in tabs.items()}
    m.ret["_GET_USHORT_PTR"] = lambda e: ta.get(e.get_reg(UC_MIPS_REG_A0), ta[0xe0])
    m.ret["_GET_LEN"] = 8
    a2 = r.choice([0, r.randrange(1, 100), r.randrange(1, 10000), (1000000 // 100) // r.randrange(100, 20000)])
    return (b, a2, r.choice([0, 0, 0, 1, 2]), out), (), [(b, 0x1000), (out, 4)]

def b_scene(m, r):
    b = rbuf(m, r, 0x1800); out = m.e.alloc(4, b"\xaa" * 4)
    m.ret["apical_command"] = 0
    return (b, r.randrange(62, 80), r.choice([0, 0, 0, 1]), out), (), [(b, 0x1800), (out, 4), (m.stab(), 0x40)]

def b_dpc(m, r):
    b = rbuf(m, r, 0x5000)
    return (b, r.choice([0, 1, 5, 64, 4096, 4097, 5000])), (), [(b, 0x10)]

def b_msc(m, r):
    s0 = rbuf(m, r, 0x200); a1 = rbuf(m, r, 0x100); m.e.write(a1 + 0xd4, struct.pack("<I", s0))
    chb, vib = rbuf(m, r, 0x400), rbuf(m, r, 0x400)
    m.e.write(s0 + 0xe8, struct.pack("<I", r.choice([1, 2, 2, 3, 4, 4])))
    m.e.write(s0 + 0x138, struct.pack("<II", chb, r.randrange(0, 5))); m.e.write(s0 + 0x144, struct.pack("<II", vib, r.randrange(0, 5)))
    m.ret["private_reset_tx_isp_module"] = r.choice([0, 0, 0xffffffea])
    for n in ("mscaler_frame_channel_streamoff.isra.0", "mscaler_frame_channel_streamoff_isra_0"):
        m.ret[n] = lambda e: (m.log.append("off %#x" % e.get_reg(UC_MIPS_REG_A0)), 0)[1]
    return (a1, r.choice([0, 1])), (), [(s0 + 0xe8, 4), (chb, 0x400), (vib, 0x400)]

def gmv_world(m, r):
    pts = rbuf(m, r, 0x200); rows, cols = r.randrange(0, 6), r.randrange(0, 6)
    lim = r.choice([0, 50, 200, 0x400]); tab = rbuf(m, r, 0x1000)
    m.e.write(pts + 8, struct.pack("<ii", rows, cols)); m.e.write(pts + 0xfc, struct.pack("<II", tab, lim))
    mv = m.e.alloc(24 * 40, b"".join(struct.pack("<ii", r.randrange(-12, 13), r.randrange(-12, 13)) + bytes(16) for _ in range(40)))
    return pts, mv

def b_gmv(m, r):
    pts, mv = gmv_world(m, r); out = m.e.alloc(8, b"\xaa" * 8); a2 = m.e.alloc(8, b"\x55" * 8)
    return (pts, mv + 24, a2, 0), (out,), [(out, 8), (a2, 8)]

def b_dis(m, r):
    pts, mv = gmv_world(m, r); o3, o4 = m.e.alloc(8, b"\xaa" * 8), m.e.alloc(8, b"\xbb" * 8)
    fn = m.e.symaddr["get_gmv_gauss_method_fast_v2"]; m.e.stubname.pop(fn, None)
    m.e.write(pts + 0x24, struct.pack("<I", fn))
    m.ret["validate_motion_vectors"] = r.choice([0, 0, 0, 1])
    m.ret["iir_filter_v2"] = lambda e: (m.log.append("iir %#x %#x" % (e.get_reg(UC_MIPS_REG_A1), e.get_reg(UC_MIPS_REG_A2))), 7)[1]
    return (pts, mv + 24, o3, o4), (), [(o3, 8), (o4, 8)]

def b_clip(m, r):                        # dis_clip_gmv_vector: two-word vector clipped to +-ctx+0x10 / +-ctx+0x14 (#463: caller passed a scalar)
    ctx = rbuf(m, r, 0x40); m.e.write(ctx + 0x10, struct.pack("<ii", r.randrange(-50, 50), r.randrange(-50, 50)))
    v = m.e.alloc(8, struct.pack("<ii", r.randrange(-80, 80), r.randrange(-80, 80)))
    return (ctx if r.random() < 0.9 else 0, v), (), [(v, 8)]

CLASS463 = (("cmos_antiflicker_update", b_antiflicker), ("general_frame_end", b_gfe), ("image_crop_yoffset", b_crop_y),
            ("awb_range_high", b_awb_high), ("scene_mode", b_scene), ("defect_pixel_table_write", b_dpc),
            ("mscaler_core_ops_init", b_msc), ("get_gmv_gauss_method_fast_v2", b_gmv), ("dis_analyze", b_dis), ("dis_clip_gmv_vector", b_clip))
def t_class463(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})          # fresh modules: every callee is stubbed here
    bad = 0
    for fn, b in CLASS463:
        for k in range(max(2, n // 4)):
            seed = rnd.getrandbits(32)
            res = [stub_call(m, fn, seed, b) for m in (V, O)]
            if fn == "mscaler_core_ops_init":          # lock helpers differ (inline vs call): buffers, streamoff args, return
                res = [(x[0], x[1], [l for l in x[3] if l.startswith("off")], x[4]) for x in res]
            if res[0] != res[1]:
                bad += 1
                print("DIFF %s k=%d: %s" % (fn, k, diffdesc(res[0], res[1]))); break
    return bad

def diffdesc(a, b):
    """first differing element; for byte buffers the differing offsets (vendor/open bytes)"""
    if isinstance(a, (tuple, list)) and isinstance(b, (tuple, list)) and len(a) == len(b):
        for i, (x, y) in enumerate(zip(a, b)):
            if x != y: return "[%d] %s" % (i, diffdesc(x, y))
    if isinstance(a, bytes) and isinstance(b, bytes) and len(a) == len(b):
        d = [i for i in range(len(a)) if a[i] != b[i]]
        return "bytes differ at " + " ".join("+%#x(%02x/%02x)" % (i, a[i], b[i]) for i in d[:12]) + (" ..%d" % len(d) if len(d) > 12 else "")
    return "vendor %s open %s" % (str(a)[:300], str(b)[:300])

# frame_channel_unlocked_ioctl(_raw): __vb2_queue_free(queue, count) (#369: the lift passed the queue only, count = stale a1)
FCI_CMDS = (0xc0145608, 0x40045613, 0x40045612, 0x80045613, 0xc0505609, 0x40445611)
def t_fci_args(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})
    bad = 0; hits = 0
    for k in range(n):
        cmd = FCI_CMDS[k % len(FCI_CMDS)]; seed = rnd.getrandbits(32); res = []
        for m in (V, O):
            r0 = random.Random(seed); ti, b = gp_world(m, r0); gp_hooks(m); lg = m.log
            m.hook("__vb2_queue_free", lambda e: (lg.append(("vb2_free", e.get_reg(UC_MIPS_REG_A0), e.get_reg(UC_MIPS_REG_A1))), 0)[1])
            m.hook("__copy_user", lambda e: (e.write(e.get_reg(UC_MIPS_REG_A0), e.read(e.get_reg(UC_MIPS_REG_A1), e.get_reg(UC_MIPS_REG_A2))), e.set_reg(UC_MIPS_REG_A2, 0), 0)[2])
            ub = m.e.alloc(0x100, bytes((r0.getrandbits(8) & (7 if i % 4 == 0 else 0xff)) | (1 if i == 0 else 0) for i in range(0x100)))   # req.count != 0 (the lifted stack aliasing of 16(sp) is not modelled)
            filp = m.e.alloc(0x100); m.e.write(filp + 112, struct.pack("<I", b[0]))
            for off in (524, 60, 36, 660, 664, 548, 532, 676):
                m.e.write(b[0] + off, struct.pack("<I", r0.choice([0, 1, 2, 3, 4, 8, 16])))
            del m.log[:]
            r = m.e.call("frame_channel_unlocked_ioctl_raw" if "frame_channel_unlocked_ioctl_raw" in m.e.symaddr else "frame_channel_unlocked_ioctl", filp, cmd, ub)
            res.append(([x for x in m.log if x[0] == "vb2_free"], r.stop if r.stop else s32(r.v0)))
            hits += len(res[-1][0])
        if res[1][1] == '__kmalloc': continue          # lifted stack aliasing of the 20-byte request (16(sp) vs local_10): other path, not an argument
        if res[0][0] != res[1][0]:
            bad += 1
            if bad < 5: print("DIFF fci cmd=%#x: vendor %s open %s" % (cmd, res[0], res[1]))
    if "-v" in sys.argv: print("  fci hits", hits)
    return bad

# VIDIOC_DQBUF with a signal pending and an empty done list: EXPECTED DIFFERENCE (user-approved beyond vendor
# 2026-11-12, #365, docs/BEYOND_VENDOR.md).  The vendor loop (frame_channel_unlocked_ioctl 0x894..0x91c) leaves the
# wait with the signal pending, finish_wait()s and starts the wait again: it spins (the emulator stops at the
# instruction limit).  Our tx_isp_t30_frame_dqbuf returns -ERESTARTSYS after one finish_wait().  Queue layout
# (type +36, done list +540, wait queue +552, streaming byte +560) is the same in both, so one world serves both.
def t_dqbuf_signal(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})
    bad = 0
    for k in range(n):
        sig = k % 2 == 0; res = []
        for m, fn in ((V, "frame_channel_unlocked_ioctl"), (O, "tx_isp_t30_frame_dqbuf")):
            m.e.heap_reset(); m.e.max_insns = 20000
            ti = m.e.alloc(0x40); task = m.e.alloc(0x40); m.e.gp = ti
            m.e.write(ti, struct.pack("<8I", task, 0, 2 if sig else 0, 0, 0, 0, USER_DS, 0)); m.e.write(task + 4, struct.pack("<I", ti))
            ch = m.e.alloc(0x400); m.e.write(ch + 36, struct.pack("<I", 1)); m.e.write(ch + 540, struct.pack("<II", ch + 540, ch + 540))
            m.e.write(ch + 560, bytes([1]))
            ub = m.e.alloc(0x100); m.e.write(ub + 4, struct.pack("<I", 1))
            filp = m.e.alloc(0x100); m.e.write(filp + 112, struct.pack("<I", ch))
            lg = m.log; del lg[:]
            m.hook("__copy_user", lambda e: (e.write(e.get_reg(UC_MIPS_REG_A0), e.read(e.get_reg(UC_MIPS_REG_A1), e.get_reg(UC_MIPS_REG_A2))), e.set_reg(UC_MIPS_REG_A2, 0), 0)[2])
            for f in ("printk", "isp_printf", "__might_sleep", "prepare_to_wait"): m.hook(f, lambda e: 0)
            m.hook("finish_wait", m.rec("finish_wait", 0)); m.hook("schedule", m.rec("schedule", 0))
            r = (m.e.call("frame_channel_unlocked_ioctl_raw" if "frame_channel_unlocked_ioctl_raw" in m.e.symaddr else fn, filp, 0xc0445611, ub)
                 if m is V else m.e.call(fn, ch, ub))
            spin = r.insns >= m.e.max_insns      # the emulator ends a run at the instruction limit
            res.append(("spin" if spin else r.stop, s32(r.v0) if not (spin or r.stop) else None, [x[0] for x in lg][:2]))
        v, o = res
        if sig:      # expected: vendor spins, ours returns -ERESTARTSYS after one finish_wait, no schedule()
            ok = v[0] == "spin" and o == (None, -512, ["finish_wait"])
        else:        # no signal: both sleep in schedule() once (the emulated schedule returns, next pass hits the instruction limit)
            ok = v[0] == o[0] == "spin" and v[2][:1] == o[2][:1] == ["schedule"]
        if not ok:
            bad += 1
            if bad < 4: print("DIFF dqbuf signal=%s: vendor %s open %s" % (sig, v, o))
    return bad

# ---------------------------------------------------------------- LDC/NCU frame-end watchdog (#377)
# OEM tx_isp_sync_ldc (0x153fc, tail call to tx_isp_sync_ncu 0x13850), run by the core ISR at each frame end.
# Vendor state in .bss (15776 g_ncu, 15780/15784 ncu stall/last start, 15796 ldc stall, 15800/15804 last
# done, 15808 g_ldc); ours in named statics.  Compared: tx_isp_reg_set calls (with the stack value), the
# devices' counters/registers, the watchdog state and the mscaler word at KSEG1 0x130b0000.
SYNC_MSC = 0x130b0000                                # KSEG1 0xb30b0000 as Unicorn (no MMU) sees it
def sync_state(m):
    if "g_ldc" in m.e.symaddr and "tx_isp_sync_ldc_stall" in m.e.symaddr:
        a = m.e.symaddr
        return {"ldc": a["g_ldc"], "ncu": a["g_ncu"], "ldc_stall": a["tx_isp_sync_ldc_stall"],
                "ldc_last": a["tx_isp_sync_ldc_last"], "ncu_stall": a["tx_isp_sync_ncu_stall"],
                "ncu_last": a["tx_isp_sync_ncu_last"]}
    bss = [b for i, b in m.e.secbase.items() if m.e.secname.get(i) == ".bss"][0]
    return {"ldc": bss + 15808, "ncu": bss + 15776, "ldc_stall": bss + 15796, "ldc_last": bss + 15800,
            "ncu_stall": bss + 15780, "ncu_last": bss + 15784}
def t_sync(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})
    for m in (V, O): m.e.uc.mem_map(SYNC_MSC, 0x1000)
    bad = 0; kicks = [0, 0]
    for k in range(n):
        seed = rnd.getrandbits(32); res = []
        for m in (V, O):
            r = random.Random(seed); m.e.heap_reset(); st = sync_state(m); lg = m.log; del lg[:]
            W = lambda a, v: m.e.write(a, struct.pack("<I", v & M32))
            ldc, ncu, lr, nr = m.e.alloc(0x200), m.e.alloc(0x1d0), m.e.alloc(0x100), m.e.alloc(0x100)
            m.e.write(ldc, bytes(r.getrandbits(8) for _ in range(0x200))); m.e.write(lr, bytes(r.getrandbits(8) for _ in range(0x100)))
            m.e.write(ncu, bytes(r.getrandbits(8) for _ in range(0x1d0))); m.e.write(nr, bytes(r.getrandbits(8) for _ in range(0x100)))
            W(ldc + 184, lr); W(ncu + 184, nr); W(ldc + 376, r.choice([4, 4, 4, 3])); W(ncu + 372, r.choice([4, 4, 4, 3]))
            for dev, so, do, cnt in ((ldc, 488, 496, (st["ldc_last"], st["ldc_last"] + 4)), (ncu, 440, 448, (st["ncu_last"],))):
                d = r.choice([0, 1, 5, 0xffffffff]); dh = r.choice([0, 0, 0, 1])
                W(dev + do, d); W(dev + do + 4, dh)
                sl = (d + r.choice([0, 0, 1])) & M32; W(dev + so, sl); W(dev + so + 4, dh + (sl < d))
                W(cnt[0], r.choice([d, d, sl, 7]))
                if len(cnt) > 1: W(cnt[1], r.choice([dh, dh, 1]))
            W(st["ldc_stall"], r.choice([0, 1, 2, 2, 3, 6])); W(st["ncu_stall"], r.choice([0, 1, 2, 2, 3, 6]))
            if r.random() < 0.5: W(st["ncu_last"], struct.unpack("<I", m.e.read(ncu + 440, 4))[0])   # NCU job held since the last frame
            W(st["ldc"], r.choice([ldc, ldc, ldc, 0])); W(st["ncu"], r.choice([ncu, ncu, ncu, 0])); W(SYNC_MSC, 0)
            def rs(e, lg=lg):
                sp = e.get_reg(UC_MIPS_REG_SP)
                lg.append(("tx_isp_reg_set",) + tuple(e.get_reg(x) for x in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3))
                          + struct.unpack("<I", e.read(sp + 16, 4)))
                return 0
            m.hook("tx_isp_reg_set", rs)
            rr = m.e.call("tx_isp_sync_ldc")
            g = lambda a, nb=4: m.e.read(a, nb)
            res.append((rr.stop, list(lg), g(ldc, 0x200), g(ncu, 0x1d0), g(lr, 0x100), g(nr, 0x100), g(SYNC_MSC),
                        g(st["ldc_stall"]), g(st["ldc_last"], 8), g(st["ncu_stall"]), g(st["ncu_last"])))
        for x in res[0][1]: kicks[x[2] == 0] += 1      # reg_set(ldc, 0, ...) / reg_set(ncu, 4, ...)
        if res[0][0] or res[0] != res[1]:                 # a stop (unmapped, trap) counts too
            bad += 1
            if bad < 4:
                d = [i for i in range(len(res[0])) if res[0][i] != res[1][i]]
                print("DIFF sync fields %s: vendor %s open %s" % (d, res[0][:2], res[1][:2]))
    if "-v" in sys.argv: print("  sync kicks: ncu %d, ldc %d" % (kicks[0], kicks[1]))
    return bad

# ldc_core_interrupt_service_routine (OEM 0x14904): the done count (u64 +1 at 0x14970) the watchdog compares;
# tx_isp_ncu_show (OEM 0x12450): infifo entries' +8 address (0x12630) and the start/done u64 pair (0x125c0) (#377)
def t_counts(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})
    bad = 0
    for k in range(n):
        seed = rnd.getrandbits(32); res = []; isr = k % 2 == 0
        for m in (V, O):
            r = random.Random(seed); ti, b = gp_world(m, r); gp_hooks(m); lg = m.log
            W = lambda a, o, v: m.e.write(a + o, struct.pack("<I", v & M32))
            def sp(e, lg=lg):                          # the o32 words the format consumes (%lld: an even pair)
                st = e.get_reg(UC_MIPS_REG_SP); fa = e.get_reg(UC_MIPS_REG_A1)
                fmt = bytes(e.read(fa, 64)).split(b"\0")[0]
                w = [e.get_reg(UC_MIPS_REG_A2), e.get_reg(UC_MIPS_REG_A3)] + list(struct.unpack("<4I", e.read(st + 16, 16)))
                out, i = [], 0
                for sp_ in fmt.split(b"%")[1:]:
                    if sp_.startswith(b"ll"): i += i & 1; out += w[i:i + 2]; i += 2
                    elif sp_ and not sp_.startswith(b"%"): out.append(w[i]); i += 1
                lg.append(("seq_printf", fmt) + tuple(out))
                return 1
            m.hook("seq_printf", sp)
            if "regtrace_seq_printf" in m.e.symaddr: m.hook("regtrace_seq_printf", sp)   # our seq_printf(...) shim
            if isr:
                W(b[0], 212, b[1]); W(b[0], 204, b[5]); W(b[0], 208, b[6]); W(b[1], 184, b[2])
                W(b[2], 4, r.choice([0, 1, 3, 5, 7])); W(b[1], 376, r.choice([4, 3])); W(b[1], 392, r.choice([0, 1]))
                W(b[1], 496, r.choice([0, 7, 0xffffffff])); W(b[1], 500, r.choice([0, 2]))
                del lg[:]; rr = m.e.call("ldc_core_interrupt_service_routine", b[0])
                res.append((rr.stop, rr.v0 if not rr.stop else None, list(lg), m.e.read(b[1], 0x200)))
            else:
                W(b[0], 0x3c, b[1]); W(b[1], 0xd4, b[2]); W(b[1], 0xd0, b[3]); W(b[2], 0x174, 4); W(b[3], 0x14, r.choice([0, 0x20]))
                h = b[2] + 404; W(b[2], 404, b[4]); W(b[2], 408, b[4]); W(b[4], 0, h); W(b[4], 4, h); W(b[4], 8, r.getrandbits(32))
                W(b[2], 412, r.choice([0, b[7]]))
                for o in (440, 444, 448, 452, 456): W(b[2], o, r.choice([0, 1, 3, 0x80000000]))
                del lg[:]; rr = m.e.call("tx_isp_ncu_show", b[0])
                res.append((rr.stop, [x for x in lg if x[0] != "seq_printf"] , [x[1:] for x in lg if x[0] == "seq_printf"][1:]))
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF %s: vendor %s open %s" % ("ldc_isr" if isr else "ncu_show", res[0][:3], res[1][:3]))
    return bad

LDC_STEP = 0x2d4                                  # OEM ldc_core_ops_init 0x2d4: v0 = running checksum at each loop test
def c_ldc_init(m, r, k):                # ldc_core_ops_init (OEM 0x13f0c, #418): spin lock at +476, param file size/checksum loop
    t375_world(m, r, {}); lg = m.log
    for f in ("arch_local_irq_save", "arch_local_irq_restore", "private_spin_unlock_irqrestore", "private_reset_tx_isp_module",
              "filp_open", "vfs_read", "filp_close", "__kmalloc", "kfree", "snprintf", "memcpy", "ldc_frame_channel_streamoff"):
        m.hook(f, m.rec(f, 3))
    W = lambda a, o, v: m.e.write(a + o, struct.pack("<I", v & M32))
    dev, s0, buf, fil, f1, f2 = (m.e.alloc(0x300, bytes(0x300)) for _ in range(6))
    size = r.choice([24, 24 + 4 * r.randrange(1, 40), 24 + 4 * r.randrange(1, 40), 24 + 4 * r.randrange(0, 40)])   # a file below 24 bytes returns an undefined register in the OEM
    for i in range(0, 0x300, 4): W(buf, i, r.getrandbits(32))
    W(dev, 212, s0); W(s0, 376, r.choice([0, 2, 2, 3, 4])); W(s0, 508, r.choice([buf, buf, 0])); W(s0, 272, buf + 0x200)
    W(fil, 12, f1); W(f1, 40, f2); W(f2, 48, size); W(f2, 52, r.choice([0, 0, 0, 1]))
    W(buf, 16, size - 24 if r.random() < 0.85 else size)
    m.e.write(m.e.symaddr[fname(m, "ldc_user_params")], struct.pack("<I", r.choice([0, 0, 0, 1])))
    m.hook("filp_open", lambda e: (lg.append(("filp_open",)), fil)[1])
    m.hook("__kmalloc", lambda e: (lg.append(("kmalloc", e.get_reg(UC_MIPS_REG_A0))), buf)[1])
    m.hook("vfs_read", lambda e: (lg.append(("vfs_read", e.get_reg(UC_MIPS_REG_A1), e.get_reg(UC_MIPS_REG_A2))), 0)[1])
    m.hook("private_reset_tx_isp_module", lambda e: 0)
    for f in ("sprintf", "snprintf"): m.hook(f, lambda e: (lg.append(("sprintf",)), 0)[1])     # vendor sprintf, ours snprintf
    m.hook("private_spin_unlock_irqrestore", lambda e: (lg.append(("unlock", e.get_reg(UC_MIPS_REG_A0))), 0)[1])
    for f in ("kfree", "filp_close"): m.hook(f, m.rec(f, 1))
    m.hook("arch_local_irq_save", lambda e: (lg.append(("irqsave",)), 0x55)[1])      # a0 is a stale register here
    def mset(e):
        a, c, kk = (e.get_reg(x) for x in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2))
        lg.append(("memset", kk)); e.write(a, bytes([c & 0xff]) * kk); return a
    m.hook("memset", mset)
    def mc(e):
        d, sr, c = (e.get_reg(x) for x in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2))
        lg.append(("memcpy", c, bytes(e.read(sr, c)))); e.write(d, e.read(sr, c)); return d
    m.hook("memcpy", mc)
    return dev, s0, buf, size

def ldc_init_run(m, dev, s0, buf, chk=None):
    pd = m.e.symaddr["ldc_params_version"]; up = m.e.symaddr[fname(m, "ldc_user_params")]; nm = m.e.symaddr["ldc_params_nums"]
    m.e.write(pd, bytes(16)); m.e.write(nm, struct.pack("<I", 6)); pdd = m.e.symaddr["tx_isp_platform_device"]; pdb = m.e.read(pdd, 16)
    if chk is not None: m.e.write(buf + 20, struct.pack("<I", chk))
    rr = m.e.call("ldc_core_ops_init", dev, 1)
    return (rr.stop, None if rr.stop else s32(rr.v0), list(m.log), m.e.read(s0, 0x300), m.e.read(buf, 0x300), m.e.read(pd, 16), m.e.read(up, 4), m.e.read(nm, 4), m.e.read(pdd, 16) == pdb)

def t_ldc_init(V, O, rnd, n):
    V, O = Mod(V.path, {}), Mod(O.path, {})
    bad = 0; okp = 0; sums = []
    V.e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, sz, ud: sums.append(uc.reg_read(UC_MIPS_REG_V0)), None,
                    V.e.symaddr["ldc_core_ops_init"] + LDC_STEP, V.e.symaddr["ldc_core_ops_init"] + LDC_STEP)
    for k in range(n):
        seed = rnd.getrandbits(32); res = []
        # probe the vendor's running checksum once so the success path (OEM 0x324: memcpy of the 16 header bytes) is reached
        del sums[:]
        pr = random.Random(seed); dev, s0, buf, sz = c_ldc_init(V, pr, k); ldc_init_run(V, dev, s0, buf)
        chk = sums[-1] if sums and pr.random() < 0.8 else None
        for m in (V, O):
            r = random.Random(seed); dev, s0, buf, sz = c_ldc_init(m, r, k)
            res.append(ldc_init_run(m, dev, s0, buf, chk))
        okp += any(x[0] == "memcpy" for x in res[0][2])
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF ldc_init k=%d fields %s: vendor %s open %s" % (k, [i for i in range(9) if res[0][i] != res[1][i]], str(res[0][:3])[:200], str(res[1][:3])[:200]))
    if "-v" in sys.argv: print("  ldc_init: %d/%d reached the memcpy (checksum ok)" % (okp, n))
    return bad

def t_set_target(V, O, rnd, n):
    """cmos_set_exposure_target (vendor 0x36058): hysteresis of isp+0x11c (cmos+0x1c) against table 146 word 5,
    owner byte +0x2fc gate, exposure ratio +0x1bc clamped at 0x400 (#428)"""
    bad = 0
    for k in range(n):
        thr = rnd.choice([0, 1, 0x800, 0x4000, 0x20000, rnd.getrandbits(20), 0xffffffff])
        cur = rnd.choice([0, 691247, rnd.getrandbits(24), rnd.getrandbits(32)])
        tgt = rnd.choice([cur, cur + rnd.randrange(-0x5000, 0x5000), rnd.getrandbits(24), rnd.getrandbits(32)]) & M32
        ratio = rnd.choice([0, 0x100, 0x400, 0x401, rnd.getrandbits(16), rnd.getrandbits(32)])
        gate = rnd.choice([0, 0, 0, 1]); mode = rnd.choice([0, 0, 1, 2, 3])
        res = []
        for m in (V, O):
            m.e.heap_reset()
            own = bytearray(0x1600); own[0x2fc] = gate; own[0x1524] = mode
            o = m.e.alloc(len(own), own); tab = m.e.alloc(32, struct.pack("<8I", 1, 2, 3, 4, 5, thr, 7, 8))
            cm = bytearray(0x200); struct.pack_into("<I", cm, 0, o); struct.pack_into("<I", cm, 0x1c, cur & M32)
            struct.pack_into("<I", cm, 0x1bc, 0x55aa); c = m.e.alloc(len(cm), cm)
            m.hook("_GET_HDR_TABLE_INDEX", lambda e: 0x92 + 1000 * e.get_reg(UC_MIPS_REG_A1))
            m.hook("_GET_UINT_PTR", lambda e, tab=tab: tab if e.get_reg(UC_MIPS_REG_A0) == 0x92 + 1000 * mode else 0)
            r = m.call("cmos_set_exposure_target", c, tgt, ratio & M32)
            m.unhook("_GET_HDR_TABLE_INDEX"); m.unhook("_GET_UINT_PTR")
            res.append((r.stop, m.e.read(c, len(cm)), m.e.read(o, len(own))))
        if res[0] != res[1]:
            bad += 1
            if bad < 4:
                d = [hex(i) for i in range(0x200) if res[0][1][i] != res[1][1][i]]
                print("DIFF set_exposure_target thr=%#x cur=%#x tgt=%#x ratio=%#x gate=%d stop=%s/%s bytes %s" % (thr, cur, tgt, ratio, gate, res[0][0], res[1][0], d[:8]))
    return bad

def t_wb_regs(V, O, rnd, n):
    """WB gain registers (#459): vendor cmos_fsm_process_interrupt(cmos, 0) (ko 0x25548..0x256f8) against our
    tx_isp_t30_awb_gain_registers(isp): math_exp2 of the four awb_normalise log2 gains (Q17 in WDR modes 1/2),
    0xfff clamp, black-level factor 0xfff00/(0xfff-BL)>>8 in WDR modes 1..3; the 0x300..0x30c writes in order"""
    bad = 0
    for k in range(n):
        mode = rnd.choice([0, 0, 1, 2, 3, 4, 0xff])
        logs = [rnd.choice([0, 0x10000, rnd.getrandbits(20), rnd.getrandbits(32)]) for _ in range(4)]
        regs0 = {a: rnd.getrandbits(32) for a in (0x300, 0x304, 0x308, 0x30c)}
        for a in (0x310, 0x314, 0x318, 0x31c):       # BL 0xfff traps in the OEM (teq): not a valid input
            regs0[a] = (rnd.getrandbits(32) & ~0xfff) | rnd.choice([0, 0x51, 0x100, 0xffe, rnd.randrange(0, 0xfff)])
        res = []
        for m in (V, O):
            m.e.heap_reset()
            isp = bytearray(0x1600); isp[0x1524] = mode
            for i, v in enumerate(logs): struct.pack_into("<I", isp, 0x748 + 0x760 + 4 * i, v)
            ia = m.e.alloc(len(isp), isp); struct.pack_into("<I", isp, 0x100, ia); m.e.write(ia, isp)
            xs = m.e.alloc(0x40, bytes(0x40))
            regs, wl = dict(regs0), []
            regfile(m, regs, wl)
            m.hook("math_exp2", lambda e: (e.get_reg(UC_MIPS_REG_A0) * 0x9e37 + e.get_reg(UC_MIPS_REG_A1) * 0x101 + e.get_reg(UC_MIPS_REG_A2) * 7) & 0x1fff)
            for h in ("cmos_get_frame_exposure_set", "_process_fps_cnt", "cmos_move_exposure_history"):
                m.hook(h, lambda e, xs=xs: xs)
            if m is V: m.call("cmos_fsm_process_interrupt", ia + 0x100, 0)
            else: m.call("tx_isp_t30_awb_gain_registers", ia)
            for h in ("math_exp2", "cmos_get_frame_exposure_set", "_process_fps_cnt", "cmos_move_exposure_history"): m.unhook(h)
            res.append([w for w in wl if 0x300 <= w[0] <= 0x30c])
        if res[0] != res[1] or len(res[0]) != 4:
            bad += 1
            if bad < 4: print("DIFF wb_gain_registers mode=%d logs=%s: vendor %s open %s" % (mode, [hex(x) for x in logs], res[0], res[1]))
    return bad

# ---------------------------------------------------------------- YUV matrix (#478): initialize + the tuning setters' update sequence
MYUV_SEQ = ((0xa2, 0x80), (0xa2, 0), (0xa2, 230), (0xa2, 0x80))    # b72/b73 picture sequence (saturation_strength byte)
def t_matrix_yuv(V, O, rnd, n):
    """matrix_yuv_initialize, then per step one field of the FSM block (+0xa2/+0xa3/+0xa4 strengths, +0xa0 hue,
    +0xf6 output selector, +0xa8 mode) and matrix_yuv_update: every register write 0x540..0x5ff in order and the
    whole FSM block after each step; the calibration table is synthetic.  Step k=0 = the b72/b73 sequence."""
    V, O = Mod(V.path, {}), Mod(O.path, {})
    bad = 0
    for k in range(max(4, n // 10)):
        seed = rnd.getrandbits(32); r = random.Random(seed)
        seq = MYUV_SEQ if k == 0 else [r.choice([(0xa2, r.getrandbits(8)), (0xa3, r.getrandbits(8)), (0xa4, r.getrandbits(8)),
                                                 (0xa0, r.choice([0, 0xb4, 0x168, r.getrandbits(9)])),
                                                 (0xf6, r.choice([0x4f, 0x50, 0x51, 0x52, 0x53, 0x54, r.getrandbits(16)])),
                                                 (0xa8, r.choice([0, 1, 2, 3, 4, 5]))]) for _ in range(6)]
        tab = struct.pack("<16H", *[r.getrandbits(10) for _ in range(16)])
        blk = bytes(r.getrandbits(8) for _ in range(0x100))
        regs0 = {a: r.getrandbits(32) for a in range(0x540, 0x600, 4)}
        res = []
        for m in (V, O):
            m.e.heap_reset()
            regs, wl = dict(regs0), []
            regfile(m, regs, wl)
            ta = m.e.alloc(len(tab), tab)
            m.hook("_GET_USHORT_PTR", lambda e, ta=ta: ta)
            b = m.e.alloc(0x100, blk)
            out = []
            m.call("matrix_yuv_initialize", b)
            out.append(("init", m.e.read(b, 0x100)))
            for off, val in seq:
                if off in (0xa0, 0xf6): m.e.write(b + off, struct.pack("<H", val & 0xffff))
                else: m.e.write(b + off, bytes([val & 0xff]))
                del wl[:]
                rr = m.call("matrix_yuv_update", b)
                out.append((off, val, rr.stop, [w for w in wl if 0x540 <= w[0] < 0x600], m.e.read(b, 0x100)))
            m.unhook("_GET_USHORT_PTR")
            res.append(out)
        if res[0] != res[1]:
            bad += 1
            if bad < 4: print("DIFF matrix_yuv k=%d seq=%s: %s" % (k, [(hex(a), v) for a, v in seq], diffdesc(res[0], res[1])))
    return bad

TESTS = [("nr_selectors", t_nr), ("general_frame_start", t_general), ("fsm_switch_state", t_fsm), ("crop_event", t_crop),
         ("sbus_write_u8", t_sbus), ("awb_normalise", t_normalise), ("awb_detect_light_source", t_detect_ls), ("event_queue_push", t_queue),
         ("awb_mesh_chain", t_mesh), ("compute_weight", t_weight), ("awb_calc_mesh", t_calc),
         ("gp_lock_paths", t_gp_lock), ("ioctl_uaccess", t_uaccess),
         ("ptr_scale", t_ptr_scale), ("fsm_buffers", t_fsm_buffers), ("cm_initialize", t_cm_init), ("ptr_class_463", t_class463), ("fci_args", t_fci_args), ("sync_watchdog", t_sync), ("ldc_ncu_counts", t_counts), ("ldc_core_ops_init", t_ldc_init),
         ("set_exposure_target", t_set_target), ("awb_gain_registers", t_wb_regs), ("matrix_yuv", t_matrix_yuv),
         ("dqbuf_signal(expected diff)", t_dqbuf_signal)]

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
