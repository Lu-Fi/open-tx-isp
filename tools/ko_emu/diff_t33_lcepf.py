#!/usr/bin/env python3
"""diff_t33_lcepf.py OEM.ko LCEPF_EMU.o [--n 6] [--frames 4] [--leaf 300] [--seed 7] [--fn init,leaf,frame]

T33 LCE / LSF per-frame path (openimp #444): the vendor tx-isp-t33.ko functions tisp_lce_init (its lceInfo part),
tisp_lce_interrupt_static (irq 8), tisp_lsf_interrupt_static (irq 30), tisp_lce_soft_process (event 0xe) with
Tisp_lce_soft and its leaves, and the C of driver/t32/tx_isp_t33_lcepf.h (built for mipsel by
tests/t33_lcepf/build_emu.sh into LCEPF_EMU.o) run in Unicorn on the same random state: section 15 (sizes, EV / BV
knots, Gauss parameters, clip and converge words, modes), statistics parts, LSF histograms or curves, the
registers 0xc088 / 0x25040 / 0x25220, one or two sensors.
Compared after every call: the whole lceInfo block (but the list mirror 0x52c..0x62f4 func_lce_para_update keeps
and the section pointer), the section, the curve buffer, lists 16 and 37, the LSF buffer, the order and arguments
of the register reads, list starts and event pushes; for tisp_lce_init also the irq / event registrations.
Leaves (tisp_lce_hist_method, _thres_search, _hist_adjust, _16bit_data_converge, tisp_lsf_write_curve) on their own
with random inputs.  Nothing of the vendor module is stored here.
"""
import argparse, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_V0,
                                UC_MIPS_REG_V1, UC_MIPS_REG_PC, UC_MIPS_REG_RA)

ap = argparse.ArgumentParser(); ap.add_argument("oem"); ap.add_argument("our")
ap.add_argument("--n", type=int, default=6); ap.add_argument("--frames", type=int, default=4)
ap.add_argument("--leaf", type=int, default=300); ap.add_argument("--seed", type=int, default=7)
ap.add_argument("--fn", default="leaf,init,frame")
a = ap.parse_args(); rnd = random.Random(a.seed); M32 = 0xffffffff
FNS = set(a.fn.split(","))
LI, SEC = 0x23dc8, 0x400
CURVE_PA, STAT_PA, HIST_PA, LSF_PA = 0x02000000, 0x03000000, 0x04000000, 0x05000000
REG = {}
log = {"oem": [], "our": []}

def mk_hle(tag, emu_ref):
    L = log[tag]
    def u32at(e, ad): return struct.unpack("<I", e.read(ad, 4))[0]
    def rd(e):
        r = e.get_reg(UC_MIPS_REG_A0); L.append(("r", r)); return REG.get(r, 0)
    def trig(e):
        L.append(("trig", e.get_reg(UC_MIPS_REG_A0) & 0xff, e.get_reg(UC_MIPS_REG_A1) & 0xff)); return 0
    def push(e):
        L.append(("ev", e.get_reg(UC_MIPS_REG_A0), u32at(e, e.get_reg(UC_MIPS_REG_A1) + 8))); return 0
    def irqset(e):
        L.append(("irq", e.get_reg(UC_MIPS_REG_A0), e.get_reg(UC_MIPS_REG_A1), e.get_reg(UC_MIPS_REG_A2))); return 0
    def evset(e):
        L.append(("evcb", e.get_reg(UC_MIPS_REG_A0), e.get_reg(UC_MIPS_REG_A1), e.get_reg(UC_MIPS_REG_A2))); return 0
    def div64(e):
        n = e.get_reg(UC_MIPS_REG_A0) | e.get_reg(UC_MIPS_REG_A1) << 32
        d = e.get_reg(UC_MIPS_REG_A2) | e.get_reg(UC_MIPS_REG_A3) << 32
        q = n // d if d else 0
        return (q & M32, q >> 32)
    return {"system_reg_read": rd, "system_reg_write": lambda e: (L.append(("w", e.get_reg(UC_MIPS_REG_A0),
                                                                             e.get_reg(UC_MIPS_REG_A1))), 0)[1],
            "tisp_rcfg_module_cfg_trig": trig, "tisp_event_push": push, "system_irq_func_set": irqset,
            "tisp_event_set_cb": evset, "tisp_rcfg_module_register": lambda e: 0, "div64_u64": div64,
            "private_dma_cache_sync": lambda e: 0, "__private_spin_lock_irqsave": lambda e: 0,
            "private_spin_unlock_irqrestore": lambda e: 0, "isp_printf": lambda e: 0, "printk": lambda e: 0,
            "private_vmalloc": lambda e: emu_ref[0].alloc(e.get_reg(UC_MIPS_REG_A0)),
            "private_kmalloc": lambda e: emu_ref[0].alloc(e.get_reg(UC_MIPS_REG_A0))}

