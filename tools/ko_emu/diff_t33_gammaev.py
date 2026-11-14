#!/usr/bin/env python3
"""diff_t33_gammaev.py OEM.ko GAMMAEV_EMU.o [--n 2000] [--seed 7]

T33 gamma EV update and bin switch (openimp #507): the vendor tx-isp-t33.ko tisp_gamma_ev_update(0) / tisp_gamma_switch_para(0) (EV clamp, interp_by_ev not
forced, update_curve, strength_transform + write_lut_rgb on a new strength) and our t33_gamma_ev_update
(tests/t33_binoracle/gammaev.c, built by build_emu.sh) on the same random section 13, curve table, strength byte,
curve state and EV.  Vendor: gamma_para[0] = { section, strength buffer, EV words, sp, curve, ap }, stLutInfo[0]
+ 0x20 = the curve table, gammaCfgMode = 1 (the writes go to list slots), tisp_rcfg_get_dma_state = 0,
tisp_reg_write_t33 recorded (slot -> word).  Compared: the return value, the strength byte, the EV words, the
curve state and the written slots.  Nothing of the vendor module is stored here.
"""
import argparse, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu  # noqa: E402
from unicorn.mips_const import UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_V0  # noqa: E402

ap = argparse.ArgumentParser(); ap.add_argument("oem"); ap.add_argument("our")
ap.add_argument("--n", type=int, default=2000); ap.add_argument("--seed", type=int, default=7)
a = ap.parse_args(); rnd = random.Random(a.seed); M32 = 0xffffffff
SEC, CV, NW = 0x48, 0x10a, 0x120

H = {"isp_printf": lambda e: 0, "printk": lambda e: 0}
oem = KoEmu(a.oem, hle=H, max_insns=20_000_000)
our = KoEmu(a.our, hle=H, max_insns=20_000_000)
OS = {k: oem.symaddr[k] for k in ("gamma_para", "stLutInfo", "gammaCfgMode", "tparamsP")}
WR = []
for n, f in (("tisp_reg_write_t33", lambda e: (WR.append((e.get_reg(UC_MIPS_REG_A2), e.get_reg(UC_MIPS_REG_A3))), 0)[1]),
             ("tisp_rcfg_get_dma_state", lambda e: 0), ("__private_spin_lock_irqsave", lambda e: 0),
             ("private_spin_unlock_irqrestore", lambda e: 0)):
    oem.stubname[oem.symaddr[n]] = n; oem.hle[n] = f


def w32(e, ad, v): e.write(ad, struct.pack("<I", v & M32))


def pick(k):
    return rnd.choice(k)


def mk_case():
    sec = bytearray(rnd.randbytes(SEC))
    struct.pack_into("<I", sec, 0, pick([0x01350181, 0x01350181, 0x01350182, 0x01350180]))
    v = rnd.randrange(0, 0x40)
    for k in range(12):                                           # EV points rising (equal ones: vendor error)
        struct.pack_into("<I", sec, 4 + 4 * k, v)
        v = min(v + pick([0, rnd.randrange(1, 0x40), rnd.randrange(1, 0x800), rnd.randrange(1, 0x8000)]), M32)
    sec[0x34] = pick([0, 1, 1, 1, 1, 2]); sec[0x42] = pick([0, 0, 0, 0, 1, 2]); sec[0x43] = pick([0, 0, 0, 1, 2])
    n = rnd.randrange(1, 7); sec[0x46] = n
    tab = bytearray()
    b = rnd.randrange(-0x40, 0x400)
    for k in range(n):
        lo = b; hi = lo + pick([0, rnd.randrange(0, 0x800)])
        tab += b"".join(struct.pack("<H", rnd.getrandbits(pick([12, 12, 16]))) for _ in range(0x81))
        tab += struct.pack("<ii", lo, hi)
        b = hi + pick([0, rnd.randrange(1, 0x2000)])
    return bytes(sec), bytes(tab)


def ev_for(sec, tab):
    k = rnd.random()
    if k < 0.4:                                                   # near an EV point
        e = struct.unpack_from("<I", sec, 4 + 4 * rnd.randrange(12))[0] + pick([-1, 0, 1, rnd.randrange(-0x40, 0x40)])
    elif k < 0.7:                                                 # near a curve bound
        j = rnd.randrange(sec[0x46]); e = struct.unpack_from("<i", tab, j * CV + pick([0x102, 0x106]))[0] + pick([-1, 0, 1])
    else:
        e = rnd.getrandbits(pick([8, 16, 24]))
    e &= M32
    lo = ((e << 10) & M32) | rnd.getrandbits(10); hi = (e >> 22) & M32
    if rnd.random() < 0.15:
        hi = pick([0x100, 0x101, rnd.getrandbits(32)]); lo = pick([0, lo])
    return lo, hi


