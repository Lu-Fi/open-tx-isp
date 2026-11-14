#!/usr/bin/env python3
"""diff_t33_lceev.py OEM.ko LCEEV_EMU.o [--n 40] [--evs 4] [--poke 300] [--seed 11] [--fn chain,poke,leaf]

T33 LCE / LSF EV update (openimp #500): the vendor tx-isp-t33.ko functions tisp_lce_init (its lceInfo part),
tisp_lce_ev_update, func_lce_para_update, tisp_lce_bin_para_update and tisp_lce_gausswgt_calc, and the C of
driver/t32/tx_isp_t33_lceev.h (built into LCEEV_EMU.o by tests/t33_binoracle/build_emu.sh from
tests/t33_binoracle/lceev.c) run in Unicorn on the same state.
  chain: random section 15 (EV / BV knots and tables, Gauss parameters, LSF mode, sizes), the vendor's
         tisp_lce_init and ours, then --evs EV updates with random EV / BV words.
  poke:  the whole lceInfo block random (but the section pointer), a random section, one EV update; also
         func_lce_para_update, bin_para_update and gausswgt_calc on their own on random blocks.
Compared after every call: the whole lceInfo block (but the section pointer 0x62f8), the section, lists 16 and
37 (all 0x1000 bytes) and lcemodeflag[0] / lcemodecounter[0].  The vendor's lceCfgMode / lsfCfgMode stay at their
module value 1.  Any call into a kernel import the script does not provide stops the run (a failure).
Nothing of the vendor module is stored here.
"""
import argparse, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_V0, UC_MIPS_REG_V1, UC_MIPS_REG_PC, UC_MIPS_REG_RA  # noqa: E402

ap = argparse.ArgumentParser(); ap.add_argument("oem"); ap.add_argument("our")
ap.add_argument("--n", type=int, default=40); ap.add_argument("--evs", type=int, default=4)
ap.add_argument("--poke", type=int, default=300); ap.add_argument("--seed", type=int, default=11)
ap.add_argument("--fn", default="chain,poke,leaf")
a = ap.parse_args(); rnd = random.Random(a.seed); M32 = 0xffffffff
FNS = set(a.fn.split(","))
LI, SEC, LST = 0x23dc8, 0x400, 0x1000
CURVE_PA, STAT_PA, HIST_PA, LSF_PA = 0x02000000, 0x03000000, 0x04000000, 0x05000000


def mk_hle(emu_ref):
    def div64(e):
        from unicorn.mips_const import UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3
        n = e.get_reg(UC_MIPS_REG_A0) | e.get_reg(UC_MIPS_REG_A1) << 32
        d = e.get_reg(UC_MIPS_REG_A2) | e.get_reg(UC_MIPS_REG_A3) << 32
        q = n // d if d else 0
        return (q & M32, q >> 32)
    return {"div64_u64": div64, "private_dma_cache_sync": lambda e: 0, "isp_printf": lambda e: 0,
            "printk": lambda e: 0, "tisp_rcfg_module_register": lambda e: 0, "system_irq_func_set": lambda e: 0,
            "tisp_event_set_cb": lambda e: 0, "__private_spin_lock_irqsave": lambda e: 0,
            "private_spin_unlock_irqrestore": lambda e: 0,
            "private_vmalloc": lambda e: emu_ref[0].alloc(e.get_reg(UC_MIPS_REG_A0)),
            "private_kmalloc": lambda e: emu_ref[0].alloc(e.get_reg(UC_MIPS_REG_A0))}