ro, ru = [None], [None]
oem = KoEmu(a.oem, hle=mk_hle("oem", ro), max_insns=60_000_000); ro[0] = oem
our = KoEmu(a.our, hle=mk_hle("our", ru), max_insns=60_000_000); ru[0] = our
OH = mk_hle("oem", ro)
for nm in ("system_reg_read", "system_reg_write", "tisp_rcfg_module_cfg_trig", "tisp_event_push", "system_irq_func_set",
           "tisp_event_set_cb", "tisp_rcfg_module_register", "private_dma_cache_sync", "__private_spin_lock_irqsave",
           "private_spin_unlock_irqrestore", "isp_printf", "private_vmalloc", "private_kmalloc"):
    def mk(nm):
        f = OH[nm]
        def _hook(uc, addr, size, ud):
            rv = f(oem); rv = rv if isinstance(rv, tuple) else (rv, 0)
            oem.set_reg(UC_MIPS_REG_V0, rv[0] & M32); oem.set_reg(UC_MIPS_REG_V1, rv[1] & M32)
            oem.set_reg(UC_MIPS_REG_PC, oem.get_reg(UC_MIPS_REG_RA))
        return _hook
    ad = oem.symaddr[nm]; oem.uc.hook_add(UC_HOOK_CODE, mk(nm), None, ad, ad)
oem.max_insns = our.max_insns = 60_000_000
OS = {k: oem.symaddr[k] for k in ("lceInfo", "lceBuffInfo", "lce_frame_flag", "adam_counter_lce", "stModuleAttr",
                                  "stIspInitPar", "tparamsP")}
FAIL = [0]

def w32(e, ad, v): e.write(ad, struct.pack("<I", v & M32))
def r32(): return rnd.getrandbits(32)

class Side:
    """one emulator's state: lceInfo block, section, buffers, lists"""
    def __init__(s, e, sec):
        s.e = e; e.heap_reset()
        s.c = e.alloc(LI, bytes(LI)); s.sec = e.alloc(SEC, sec)
        s.curve = e.alloc(0x2d00, bytes(0x2d00)); s.stat = e.alloc(0x13420, bytes(0x13420))
        s.hist = e.alloc(0x4840, bytes(0x4840)); s.lsf = e.alloc(0x2420, bytes(0x2420))
        s.l16 = e.alloc(0x1000, bytes(0x1000)); s.l37 = e.alloc(0x1000, bytes(0x1000))
        s.bufs = e.alloc(16, struct.pack("<4I", s.curve, STAT_PA, HIST_PA, CURVE_PA))
        s.scratch = e.alloc(0x1000, bytes(0x1000))
    def blobs(s):
        e = s.e; c = bytearray(e.read(s.c, LI))
        c[0x52c:0x62f4] = bytes(0x62f4 - 0x52c); c[0x62f8:0x62fc] = bytes(4)
        return {"lceInfo": bytes(c), "sec": e.read(s.sec, SEC), "curve": e.read(s.curve, 0x2d00),
                "l16": e.read(s.l16, 0x1000), "l37": e.read(s.l37, 0x1000), "lsf": e.read(s.lsf, 0x2420)}

