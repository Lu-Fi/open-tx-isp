#!/usr/bin/env python3
"""diff_t33_bcshct.py OEM.ko BCSHCT_EMU.o [--n 2000] [--seed 7]

T33 BCSH CT update (openimp #507): the vendor tx-isp-t33.ko tisp_bcsh_ct_update(0) (interp_by_ct not forced at the
CT in $a2, the frame counter, calc_para_refresh(2), params_transform, the 54 list writes) and our
t33_bcsh_ct_update (tests/t33_binoracle/bcshct.c, built by build_emu.sh) on the same random bcsh_para block,
section 19 and CT knots (cases from diff_t33_bcshsw.py, the CT near the kept one / a knot edge so both gates and
the rebuild are taken).  Vendor: bcsh_para[0] = block, bcshInfo[0] = a zeroed info buffer, bcshCfgMode = 1 (the
writes go to list slots), tisp_rcfg_get_dma_state = 0, tisp_reg_write_t33 recorded (slot -> word).  Compared: the
whole block but word 0, the section, the return value (1 = rebuilt) and the written slots.  Nothing of the vendor
module is stored here.
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
OS = {k: oem.symaddr[k] for k in ("bcsh_para", "tparamsP", "stLutInfo", "bcshInfo", "bcshCfgMode")}
WR = []
for n, f in (("tisp_reg_write_t33", lambda e: (WR.append((e.get_reg(UC_MIPS_REG_A2), e.get_reg(UC_MIPS_REG_A3))), 0)[1]),
             ("tisp_rcfg_get_dma_state", lambda e: 0)):
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


FAIL = 0
SENT = 0xdeadbeef
for case in range(a.n):
    st, sec, lut = mk_case()
    st = bytearray(st)
    kept = struct.unpack_from("<i", st, 0x20)[0]
    k = rnd.random()
    if k < 0.3:                                                   # within / at the CT hysteresis sec + 0xac
        hy = struct.unpack_from("<H", sec, 0xac)[0]
        ct = kept + rnd.choice([-hy, hy, -hy - 1, hy + 1, 0, rnd.randrange(-hy - 1, hy + 2)])
    elif k < 0.6:                                                 # on a knot / its hysteresis edge
        n = sec[0x4bf]; j = rnd.randrange(0, n)
        kc = struct.unpack_from("<i", lut, 40 * j)[0]; hy = struct.unpack_from("<H", sec, 0x8c + 2 * j)[0]
        ct = kc + rnd.choice([-hy, hy, 0]) + rnd.choice([-1, 0, 1])
    else:
        ct = rnd.randrange(1000, 25000)
    struct.pack_into("<I", st, 0x24, rnd.choice([rnd.randrange(0, 20), 0xff, struct.unpack_from("<I", st, 0x24)[0]]))
    st[0x29] = rnd.choice([0, 0xfe, 0xff, rnd.getrandbits(8)])
    st = bytes(st)
    sec = bytearray(sec)                                          # the hue selection of calc_para_refresh(2)
    sec[0x4a7] = rnd.choice([0, 1, rnd.getrandbits(8)]); sec[0x4a8] = rnd.choice([0, 1, 2]); sec[0x4a9] = rnd.choice([0, 2, 3])
    sec = bytes(sec)
    oem.heap_reset(); our.heap_reset(); WR.clear()
    tp = oem.alloc(0x8b78 + SEC + 0x10); osec = tp + 0x8b78; oem.write(osec, sec)
    ost = oem.alloc(ST, st); olut = oem.alloc(len(lut), lut); oli = oem.alloc(0x40, bytes(0x40)); oinf = oem.alloc(0xc0, bytes(0xc0))
    w32(oem, oli + 0x1c, olut); w32(oem, OS["stLutInfo"], oli); w32(oem, OS["bcsh_para"], ost); w32(oem, OS["tparamsP"], tp)
    w32(oem, OS["bcshInfo"], oinf); oem.write(OS["bcshCfgMode"], b"\x01")
    w32(oem, ost, osec)
    usec = our.alloc(SEC + 0x10, sec); ust = our.alloc(ST, st); ulut = our.alloc(len(lut), lut)
    ul = our.alloc(0x40 * 4, struct.pack("<64I", *([SENT] * 64)))
    r = oem.call("tisp_bcsh_ct_update", 0, 0, ct & M32, 0)
    if r.stop:
        raise SystemExit("vendor stopped: %s" % r.stop)
    vr = 1 if WR else 0
    r2 = our.call("emu_bcsh_ct", usec, ulut, ust, ct & M32, stack_args=(ul,))
    if r2.stop:
        raise SystemExit("ours stopped: %s" % r2.stop)
    from unicorn.mips_const import UC_MIPS_REG_V0
    orr = our.get_reg(UC_MIPS_REG_V0)
    lw = struct.unpack("<64I", our.read(ul, 0x100))
    vw = {}
    for s, v in WR:
        vw[s] = v
    uw = {i - 1: lw[i] for i in range(1, 64) if lw[i] != SENT}
    bo = {"st": bytes(4) + oem.read(ost + 4, ST - 4), "sec": oem.read(osec, SEC), "ret": bytes([vr]), "list": repr(sorted(vw.items())).encode()}
    bu = {"st": bytes(4) + our.read(ust + 4, ST - 4), "sec": our.read(usec, SEC), "ret": bytes([orr & 0xff]), "list": repr(sorted(uw.items())).encode()}
    for k in bo:
        if bo[k] != bu[k]:
            FAIL += 1
            if FAIL <= 6:
                if k == "list":
                    print("  list oem", [(hex(s), hex(v)) for s, v in sorted(vw.items()) if uw.get(s) != v][:6], "our", [(hex(s), hex(v)) for s, v in sorted(uw.items()) if vw.get(s) != v][:6])
                else:
                    d = [j for j in range(len(bo[k])) if bo[k][j] != bu[k][j]]
                    print("  diffs", [hex(j) for j in d][:16])
                print("MISMATCH case %d %s (ct %d kept %d 4a2 %d 4a3 %d 1d0 %d)" % (case, k, ct, kept, sec[0x4a2], sec[0x4a3], st[0x1d0]))
            break
print("bcshct: %d cases, %d mismatches" % (a.n, FAIL))
sys.exit(1 if FAIL else 0)
