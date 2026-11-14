#!/usr/bin/env python3
"""diff_t33_clmct.py OEM.ko CLMCT_EMU.o [--n 2000] [--seed 7]

T33 CLM CT update (openimp #507): the vendor tx-isp-t33.ko tisp_clm_ct_interp(0, ct, 0) (the not-forced gate on the
kept CT / segment / hysteresis of clm_info, then the table interpolation into clm_info + 0x64 / + 0x89a) and ours
(tests/t33_binoracle/clmct.c: t33_clm_ct_gate + t33_clm_ct, driver/t32/tx_isp_t33_clm.h) on the same random
section 20 (0x40 bytes), knot tables (u16 tables first, CT rising, then byte tables) and clm_info state.  Vendor:
clm_info[0] = info (word 0 = section), stLutInfo[0] + 0x24 / + 0x28 = the u16 / byte tables.  Compared: return
value and the whole info block but word 0.  Nothing of the vendor module is stored here.
"""
import argparse, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu  # noqa: E402
from unicorn.mips_const import UC_MIPS_REG_V0  # noqa: E402

ap = argparse.ArgumentParser(); ap.add_argument("oem"); ap.add_argument("our")
ap.add_argument("--n", type=int, default=2000); ap.add_argument("--seed", type=int, default=7)
a = ap.parse_args(); rnd = random.Random(a.seed); M32 = 0xffffffff
INFO, SEC, K16, K8 = 0xd00, 0x40, 0x836, 0x41a
H = {"isp_printf": lambda e: 0, "printk": lambda e: 0}
oem = KoEmu(a.oem, hle=H, max_insns=20_000_000)
our = KoEmu(a.our, hle=H, max_insns=20_000_000)
OS = {k: oem.symaddr[k] for k in ("clm_info", "stLutInfo")}


def w32(e, ad, v): e.write(ad, struct.pack("<I", v & M32))


def mk_case():
    n = rnd.randrange(1, 6)
    sec = bytearray(rnd.randbytes(SEC)); sec[0x3b] = n
    for k in range(n):
        struct.pack_into("<H", sec, 0x1a + 2 * k, rnd.choice([0, 1, rnd.randrange(0, 400)]))
    t16 = bytearray(); ct = rnd.randrange(1500, 3500); kc = []
    for k in range(n):
        kc.append(ct)
        t16 += struct.pack("<h", ct) + rnd.randbytes(K16 - 2)
        ct += rnd.randrange(1, 2500)
    t8 = rnd.randbytes(n * K8)
    info = bytearray(rnd.randbytes(INFO))
    last = rnd.choice([rnd.randrange(1000, 12000), kc[rnd.randrange(n)]])
    struct.pack_into("<I", info, 0x60, last)
    info[0xcbc] = rnd.choice([0, 1, 2, 3, 4, n, rnd.getrandbits(8)])
    struct.pack_into("<H", info, 0x898, rnd.choice([0, 1, 1, rnd.randrange(0, 300)]))
    k = rnd.random()
    if k < 0.3:
        hy = struct.unpack_from("<H", info, 0x898)[0]
        c = last + rnd.choice([-hy, hy, -hy - 1, hy + 1, 0])
    elif k < 0.7:
        j = rnd.randrange(n); hy = struct.unpack_from("<H", sec, 0x1a + 2 * j)[0]
        c = kc[j] + rnd.choice([-hy, hy, 0]) + rnd.choice([-1, 0, 1])
    else:
        c = rnd.randrange(1000, 15000)
    return bytes(sec), bytes(t16), t8, bytes(info), c


FAIL = 0
for case in range(a.n):
    sec, t16, t8, info, ct = mk_case()
    oem.heap_reset(); our.heap_reset()
    osec = oem.alloc(SEC, sec); oinf = oem.alloc(INFO, info); o16 = oem.alloc(len(t16), t16); o8 = oem.alloc(len(t8), t8)
    oli = oem.alloc(0x40, bytes(0x40)); w32(oem, oli + 0x24, o16); w32(oem, oli + 0x28, o8)
    w32(oem, oinf, osec); w32(oem, OS["clm_info"], oinf); w32(oem, OS["stLutInfo"], oli)
    lut = t16 + t8
    usec = our.alloc(SEC, sec); uinf = our.alloc(INFO, info); ulut = our.alloc(len(lut), lut)
    r = oem.call("tisp_clm_ct_interp", 0, ct & M32, 0)
    if r.stop:
        raise SystemExit("vendor stopped: %s" % r.stop)
    vr = oem.get_reg(UC_MIPS_REG_V0)
    r = our.call("emu_clm_ct", uinf, usec, ulut, len(lut), stack_args=(ct & M32,))
    if r.stop:
        raise SystemExit("ours stopped: %s" % r.stop)
    orr = our.get_reg(UC_MIPS_REG_V0)
    bo = {"ret": vr, "info": oem.read(oinf + 4, INFO - 4)}
    bu = {"ret": orr, "info": our.read(uinf + 4, INFO - 4)}
    for k in bo:
        if bo[k] != bu[k]:
            FAIL += 1
            if FAIL <= 6:
                if k == "info":
                    d = [j + 4 for j in range(len(bo[k])) if bo[k][j] != bu[k][j]]
                    print("  diffs", [hex(j) for j in d][:12])
                print("MISMATCH case %d %s (ct %d n %d ret %#x / %#x)" % (case, k, ct, sec[0x3b], vr, orr))
            break
print("clmct: %d cases, %d mismatches" % (a.n, FAIL))
sys.exit(1 if FAIL else 0)