def oem_globals(o, nsens, frame_flag=1):
    e = o.e
    w32(e, OS["lceInfo"], o.c); w32(e, o.c + 0x62f8, o.sec)
    b = OS["lceBuffInfo"]
    for off, v in ((0x10, o.curve), (0x14, o.stat), (0x18, o.hist), (0x1c, o.lsf), (0x20, CURVE_PA), (0x24, STAT_PA),
                   (0x28, HIST_PA), (0x2c, LSF_PA)):
        w32(e, b + off, v)
    w32(e, OS["stModuleAttr"] + 0x10 * 0x1c + 8, o.l16); w32(e, OS["stModuleAttr"] + 0x25 * 0x1c + 8, o.l37)
    e.write(OS["lce_frame_flag"], bytes([frame_flag])); w32(e, OS["adam_counter_lce"], 0)
    w32(e, OS["stIspInitPar"] + 904, nsens)

def cmp(what, o, u, extra=""):
    bo, bu = o.blobs(), u.blobs(); bad = []
    for k in bo:
        if bo[k] != bu[k]:
            i = next(j for j in range(len(bo[k])) if bo[k][j] != bu[k][j])
            n = sum(1 for j in range(len(bo[k])) if bo[k][j] != bu[k][j])
            bad.append("%s: %d bytes differ, first +%#x oem %02x our %02x" % (k, n, i, bo[k][i], bu[k][i]))
    if log["oem"] != log["our"]:
        bad.append("calls: oem %s our %s" % (log["oem"][:12], log["our"][:12]))
    if bad:
        FAIL[0] += 1
        print("MISMATCH %s %s" % (what, extra)); [print("  " + x) for x in bad[:6]]
        return False
    return True

def run(e, fn, *args, stack_args=()):
    r = e.call(fn, *args, stack_args=stack_args)
    if r.stop: raise SystemExit("emulation stopped in %s: %s (%s)" % (fn, r.stop, e.where(e.get_reg(UC_MIPS_REG_PC))))
    return r

def mk_sec():
    s = bytearray(random.Random(r32()).randbytes(SEC))
    s[0:4] = struct.pack("<I", 15)
    s[4:6] = bytes([rnd.choice([0, 1, 2, 4]), rnd.choice([0, 0, 4, 1])])        # +5: Tisp_lce_soft's update word
    for k in range(11):                                                             # EV knots rising, BV knots falling
        struct.pack_into("<I", s, 0x2fc + 4 * k, 0x100 * k * k + rnd.randrange(0, 0x80))
        struct.pack_into("<I", s, 0x328 + 4 * k, 0x80000 - 0x1000 * k * k - rnd.randrange(0, 0x80))
    s[0x3d9] = rnd.choice([0, 0, 1])
    for t in (0x2a, 0x35, 0x40, 0x4b, 0x56, 0x61):                                   # Gauss a, b, c tables
        lo, hi = {0x2a: (8, 200), 0x35: (1, 40), 0x40: (0, 255), 0x4b: (8, 200), 0x56: (1, 40), 0x61: (0, 255)}[t]
        for k in range(11): s[t + k] = rnd.randrange(lo, hi)
    s[0x1d] = rnd.randrange(1, 12); s[0x1e] = rnd.randrange(1, 12)                   # clips (<< 10)
    s[0x111] = rnd.choice([0, 16, 64, 100, 128, rnd.getrandbits(8)])                 # converge gain, lo, hi
    s[0x112] = rnd.choice([0, 1, 2, 4]); s[0x113] = rnd.choice([4, 16, 64, 255])
    s[0xc5] = rnd.choice([0, 1, 1, 2])
    lsfmode = rnd.random() < 0.15
    s[0x2fb] = 1 if lsfmode or rnd.random() < 0.2 else 0; s[0xc8] = 1 if lsfmode else rnd.choice([0, 0, 1])
    struct.pack_into("<H", s, 0x354, rnd.choice([0, 1, 1, 2])); struct.pack_into("<H", s, 0x356, rnd.choice([0, 32, 64, 96, 128, 200]))
    s[0x3d6] = rnd.choice([0, 3, 11, 12, rnd.getrandbits(8)]); s[0x3d7] = rnd.choice([0, 5, 23, 24, rnd.getrandbits(8)])
    return bytes(s)