ro, ru = [None], [None]
oem = KoEmu(a.oem, hle=mk_hle(ro), max_insns=60_000_000); ro[0] = oem
our = KoEmu(a.our, hle=mk_hle(ru), max_insns=60_000_000); ru[0] = our
OH = mk_hle(ro)
for nm in ("private_dma_cache_sync", "isp_printf", "tisp_rcfg_module_register", "system_irq_func_set",
           "tisp_event_set_cb", "__private_spin_lock_irqsave", "private_spin_unlock_irqrestore", "private_vmalloc",
           "private_kmalloc"):
    def mk(nm):
        f = OH[nm]
        def _hook(uc, addr, size, ud):
            rv = f(oem); rv = rv if isinstance(rv, tuple) else (rv, 0)
            oem.set_reg(UC_MIPS_REG_V0, rv[0] & M32); oem.set_reg(UC_MIPS_REG_V1, rv[1] & M32)
            oem.set_reg(UC_MIPS_REG_PC, oem.get_reg(UC_MIPS_REG_RA))
        return _hook
    if nm in oem.symaddr:
        ad = oem.symaddr[nm]; oem.uc.hook_add(UC_HOOK_CODE, mk(nm), None, ad, ad)
OS = {k: oem.symaddr[k] for k in ("lceInfo", "lceBuffInfo", "stModuleAttr", "stIspInitPar", "tparamsP",
                                  "lcemodeflag", "lcemodecounter", "lceCfgMode", "lsfCfgMode")}
UM = our.symaddr["emu_lcemode"]
FAIL = [0]


def w32(e, ad, v): e.write(ad, struct.pack("<I", v & M32))
def r32(): return rnd.getrandbits(32)


class Side:
    """one emulator's state: lceInfo block, section, lists, buffer table"""
    def __init__(s, e, sec, blk=None):
        s.e = e; e.heap_reset()
        s.c = e.alloc(LI, blk if blk is not None else bytes(LI)); s.sec = e.alloc(SEC, sec)
        s.l16 = e.alloc(LST, bytes(LST)); s.l37 = e.alloc(LST, bytes(LST))
        s.bufs = e.alloc(16, struct.pack("<2I", CURVE_PA, HIST_PA) + bytes(8))
        s.hist = e.alloc(0x4840, bytes(0x4840))
    def mode(s):
        if s.e is oem:
            return s.e.read(OS["lcemodeflag"], 4) + s.e.read(OS["lcemodecounter"], 4)
        return s.e.read(UM, 8)
    def blobs(s):
        c = bytearray(s.e.read(s.c, LI)); c[0x62f8:0x62fc] = bytes(4)
        return {"lceInfo": bytes(c), "sec": s.e.read(s.sec, SEC), "l16": s.e.read(s.l16, LST),
                "l37": s.e.read(s.l37, LST), "mode": s.mode()}


def oem_globals(o):
    e = o.e
    w32(e, OS["lceInfo"], o.c); w32(e, o.c + 0x62f8, o.sec)
    b = OS["lceBuffInfo"]
    for off, v in ((0x18, o.hist), (0x20, CURVE_PA), (0x24, STAT_PA), (0x28, HIST_PA), (0x2c, LSF_PA)):
        w32(e, b + off, v)
    w32(e, OS["stModuleAttr"] + 0x10 * 0x1c + 8, o.l16); w32(e, OS["stModuleAttr"] + 0x25 * 0x1c + 8, o.l37)
    e.write(OS["lceCfgMode"], b"\x01"); e.write(OS["lsfCfgMode"], b"\x01")
    w32(e, OS["stIspInitPar"] + 904, 1)


def set_mode(o, u, m):
    o.e.write(OS["lcemodeflag"], m[:4]); o.e.write(OS["lcemodecounter"], m[4:]); u.e.write(UM, m)


def cmp(what, o, u, extra=""):
    bo, bu = o.blobs(), u.blobs(); bad = []
    for k in bo:
        if bo[k] != bu[k]:
            i = next(j for j in range(len(bo[k])) if bo[k][j] != bu[k][j])
            n = sum(1 for j in range(len(bo[k])) if bo[k][j] != bu[k][j])
            bad.append("%s: %d bytes differ, first +%#x oem %02x our %02x" % (k, n, i, bo[k][i], bu[k][i]))
    if bad:
        FAIL[0] += 1
        if FAIL[0] <= 8:
            print("MISMATCH %s %s" % (what, extra)); [print("  " + x) for x in bad[:6]]
        return False
    return True