FAIL = 0
SENT = 0xdeadbeef
for case in range(a.n):
    sec, tab = mk_case()
    lo, hi = ev_for(sec, tab)
    st = bytearray(rnd.randbytes(0x104)); st[0] = pick([0xff, sec[0x35], sec[0x36], rnd.getrandbits(8)])
    cur = rnd.randbytes(0x102); ip0 = rnd.randbytes(8)
    sw = rnd.random() < 0.5                                       # tisp_gamma_switch_para (forced, API curve)
    if sw:
        if rnd.random() < 0.8:                                    # kept EV words near the knots / bounds
            ip0 = struct.pack("<II", lo, hi)
        sec = bytearray(sec); sec[0x44] = pick([0, 1, 1]); sec[0x45] = pick([0, 0, 1]); sec = bytes(sec)
    else:
        sec = bytearray(sec); sec[0x44] = pick([0, 1, 1]); sec = bytes(sec)
    apc = b"".join(struct.pack("<H", rnd.getrandbits(12)) for _ in range(0x81))
    oem.heap_reset(); our.heap_reset(); WR.clear()
    tp = oem.alloc(0x5b80 + SEC + 0x10); osec = tp + 0x5b80; oem.write(osec, sec)
    otab = oem.alloc(len(tab), tab); ostr = oem.alloc(0x104, bytes(st))
    oip = oem.alloc(8, ip0); osp = oem.alloc(0x104, bytes(0x104)); ocur = oem.alloc(0x102 + 2, cur + b"\0\0")
    oap = oem.alloc(0x108, bytes(4) + apc + bytes(2)); oblk = oem.alloc(0x40, bytes(0x40)); oli = oem.alloc(0x40, bytes(0x40))
    for i, p in enumerate((osec, ostr, oip, osp, ocur, oap)):
        w32(oem, oblk + 4 * i, p)
    w32(oem, OS["gamma_para"], oblk); w32(oem, oli + 0x20, otab); w32(oem, OS["stLutInfo"], oli); w32(oem, OS["tparamsP"], tp)
    oem.write(OS["gammaCfgMode"], b"\x01")
    usec = our.alloc(SEC + 0x10, sec); utab = our.alloc(len(tab), tab); ustr = our.alloc(0x104, bytes(st))
    uip = our.alloc(8, ip0); ucur = our.alloc(0x102 + 2, cur + b"\0\0"); uap = our.alloc(0x104, apc + bytes(2))
    ul = our.alloc(NW * 4, struct.pack("<%dI" % NW, *([SENT] * NW)))
    if sw:
        r = oem.call("tisp_gamma_switch_para", 0, 0)
    else:
        r = oem.call("tisp_gamma_ev_update", 0, 0, lo, hi)
    if r.stop:
        raise SystemExit("vendor stopped: %s" % r.stop)
    vr = 0 if sw else oem.get_reg(UC_MIPS_REG_V0)
    if sw:
        r2 = our.call("emu_gamma_sw", usec, utab, uip, ustr, stack_args=(ucur, uap, ul))
    else:
        r2 = our.call("emu_gamma_ev", usec, utab, lo, hi, stack_args=(ustr, uip, ucur, uap, ul))
    if r2.stop:
        raise SystemExit("ours stopped: %s" % r2.stop)
    ur = 0 if sw else our.get_reg(UC_MIPS_REG_V0)
    lw = struct.unpack("<%dI" % NW, our.read(ul, NW * 4))
    vw = dict(WR)
    uw = {i - 1: lw[i] for i in range(1, NW) if lw[i] != SENT}
    bo = {"ret": vr, "str": oem.read(ostr, 1), "ip": oem.read(oip, 8), "curve": oem.read(ocur, 0x102), "ap": oem.read(oap + 4, 0x102), "list": sorted(vw.items())}
    bu = {"ret": ur, "str": our.read(ustr, 1), "ip": our.read(uip, 8), "curve": our.read(ucur, 0x102), "ap": our.read(uap, 0x102), "list": sorted(uw.items())}
    for k in bo:
        if bo[k] != bu[k]:
            FAIL += 1
            if FAIL <= 6:
                print("MISMATCH case %d sw %d %s: oem %s our %s (34 %d 42 %d 43 %d n %d e %#x)" % (
                    case, sw, k, str(bo[k])[:80], str(bu[k])[:80], sec[0x34], sec[0x42], sec[0x43], sec[0x46],
                    ((hi << 22) | (lo >> 10)) & M32))
            break
print("gammaev: %d cases, %d mismatches" % (a.n, FAIL))
sys.exit(1 if FAIL else 0)
