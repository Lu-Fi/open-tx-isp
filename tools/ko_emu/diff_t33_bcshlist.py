#!/usr/bin/env python3
"""diff_t33_bcshlist.py OEM.ko BCSHLIST_EMU.o [--n 2000] [--seed 7]

T33 BCSH: which vendor function writes list 18 when (openimp #507).  Per case one random bcsh_para block,
section 19 and CT knots (as diff_t33_bcshct.py), then a sequence on the vendor tx-isp-t33.ko and on ours
(tests/t33_binoracle/bcshlist.c, built by build_emu.sh):
  1. tisp_bcsh_switch_para(0) / t33_bcsh_switch_para: the vendor must write no list slot, must not touch
     tispBcshRefresh nor bcshInfo (no params_transform); ours must leave its list untouched and return 0.
  2. two or three updates in random order: tisp_bcsh_ev_update(0, 0, EV lo, EV hi, BV lo, BV hi) /
     t33_bcsh_ev_update and tisp_bcsh_ct_update(0, 0, ct) / t33_bcsh_ct_update.  Each vendor update runs with
     the list DMA idle (the slots are written at once, tispBcshRefresh = 1) or busy (no slot written,
     tispBcshRefresh = 2, then tisp_bcsh_irq_frame_down(0) with the DMA idle writes them).
Compared after every step: the block but word 0, the section, "rebuilt" (vendor: slots written or refresh 2) and
the accumulated list slots (before half of the updates the inputs of the calc parts the update's mode skips are
poked into both blocks: the CT matrix, the brightness / offset bytes); so the stale effect is covered: after the switch the list keeps its old words until
an update rebuilds it, and that update stores the whole switched state.  Vendor: bcsh_para[0] = block, bcshInfo[0]
= an info buffer, bcshCfgMode = 1, stIspInitPar + 904 = 1, tisp_reg_write_t33 recorded (slot -> word).  Nothing
of the vendor module is stored here.
"""
import argparse, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu  # noqa: E402
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_PC  # noqa: E402

ap = argparse.ArgumentParser(); ap.add_argument("oem"); ap.add_argument("our")
ap.add_argument("--n", type=int, default=2000); ap.add_argument("--seed", type=int, default=7)
a = ap.parse_args(); rnd = random.Random(a.seed); M32 = 0xffffffff
ST, SEC, KN = 0x290, 0x4c4, 40


def div64(e):
    n = e.get_reg(UC_MIPS_REG_A0) | e.get_reg(UC_MIPS_REG_A1) << 32
    d = e.get_reg(UC_MIPS_REG_A2) | e.get_reg(UC_MIPS_REG_A3) << 32
    q = n // d if d else 0
    return (q & M32, q >> 32)


H = {"div64_u64": div64, "isp_printf": lambda e: 0, "printk": lambda e: 0}
oem = KoEmu(a.oem, hle=H, max_insns=20_000_000)
our = KoEmu(a.our, hle=H, max_insns=20_000_000)
OS = {k: oem.symaddr[k] for k in ("bcsh_para", "tparamsP", "stLutInfo", "bcshInfo", "bcshCfgMode", "tispBcshRefresh", "stIspInitPar")}
WR = []; DMA = [0]; TRIG = []
for n, f in (("tisp_reg_write_t33", lambda e: (WR.append((e.get_reg(UC_MIPS_REG_A2), e.get_reg(UC_MIPS_REG_A3))), 0)[1]),
             ("tisp_rcfg_get_dma_state", lambda e: DMA[0]),
             ("tisp_rcfg_module_cfg_trig", lambda e: (TRIG.append(e.get_reg(UC_MIPS_REG_A1)), 0)[1])):
    oem.stubname[oem.symaddr[n]] = n; oem.hle[n] = f


def w32(e, ad, v): e.write(ad, struct.pack("<I", v & M32))


def run(e, fn, *args):
    r = e.call(fn, *args)
    if r.stop:
        raise SystemExit("emulation stopped in %s: %s (%s)" % (fn, r.stop, e.where(e.get_reg(UC_MIPS_REG_PC))))


def word():
    k = rnd.random()
    return rnd.getrandbits(32) if k < 0.3 else rnd.getrandbits(8) if k < 0.6 else rnd.getrandbits(12) if k < 0.85 \
        else rnd.choice([0, 1, 0x400, 0x7fff, 0x8000, 0xffff, M32])