def run(e, fn, *args, stack_args=()):
    r = e.call(fn, *args, stack_args=stack_args)
    if r.stop:
        raise SystemExit("emulation stopped in %s: %s (%s)" % (fn, r.stop, e.where(e.get_reg(UC_MIPS_REG_PC))))
    return r


def mk_sec():
    s = bytearray(random.Random(r32()).randbytes(SEC))
    s[0:4] = struct.pack("<I", 15)
    for k in range(11):                                                             # EV knots rising, BV knots falling
        struct.pack_into("<I", s, 0x2fc + 4 * k, 0x100 * k * k + rnd.randrange(0, 0x80))
        struct.pack_into("<I", s, 0x328 + 4 * k, 0x80000 - 0x1000 * k * k - rnd.randrange(0, 0x80))
    if rnd.random() < 0.1:                                                          # repeated knots
        k = rnd.randrange(1, 11); s[0x2fc + 4 * k:0x300 + 4 * k] = s[0x2f8 + 4 * k:0x2fc + 4 * k]
    s[0x3d9] = rnd.choice([0, 0, 1, rnd.getrandbits(8)])
    for t in (0x2a, 0x35, 0x40, 0x4b, 0x56, 0x61):                                   # gausswgt a, b, c tables
        lo, hi = {0x2a: (8, 200), 0x35: (1, 40), 0x40: (0, 255), 0x4b: (8, 200), 0x56: (1, 40), 0x61: (0, 255)}[t]
        for k in range(11): s[t + k] = rnd.randrange(lo, hi)
    for k in range(4):                                                              # para_update's four a, b, c sets
        s[0xac + 3 * k] = rnd.randrange(0, 256); s[0xad + 3 * k] = rnd.randrange(1, 48)
        s[0xae + 3 * k] = rnd.randrange(0, 256)
    for k in range(11): s[0x1f + k] = rnd.choice([rnd.randrange(0, 9), rnd.randrange(9, 64), rnd.getrandbits(8)])
    s[4 + 1] = rnd.choice([0, 1, 2, 4, rnd.getrandbits(8)])
    lsfmode = rnd.random() < 0.25
    s[0x2fb] = 1 if lsfmode or rnd.random() < 0.2 else rnd.choice([0, 2]); s[0xc8] = 1 if lsfmode else rnd.choice([0, 0, 1])
    return bytes(s)


def mk_ev(sec):
    """EV words lo / hi and BV word around the knots (ev = hi << 22 | lo >> 10)"""
    if rnd.random() < 0.2:
        return r32(), r32(), r32()
    e = struct.unpack_from("<I", sec, 0x2fc + 4 * rnd.randrange(11))[0] + rnd.randrange(-0x200, 0x200)
    e &= M32; hi = e >> 22; lo = ((e & 0x3fffff) << 10) | rnd.getrandbits(10)
    b = (struct.unpack_from("<I", sec, 0x328 + 4 * rnd.randrange(11))[0] + rnd.randrange(-0x2000, 0x2000)) & M32
    return lo, hi, b


def ev_both(o, u, lo, hi, bv):
    run(oem, "tisp_lce_ev_update", 0, 0, lo, hi, stack_args=(bv, 0))
    run(our, "emu_ev", u.c, u.sec, lo, hi, stack_args=(bv, u.bufs, u.l16, u.l37))


SIZES = [(1920, 1080), (2560, 1440), (1280, 720), (2304, 1296), (640, 360), (2880, 1620)]