def mk_hist_zone():
    """32 bins summing to about 0x8000 (15 bit), sometimes peaked"""
    if rnd.random() < 0.1: return [rnd.getrandbits(15) for _ in range(32)]
    w = [rnd.random() ** rnd.choice([1, 3, 6]) for _ in range(32)]
    if rnd.random() < 0.4:
        p = rnd.randrange(32); w[p] += rnd.choice([5, 20, 80])
    t = sum(w) or 1; tot = rnd.choice([0x8000, 0x8000, 0x7f00, 0x6000, 0x9000])
    return [min(0x7fff, int(x * tot / t)) for x in w]

def mk_lsf_buf(dataout):
    if dataout: return random.Random(r32()).randbytes(0x4840)
    b = bytearray(0x4840); g = mk_hist_zone()
    struct.pack_into("<32H", b, 0, *g)
    for z in range(288): struct.pack_into("<32H", b, 0x40 + 64 * z, *[v | (rnd.getrandbits(1) << 15) for v in mk_hist_zone()])
    return bytes(b)

def mk_stat_buf():
    b = bytearray(random.Random(r32()).randbytes(0x13420))
    for part in range(4):
        for r in range(12):
            for z in range(24):
                struct.pack_into("<I", b, part * 0x4d08 + r * 0x660 + 0x600 + 4 * z,
                                 rnd.choice([rnd.getrandbits(22), rnd.getrandbits(12), rnd.randrange(0, 0x40000)]) | (rnd.getrandbits(10) << 22))
    return bytes(b)