def mk_case():
    st = bytearray(b"".join(struct.pack("<I", word()) for _ in range(ST // 4)))
    struct.pack_into("<II", st, 8, rnd.getrandbits(rnd.choice([12, 20, 32])), rnd.choice([0, 0, 1, rnd.getrandbits(9)]))
    struct.pack_into("<i", st, 0x20, rnd.randrange(1000, 12000))
    st[0x1d0] = rnd.choice([0, 0, 0, 1, rnd.getrandbits(8)])
    sec = bytearray(rnd.randbytes(SEC))
    struct.pack_into("<I", sec, 0, 0x01350181)
    v = 0
    for k in range(12):                                           # EV knots rising (4..), BV knots falling (0x34..)
        v = min(v + rnd.choice([0, rnd.randrange(1, 0x400), rnd.randrange(1, 0x40000), 0x90000000 * (rnd.random() < 0.1)]), M32)
        struct.pack_into("<I", sec, 4 + 4 * k, v)
    v = rnd.choice([0xffffff, 0xffffff, M32 - rnd.getrandbits(8)])     # high BV knots: distances >= 2^31
    for k in range(12):
        v = max(v - rnd.choice([0, rnd.randrange(1, 0x400), rnd.randrange(1, 0x40000), 0x90000000 * (rnd.random() < 0.1)]), 0)
        struct.pack_into("<I", sec, 0x34 + 4 * k, v)
    struct.pack_into("<I", sec, 0x64, rnd.choice([0, rnd.getrandbits(8), rnd.getrandbits(20)]))
    for o in (0x4ae, 0x4af, 0x477, 0x478, 0x471, 0x472, 0x4a8, 0x4a9, 0x4a3, 0x4a4, 0x4a6, 0x46a, 0x46c, 0x46d):
        sec[o] = rnd.choice([0, 1, 2, rnd.getrandbits(8)])
    sec[0x4a2] = rnd.choice([0, 1, 1, 2, 3])
    sec[0x4c0] = rnd.choice([0, 0, 1])
    n = rnd.randrange(1, 9); sec[0x4bf] = n
    for k in range(9):
        struct.pack_into("<H", sec, 0x8c + 2 * k, rnd.choice([0, rnd.randrange(0, 300)]))
    struct.pack_into("<H", sec, 0xac, rnd.choice([0, rnd.randrange(0, 500)]))
    lut = bytearray()
    ct = rnd.randrange(1500, 3000)
    for k in range(10):
        lut += struct.pack("<i", ct) + b"".join(struct.pack("<i", rnd.randrange(-0x2400, 0x2400)) for _ in range(9))
        ct += rnd.randrange(1, 2000)
    if rnd.random() < 0.5:                                        # CT on a knot / hysteresis edge
        k = rnd.randrange(0, n)
        kc = struct.unpack_from("<i", lut, 40 * k)[0]; hy = struct.unpack_from("<H", sec, 0x8c + 2 * k)[0]
        struct.pack_into("<i", st, 0x20, kc + rnd.choice([-hy, hy, 0]) + rnd.choice([-1, 0, 1]))
    if rnd.random() < 0.2:                                        # EV / BV words far from the knots
        struct.pack_into("<I", st, 8, rnd.choice([M32 - rnd.getrandbits(12), rnd.getrandbits(28)]))
    return bytes(st), bytes(sec), bytes(lut)


def dbg(st, sec, bo, bu):
    lo32, hi32 = struct.unpack_from("<II", st, 8); bv = sec[0x4c0]
    x = lo32 if bv else ((hi32 << 22) | (lo32 >> 10)) & M32
    tb = [struct.unpack_from("<I", sec, (0x34 if bv else 4) + 4 * k)[0] for k in range(12)]
    print("  bv %d x %#x knots %s" % (bv, x, [hex(t) for t in tb]))
    print("  s16 0x422..: %s" % [struct.unpack_from("<h", sec, 0x422 + 2 * k)[0] for k in range(12)])
    print("  oem %d our %d init %d" % (struct.unpack_from("<h", bo, 0x132)[0], struct.unpack_from("<h", bu, 0x132)[0], struct.unpack_from("<h", st, 0x132)[0]))


def near(olo, ohi, hy):
    """a u32 about the hysteresis away from the kept 64-bit value (also from a negative one: the signed compare)"""
    if ohi == M32:
        v = hy + rnd.choice([-1, 0, 1, 2]) - ((1 << 32) - olo)
        if v >= 0:
            return v & M32
    return (olo + rnd.choice([-1, 1]) * (hy + rnd.choice([-1, 0, 1, 2]))) & M32


def blk(e, st, sec):
    return bytes(4) + e.read(st + 4, ST - 4), e.read(sec, SEC)


FAIL = 0
SENT = 0xdeadbeef
NSW = NUP = NBUSY = NREB = 0
for case in range(a.n):
    st, sec, lut = mk_case()
    st = bytearray(st); sec = bytearray(sec)
    struct.pack_into("<I", st, 0x24, rnd.choice([rnd.randrange(0, 20), 0xff, struct.unpack_from("<I", st, 0x24)[0]]))
    st[0x28] = rnd.choice([0, 0xfe, 0xff, rnd.getrandbits(8)]); st[0x29] = rnd.choice([0, 0xfe, 0xff, rnd.getrandbits(8)])
    struct.pack_into("<II", st, 0x10, rnd.getrandbits(rnd.choice([12, 24, 32])), rnd.choice([0, 0, 0, 1, M32]))
    sec[0x4a7] = rnd.choice([0, 1, rnd.getrandbits(8)]); sec[0x4a8] = rnd.choice([0, 1, 2]); sec[0x4a9] = rnd.choice([0, 2, 3])
    if rnd.random() < 0.15:                                       # a negative kept EV / BV close to 0
        struct.pack_into("<II", st, 8, M32 - rnd.randrange(0, struct.unpack_from("<I", sec, 0x64)[0] + 3), M32)
    st = bytes(st); sec = bytes(sec)
    oem.heap_reset(); our.heap_reset(); WR.clear(); TRIG.clear(); DMA[0] = 0
    tp = oem.alloc(0x8b78 + SEC + 0x10); osec = tp + 0x8b78; oem.write(osec, sec)
    ost = oem.alloc(ST, st); olut = oem.alloc(len(lut), lut); oli = oem.alloc(0x40, bytes(0x40))
    info0 = rnd.randbytes(0xc0); oinf = oem.alloc(0xc0, info0)
    w32(oem, oli + 0x1c, olut); w32(oem, OS["stLutInfo"], oli); w32(oem, OS["bcsh_para"], ost); w32(oem, OS["tparamsP"], tp)
    w32(oem, OS["bcshInfo"], oinf); oem.write(OS["bcshCfgMode"], b"\x01"); w32(oem, OS["stIspInitPar"] + 904, 1)
    rf0 = rnd.choice([0, 1, 2, 0x5a]); oem.write(OS["tispBcshRefresh"], bytes([rf0]))
    usec = our.alloc(SEC + 0x10, sec); ust = our.alloc(ST, st); ulut = our.alloc(len(lut), lut)
    old = [rnd.getrandbits(32) for _ in range(64)]                # the list of the last writer before the switch
    ul = our.alloc(0x40 * 4, struct.pack("<64I", *old))
    vw = {i - 1: old[i] for i in range(1, 64)}
    bad = None
    # 1. the switch
    run(oem, "tisp_bcsh_switch_para", 0)
    r2 = our.call("emu_bcsh_sw", usec, ulut, ust, ul)
    if r2.stop:
        raise SystemExit("ours stopped: %s" % r2.stop)
    NSW += 1
    if WR or oem.read(OS["tispBcshRefresh"], 1)[0] != rf0 or oem.read(oinf, 0xc0) != info0:
        raise SystemExit("vendor tisp_bcsh_switch_para wrote the list / refresh flag / bcshInfo (case %d): the premise is wrong" % case)
    if r2.v0 or list(struct.unpack("<64I", our.read(ul, 0x100))) != old:
        bad = "switch: our list changed / return %d" % r2.v0
    elif blk(oem, ost, osec) != blk(our, ust, usec):
        bad = "switch: block / section"
    # 2. the updates
    steps = [rnd.choice("ec") for _ in range(rnd.choice([2, 3]))]
    for sn, kind in enumerate(steps):
        if bad:
            break
        if rnd.random() < 0.5:                                    # inputs of the parts an update's calc mode skips
            for o, n in ((0xe8, 4), (0x220, 1), (0x222, 1)):          # CT matrix word (hue), ap brightness / offset bytes
                v = struct.pack("<i", rnd.randrange(-0x2400, 0x2400))[:n] if n == 4 else bytes([rnd.getrandbits(8)])
                oem.write(ost + o, v); our.write(ust + o, v)
        cur = oem.read(ost, ST); WR.clear(); TRIG.clear()
        busy = rnd.random() < 0.3; DMA[0] = 1 if busy else 0
        oem.write(OS["tispBcshRefresh"], b"\x00")
        if kind == "e":
            klo, khi = struct.unpack_from("<II", cur, 8); hy = struct.unpack_from("<I", sec, 0x64)[0]
            k = rnd.random()
            if k < 0.35 and khi == M32:
                lo, hi = near(klo, khi, hy), 0
            elif k < 0.35:                                        # at the hysteresis edge of the kept EV
                v = (khi << 32 | klo) + rnd.choice([-1, 1]) * (hy + rnd.choice([-1, 0, 1, 2])); v = max(v, 0)
                lo, hi = v & M32, (v >> 32) & M32
            elif k < 0.5:
                lo, hi = rnd.getrandbits(32), rnd.choice([0x100, 0x101, 0xff, rnd.getrandbits(10)])
            else:
                lo, hi = rnd.getrandbits(rnd.choice([12, 20, 32])), rnd.choice([0, 0, 1, rnd.getrandbits(8)])
            blo, bhi = struct.unpack_from("<II", cur, 0x10)
            if rnd.random() < 0.4:
                bl = near(blo, bhi, hy)
            else:
                bl = rnd.getrandbits(rnd.choice([12, 24, 32]))
            bh = rnd.choice([0, 0, rnd.getrandbits(32)])
            r = oem.call("tisp_bcsh_ev_update", 0, 0, lo, hi, stack_args=(bl, bh))
            r2 = our.call("emu_bcsh_ev", usec, ust, lo, hi, stack_args=(bl, bh, ul))
            what = "ev %#x:%08x bv %#x:%08x mode %d" % (hi, lo, bh, bl, sec[0x4c0])
        else:
            kept = struct.unpack_from("<i", cur, 0x20)[0]
            k = rnd.random()
            if k < 0.3:
                hy = struct.unpack_from("<H", sec, 0xac)[0]
                ct = kept + rnd.choice([-hy, hy, -hy - 1, hy + 1, 0, rnd.randrange(-hy - 1, hy + 2)])
            elif k < 0.6:
                n = sec[0x4bf]; j = rnd.randrange(0, n)
                kc = struct.unpack_from("<i", lut, 40 * j)[0]; hy = struct.unpack_from("<H", sec, 0x8c + 2 * j)[0]
                ct = kc + rnd.choice([-hy, hy, 0]) + rnd.choice([-1, 0, 1])
            else:
                ct = rnd.randrange(1000, 25000)
            r = oem.call("tisp_bcsh_ct_update", 0, 0, ct & M32, 0)
            r2 = our.call("emu_bcsh_ct", usec, ulut, ust, ct & M32, stack_args=(ul,))
            what = "ct %d" % ct
        if r.stop or r2.stop:
            raise SystemExit("stopped: %s / %s" % (r.stop, r2.stop))
        NUP += 1
        rf = oem.read(OS["tispBcshRefresh"], 1)[0]
        if busy:
            if WR:
                raise SystemExit("vendor wrote slots with the DMA busy (case %d)" % case)
            vr = 1 if rf == 2 else 0
            if rf not in (0, 2):
                raise SystemExit("vendor refresh %d with the DMA busy (case %d)" % (rf, case))
            if vr:                                                # the deferred write: frame done, DMA idle
                NBUSY += 1; DMA[0] = 0
                run(oem, "tisp_bcsh_irq_frame_down", 0)
                if not WR:
                    raise SystemExit("vendor frame done wrote nothing at refresh 2 (case %d)" % case)
        else:
            vr = 1 if WR else 0
            if rf != vr:
                raise SystemExit("vendor refresh %d, slots written %d (case %d)" % (rf, len(WR), case))
        for s, v in WR:
            vw[s] = v
        NREB += vr
        lw = struct.unpack("<64I", our.read(ul, 0x100))
        uw = {i - 1: lw[i] for i in range(1, 64)}
        if (r2.v0 & 0xff) != vr:
            bad = "step %d %s: rebuilt oem %d our %d" % (sn, what, vr, r2.v0)
        elif blk(oem, ost, osec) != blk(our, ust, usec):
            bo, bu = blk(oem, ost, osec)[0], blk(our, ust, usec)[0]
            bad = "step %d %s: block / section, block diffs %s" % (sn, what, [hex(j) for j in range(ST) if bo[j] != bu[j]][:12])
        elif uw != vw:
            bad = "step %d %s (busy %d): list %s" % (sn, what, busy, [(s, hex(vw[s]), hex(uw[s])) for s in sorted(vw) if vw[s] != uw[s]][:6])
    if bad:
        FAIL += 1
        if FAIL <= 6:
            print("MISMATCH case %d (%s) %s" % (case, "".join(steps), bad))
print("bcshlist: %d cases (%d switches without a list write, %d updates, %d rebuilt, %d through the frame done), %d mismatches" % (
    a.n, NSW, NUP, NREB, NBUSY, FAIL))
sys.exit(1 if FAIL else 0)