def chain(case):
    sec = mk_sec(); w, h = rnd.choice(SIZES)
    o = Side(oem, sec); u = Side(our, sec); oem_globals(o)
    info = oem.alloc(0x40, struct.pack("<12xHH", w, h) + bytes(0x40 - 16))
    tp = oem.alloc(0x6400, bytes(0x6400)); oem.write(tp + 0x5e5c, sec); w32(oem, OS["tparamsP"], tp)
    o.sec = tp + 0x5e5c; w32(oem, OS["lceInfo"], 0)                  # the vendor's section is tparamsP + 0x5e5c
    set_mode(o, u, bytes(8))
    run(oem, "tisp_lce_init", 0, info)
    o.c = struct.unpack("<I", oem.read(OS["lceInfo"], 4))[0]
    run(our, "emu_init", u.c, u.sec, w, h)
    for s in (o, u):                                                 # the init's list words are tx_isp_t33_lce/lsf.h
        s.e.write(s.l16, bytes(LST)); s.e.write(s.l37, bytes(LST))
    set_mode(o, u, bytes(8))
    if not cmp("init case %d" % case, o, u, "(%dx%d bvmode=%d lsf %d/%d)" % (w, h, sec[0x3d9], sec[0x2fb], sec[0xc8])):
        return
    for k in range(a.evs):
        lo, hi, bv = mk_ev(sec)
        ev_both(o, u, lo, hi, bv)
        if not cmp("ev case %d step %d" % (case, k), o, u, "(lo %#x hi %#x bv %#x bvmode %d lsf %d/%d)" %
                   (lo, hi, bv, sec[0x3d9], sec[0x2fb], sec[0xc8])):
            return


def poke(case, leaf):
    sec = mk_sec(); blk = bytearray(random.Random(r32()).randbytes(LI))
    if leaf == "ev":
        o = Side(oem, sec, bytes(blk)); u = Side(our, sec, bytes(blk)); oem_globals(o)
        m = random.Random(r32()).randbytes(8); set_mode(o, u, m)
        for s in (o, u):
            s.e.write(s.l16, random.Random(case).randbytes(LST)); s.e.write(s.l37, random.Random(~case).randbytes(LST))
        lo, hi, bv = mk_ev(sec)
        ev_both(o, u, lo, hi, bv)
        return cmp("poke ev %d" % case, o, u, "(lo %#x hi %#x bv %#x bvmode %d lsf %d/%d)" %
                   (lo, hi, bv, sec[0x3d9], sec[0x2fb], sec[0xc8]))
    # func_lce_para_update / bin_para_update / gausswgt_calc: the words they read sane where they divide
    for k in range(4):
        struct.pack_into("<3I", blk, 0x164 + 12 * k, rnd.getrandbits(8), rnd.randrange(1, 48), rnd.getrandbits(8))
    for k in (0x4c, 0x58):
        struct.pack_into("<3I", blk, k, rnd.getrandbits(8), rnd.randrange(1, 48), rnd.getrandbits(8))
    struct.pack_into("<I", blk, 0x48, rnd.choice([rnd.getrandbits(8), rnd.getrandbits(32), rnd.randrange(9)]))
    struct.pack_into("<I", blk, 4, rnd.choice([4, 4, r32()]))
    o = Side(oem, sec, bytes(blk)); u = Side(our, sec, bytes(blk)); oem_globals(o)
    fv, fo = {"para": ("func_lce_para_update", "emu_para"), "bin": ("tisp_lce_bin_para_update", "emu_bin"),
              "gw": ("tisp_lce_gausswgt_calc", "emu_gw")}[leaf]
    run(oem, fv, 0); run(our, fo, u.c, u.sec)
    return cmp("poke %s %d" % (leaf, case), o, u)


if "chain" in FNS:
    n0 = FAIL[0]
    for i in range(a.n):
        chain(i)
    print("chain: %d inits x %d EV updates, %d mismatches" % (a.n, a.evs, FAIL[0] - n0))
if "poke" in FNS:
    n0 = FAIL[0]
    for i in range(a.poke):
        poke(i, "ev")
    print("poke ev: %d cases, %d mismatches" % (a.poke, FAIL[0] - n0))
if "leaf" in FNS:
    for lf in ("para", "bin", "gw"):
        n0 = FAIL[0]
        for i in range(a.poke):
            poke(i, lf)
        print("poke %s: %d cases, %d mismatches" % (lf, a.poke, FAIL[0] - n0))
print("t33 lceev: %s" % ("PASS" if not FAIL[0] else "FAIL (%d)" % FAIL[0]))
sys.exit(1 if FAIL[0] else 0)