# ---------------- leaves ----------------
def leaf_tests():
    n0 = FAIL[0]
    sec = bytearray(SEC)
    o = Side(oem, bytes(sec)); u = Side(our, bytes(sec)); oem_globals(o, 1)
    for i in range(a.leaf):
        h = mk_hist_zone() + [rnd.getrandbits(16) for _ in range(4)]
        clip = [rnd.randrange(0x400, 0x3000), rnd.randrange(0x100, 0x2000)]
        if rnd.random() < 0.6:
            par = [rnd.randrange(32) for _ in range(3)]
            if rnd.random() < 0.7: par.sort()
        else:
            par = rnd.choice([[8, 16, 24], [0, 0, 0], [0, 1, 2], [31, 31, 31], [0, 31, 31], [5, 16, 24], [20, 16, 24], [0, 0, 31],
                              [5, 7, 24], [5, 10, 12], [3, 5, 7], [0, 2, 31], [10, 13, 15], [29, 31, 31]])
        st = rnd.choice([0, 0x80, 0x81, 0xff, rnd.getrandbits(8)]); c2 = rnd.choice([0, 1])
        sec[0xc5] = c2
        for s in (o, u):
            s.e.write(s.sec, bytes(sec)); s.e.write(s.scratch, struct.pack("<36H", *h))
            s.e.write(s.scratch + 0x100, struct.pack("<2H", *clip)); s.e.write(s.scratch + 0x120, bytes(par + [0]))
        log["oem"].clear(); log["our"].clear()
        run(oem, "tisp_lce_hist_method", 0, o.scratch, o.scratch + 0x100, o.scratch + 0x120, stack_args=(st,))
        run(our, "emu_hist_method", u.scratch, u.scratch + 0x100, u.scratch + 0x120, st, stack_args=(c2,))
        ho, hu = oem.read(o.scratch, 72), our.read(u.scratch, 72)
        if ho != hu:
            FAIL[0] += 1; print("MISMATCH hist_method par=%s clip=%s str=%#x clip2=%d" % (par, clip, st, c2))
            print("  oem", struct.unpack("<36H", ho)); print("  our", struct.unpack("<36H", hu))
            if FAIL[0] - n0 > 5: break
        # thres_search
        thr = sorted([rnd.randrange(0, 0x9000) for _ in range(3)]) if rnd.random() < 0.7 else [rnd.getrandbits(16) for _ in range(3)]
        for s in (o, u):
            s.e.write(s.scratch + 0x200, struct.pack("<3H", *thr)); s.e.write(s.scratch + 0x220, bytes(4))
        run(oem, "tisp_lce_thres_search", o.scratch, o.scratch + 0x200, o.scratch + 0x220)
        run(our, "emu_thres", u.scratch, u.scratch + 0x200, u.scratch + 0x220)
        if oem.read(o.scratch + 0x220, 4) != our.read(u.scratch + 0x220, 4):
            FAIL[0] += 1; print("MISMATCH thres_search thr=%s" % thr)
        # hist_adjust, converge
        cdf = bytes(rnd.getrandbits(8) for _ in range(32))
        up = [rnd.choice([0, rnd.getrandbits(8), rnd.getrandbits(32)]) for _ in range(32)]
        dn = [rnd.choice([0, rnd.getrandbits(8), rnd.getrandbits(32)]) for _ in range(32)]
        tgt = bytes(rnd.getrandbits(8) for _ in range(32))
        g, lo, hi = rnd.choice([0, 64, 128, rnd.getrandbits(8), rnd.getrandbits(32)]), rnd.getrandbits(8) if rnd.random() < 0.3 else rnd.randrange(8), rnd.getrandbits(8)
        for s in (o, u):
            s.e.write(s.scratch + 0x300, cdf); s.e.write(s.scratch + 0x400, struct.pack("<32I", *up))
            s.e.write(s.scratch + 0x500, struct.pack("<32I", *dn)); s.e.write(s.scratch + 0x600, cdf); s.e.write(s.scratch + 0x700, tgt)
        run(oem, "tisp_lce_hist_adjust", o.scratch + 0x300, o.scratch + 0x400, o.scratch + 0x500)
        run(our, "emu_adjust", u.scratch + 0x300, u.scratch + 0x400, u.scratch + 0x500)
        run(oem, "tisp_lce_16bit_data_converge", g, lo, hi, 0x20, stack_args=(o.scratch + 0x700, o.scratch + 0x600))
        run(our, "emu_converge", g, lo, hi, u.scratch + 0x700, stack_args=(u.scratch + 0x600,))
        if oem.read(o.scratch + 0x300, 32) != our.read(u.scratch + 0x300, 32):
            FAIL[0] += 1; print("MISMATCH hist_adjust")
        if oem.read(o.scratch + 0x600, 32) != our.read(u.scratch + 0x600, 32):
            FAIL[0] += 1; print("MISMATCH converge g=%#x lo=%d hi=%d" % (g, lo, hi))
        if FAIL[0] - n0 > 5: break
    log["oem"].clear(); log["our"].clear()
    oem.write(o.lsf, bytes(0x2420)); our.write(u.lsf, bytes(0x2420))
    run(oem, "tisp_lsf_write_curve", 0); run(our, "emu_lsf_curve", u.lsf)
    if oem.read(o.lsf, 0x2420) != our.read(u.lsf, 0x2420):
        FAIL[0] += 1; print("MISMATCH lsf_write_curve"); print(oem.read(o.lsf, 64).hex()); print(our.read(u.lsf, 64).hex()); print(oem.read(o.lsf+0x2380, 0xa0).hex())
    print("leaves: %d cases, %d mismatches" % (a.leaf, FAIL[0] - n0))

# ---------------- init + frames ----------------
SIZES = [(1920, 1080), (1920, 1080), (2560, 1440), (1280, 720), (2304, 1296), (640, 360)]
def chain(case, frames):
    sec = mk_sec(); nsens = rnd.choice([1, 1, 1, 2]); w, hgt = rnd.choice(SIZES)
    o = Side(oem, sec); u = Side(our, sec); oem_globals(o, nsens, frame_flag=0)
    info = oem.alloc(0x40, struct.pack("<12xHH", w, hgt) + bytes(0x40 - 16))
    tp = oem.alloc(0x6400, bytes(0x6400)); oem.write(tp + 0x5e5c, sec); w32(oem, OS["tparamsP"], tp)
    # the vendor's section is tparamsP + 0x5e5c: run on that copy, compare it as "sec"
    o.sec = tp + 0x5e5c; w32(oem, OS["lceInfo"], 0)
    log["oem"].clear(); log["our"].clear()
    run(oem, "tisp_lce_init", 0, info)
    o.c = struct.unpack("<I", oem.read(OS["lceInfo"], 4))[0]
    run(our, "emu_init", u.c, u.sec, w, hgt)
    regs = [x for x in log["oem"] if x[0] in ("irq", "evcb")]
    names = {v: k for k, v in oem.symaddr.items()}
    if case == 0:
        print("vendor tisp_lce_init registers: " + ", ".join("%s %#x -> %s" % (x[0], x[2], names.get(x[3], hex(x[3]))) for x in regs))
    log["oem"].clear(); log["our"].clear()
    # the init's list words and func_lce_para_update's section writes are not ours here
    for s in (o, u):
        s.e.write(s.l16, bytes(0x1000)); s.e.write(s.l37, bytes(0x1000)); s.e.write(s.curve, bytes(0x2d00)); s.e.write(s.lsf, bytes(0x2420))
    sv = bytearray(oem.read(o.sec, SEC))
    for lo, hi in ((0x117, 0x157), (0x1a3, 0x1e3)):
        sv[lo:hi] = our.read(u.sec + lo, hi - lo)
    oem.write(o.sec, bytes(sv))
    ok = cmp("init case %d" % case, o, u, "(%dx%d bv=%d)" % (w, hgt, sec[0x3d9]))
    if not ok: return
    w32(oem, OS["stIspInitPar"] + 904, nsens)
    for f in range(frames):
        dataout = 1 if rnd.random() < 0.15 else 0
        REG.clear(); REG[0xc088] = r32() if rnd.random() < 0.5 else rnd.randrange(4); REG[0x25040] = rnd.choice([0, 0, 1])
        REG[0x25220] = dataout | (rnd.getrandbits(1) << 1)
        st = mk_stat_buf(); hb = mk_lsf_buf(dataout)
        for s in (o, u):
            s.e.write(s.stat, st); s.e.write(s.hist, hb)
            s.e.write(s.curve + 0x1680, random.Random(f).randbytes(0x1680)) if f == 0 else None
        tag = "case %d frame %d (nsens %d dataout %d mode %d/%d)" % (case, f, nsens, dataout, sec[0x2fb], sec[0xc8])
        run(oem, "tisp_lce_interrupt_static", 0); run(our, "emu_irq8", u.c, u.sec, u.stat, u.bufs, stack_args=(u.l16, u.l37, nsens))
        if not cmp("irq8 " + tag, o, u): return
        log["oem"].clear(); log["our"].clear()
        run(oem, "tisp_lsf_interrupt_static"); run(our, "emu_irq30", u.c, u.sec, u.hist)
        if not cmp("irq30 " + tag, o, u): return
        log["oem"].clear(); log["our"].clear()
        r = run(oem, "tisp_lce_soft_process", 0); run(our, "emu_process", u.c, u.sec, u.curve + 0x1680)
        if not cmp("process " + tag, o, u, "insns %d" % r.insns): return
        log["oem"].clear(); log["our"].clear()

if "leaf" in FNS: leaf_tests()
if "init" in FNS or "frame" in FNS:
    n0 = FAIL[0]
    for i in range(a.n): chain(i, a.frames if "frame" in FNS else 0)
    print("chains: %d cases x %d frames, %d mismatches" % (a.n, a.frames if "frame" in FNS else 0, FAIL[0] - n0))
print("t33 lcepf: %s" % ("PASS" if not FAIL[0] else "FAIL (%d)" % FAIL[0]))
sys.exit(1 if FAIL[0] else 0)
