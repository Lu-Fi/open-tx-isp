#!/usr/bin/env python3
"""diff_t33_mdnspr.py OEM.ko MDNSPR_EMU.o [--n 300] [--seed 7] [--bins day.bin,night.bin] [--gains trace,...]

T33 MDNS init + gain refresh chain (openimp #99): the vendor tx-isp-t33.ko tisp_mdns_init(ch, ip = stIspInitPar + ch * 1008), tisp_mdns_refresh(ch,
gain) (-> tisp_mdns_par_refresh: 88 KB, the gain gate, binpara_update, 1020 gain interpolations, func_mdns_para_update,
all MDNS register fields, tisp_mdns_t33_reg_trig) and tisp_mdns_switch_para(ch) against ours
(tests/t33_binoracle/mdnspr.c, built by build_emu.sh: every callee in our form).  Same module state on both sides:
stIspInitPar (sensor size, zip ratio byte 46, init gain word 232, gain tolerance word 250, single-sensor word 226),
mdnsCfgMode, riscvEnable / riscvMdnsEn / stRiscvPara, tparamsP[ch] with the MDNS section at +25144.

Inputs: the MDNS sections (IQ section 16) of the real IQ bins given with --bins (day / night) or random sections;
gain sequences from real QEMU vendor runs (--gains: qemu-guest.log files, the per-frame sensor gain of the
image_sensor_exposure trace, 1/1024, as linear 16.16 and as log2 16.16: the long gain the driver passes is a log2 value,
e.g. 0xb7 at the end of the QEMU day run) or random walks.  Each case runs the init, then a sequence of refreshes
(the recorded gains, gate edges +-1 around the tolerance, jumps) with day/night section switches (the switch's
tparamsP points at the other bin's section) and state flips between calls (cfg mode, uvLimitFlag, stMemInfo WDR
byte).  After every call compared: the ordered hardware calls (tisp_reg_write_t33 (ch, blk, slot|reg, value),
system_reg_write, tisp_mdns_t33_reg_trig, tisp_rcfg_module_register, system_irq_func_set, allocations), the whole
block (8808 bytes), the five threshold tables, stMemInfo, uvLimitFlag, mdnsInit, tispMdnsRefresh.
Nothing of the vendor module is stored here.
"""
import argparse, math, os, random, re, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu  # noqa: E402
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3, UC_MIPS_REG_PC  # noqa: E402

ap = argparse.ArgumentParser(); ap.add_argument("oem"); ap.add_argument("our")
ap.add_argument("--n", type=int, default=300); ap.add_argument("--seed", type=int, default=7)
ap.add_argument("--bins", default=""); ap.add_argument("--gains", default="")
ap.add_argument("--steps", type=int, default=12)
a = ap.parse_args(); rnd = random.Random(a.seed); M32 = 0xffffffff
BLK, SEC, TPOFF, TPSZ = 8808, 9844, 25144, 25144 + 9844 + 64
TABLES = ("global_ybsnthres", "global_ypbv0stathres", "global_ypbv1stathres", "global_yrefweimin",
          "global_yrefweiminadjthres")


def iq_section(b, idx):
    """t32_iq_section (TX_ISP_T33 payload rules): (offset, length) of section idx or None"""
    off, ln, en = 0xa0, 0, 0
    pay = {7: (0x94, 0x3602), 12: (0x75e, 0x28), 13: (0x46, 0x10a), 19: (0x4bf, 0x28)}
    for i in range(idx + 1):
        e = 0x14 + 4 * i
        en = struct.unpack_from("<H", b, e)[0] == 1; ln = struct.unpack_from("<H", b, e + 2)[0]
        if i < idx and en:
            s = b[off:]; p = 0
            if i in pay:
                o, m = pay[i]; p = (s[o] * m) & 0xffff if len(s) > o else 0
            elif i == 20:
                p = ((s[0x3b] * 0x836) & 0xffff) + ((s[0x3b] * 0x41a) & 0xffff) if len(s) > 0x3b else 0
            off += ln + p
    return (off, ln) if en else None


SECS = []
for p in filter(None, a.bins.split(",")):
    b = open(p, "rb").read(); r = iq_section(b, 16)
    if not r or r[1] != SEC:
        raise SystemExit("%s: no %d byte MDNS section 16 (%s)" % (p, SEC, r))
    SECS.append(b[r[0]:r[0] + SEC])
GAINS = []
for p in filter(None, a.gains.split(",")):
    g = [int(m.group(1)) * 64 for m in re.finditer(r"image_sensor_exposure input 0 exposure \d+/16 lines, gain (\d+)/1024",
                                                   open(p, errors="replace").read())]
    if len(g) < 10:
        raise SystemExit("%s: no image_sensor_exposure gain trace" % p)
    GAINS.append(g)                                                # linear 16.16: table indexes 1..
    GAINS.append([max(0, round(math.log2(x / 65536.0) * 65536)) if x else 0 for x in g])  # log2 16.16 (tisp_long_tgain_update's domain)

REC = {"oem": [], "our": []}


def hw(side, name, nargs, ret=0):
    def f(e):
        REC[side].append((name,) + tuple(e.get_reg(r) for r in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2,
                                                                 UC_MIPS_REG_A3)[:nargs]))
        return ret(e) if callable(ret) else ret
    return f


def kmalloc(side):
    def f(e):
        n = e.get_reg(UC_MIPS_REG_A0); REC[side].append(("kmalloc", n, e.get_reg(UC_MIPS_REG_A1)))
        return e.alloc(n, bytes(rnd_fill[0][:n]) if n <= len(rnd_fill[0]) else None)
    return f


rnd_fill = [bytes(BLK)]       # what the allocator hands out (the vendor's kmalloc memory is not cleared)


def mk(path, side):
    H = {"isp_printf": lambda e: 0, "printk": lambda e: 0}
    e = KoEmu(path, hle=H, max_insns=60_000_000)
    for n, k, ret in (("tisp_reg_write_t33", 4, 0), ("system_reg_write", 2, 0), ("tisp_mdns_t33_reg_trig", 1, 0),
                      ("tisp_rcfg_module_register", 2, 0), ("system_irq_func_set", 2, 0),
                      ("private_raw_mutex_init", 0, 0)):
        f = hw(side, n, k, ret)
        if n in e.symaddr:
            e.stubname[e.symaddr[n]] = n
        e.hle[n] = f
    f = kmalloc(side)
    if "private_kmalloc" in e.symaddr:
        e.stubname[e.symaddr["private_kmalloc"]] = "private_kmalloc"
    e.hle["private_kmalloc"] = f
    return e


oem = mk(a.oem, "oem"); our = mk(a.our, "our")
G = ("mdnsInfo", "tparamsP", "stMemInfo", "uvLimitFlag", "mdnsCfgMode", "riscvEnable", "riscvMdnsEn", "stRiscvPara",
     "stIspInitPar", "mdnsInit", "tispMdnsRefresh") + TABLES
SZ = {"mdnsInfo": 16, "tparamsP": 16, "stMemInfo": 304, "uvLimitFlag": 4, "mdnsCfgMode": 1, "riscvEnable": 4,
      "riscvMdnsEn": 1, "stRiscvPara": 51200, "stIspInitPar": 4032, "mdnsInit": 1, "tispMdnsRefresh": 1}
for t in TABLES:
    SZ[t] = 44


def gput(name, data, off=0):
    for e in (oem, our):
        e.write(e.symaddr[name] + off, data)


def gget(e, name):
    return e.read(e.symaddr[name], SZ[name])


def run(e, fn, *args):
    r = e.call(fn, *args)
    if r.stop:
        raise SystemExit("emulation stopped in %s: %s (%s)" % (fn, r.stop, e.where(e.get_reg(UC_MIPS_REG_PC))))
    return r


def section():
    if SECS and rnd.random() < 0.8:
        s = bytearray(rnd.choice(SECS))
        if rnd.random() < 0.25:                                    # a few bytes of the real section changed
            for _ in range(rnd.randrange(1, 40)):
                s[rnd.randrange(SEC)] = rnd.getrandbits(8)
        return bytes(s)
    return rnd.randbytes(SEC)


def gain_seq(n):
    if GAINS and rnd.random() < 0.7:
        g = rnd.choice(GAINS); k = rnd.randrange(0, max(1, len(g) - n))
        return g[k:k + n]
    g, out = rnd.choice([0x10000, 0x20000, 0x80000, rnd.getrandbits(24), rnd.getrandbits(12), 0xb7, 0]), []
    for _ in range(n):
        g = max(0, min(M32, g + rnd.choice([0, 1, -1, 0xff, 0x100, -0x100, 0x101, rnd.randrange(-0x40000, 0x40000)])))
        if rnd.random() < 0.05:
            g = rnd.choice([0, M32, 0x7fffffff, 0x80000000, 0xa0000, 0xb0000, rnd.getrandbits(32)])
        out.append(g)
    return out


def le32(x): return struct.pack("<I", x & M32)


FAIL = 0; CALLS = 0; WRITES = 0; FULL = 0


def compare(case, step, what):
    global FAIL
    ro, ru = REC["oem"], REC["our"]
    bo = {"calls": ro}; bu = {"calls": ru}
    for k in ("mdnsInit", "tispMdnsRefresh", "stMemInfo", "uvLimitFlag") + TABLES:
        bo[k] = gget(oem, k); bu[k] = gget(our, k)
    po = struct.unpack("<I", oem.read(oem.symaddr["mdnsInfo"] + 4 * CH, 4))[0]
    pu = struct.unpack("<I", our.read(our.symaddr["mdnsInfo"] + 4 * CH, 4))[0]
    bo["blkptr"] = po; bu["blkptr"] = pu
    if po and pu:
        bo["blk"] = oem.read(po, BLK); bu["blk"] = our.read(pu, BLK)
    for k in bo:
        if bo[k] != bu[k]:
            FAIL += 1
            if FAIL <= 4:
                if k == "calls":
                    d = next((i for i in range(min(len(ro), len(ru))) if ro[i] != ru[i]), min(len(ro), len(ru)))
                    print("MISMATCH case %d step %d %s: calls %d/%d, first #%d oem %s our %s" % (
                        case, step, what, len(ro), len(ru), d, ro[d:d + 3], ru[d:d + 3]))
                elif k == "blk":
                    d = [j for j in range(0, BLK, 4) if bo[k][j:j + 4] != bu[k][j:j + 4]]
                    print("MISMATCH case %d step %d %s: block words %s oem %s our %s" % (
                        case, step, what, [j // 4 for j in d[:8]], bo[k][d[0]:d[0] + 4].hex(), bu[k][d[0]:d[0] + 4].hex()))
                else:
                    print("MISMATCH case %d step %d %s: %s oem %s our %s" % (case, step, what, k, bo[k], bu[k]))
            print("mdnspr: stopped at the first mismatch (case %d of %d)" % (case, a.n))
            sys.exit(1)
    return True


for case in range(a.n):
    CH = rnd.choice([0, 0, 0, 1, 3])
    for e in (oem, our):
        e.heap_reset()
        for name in G:
            e.write(e.symaddr[name], bytes(SZ[name]))
    secs = [section(), section()]
    tps = []
    for s in secs:                                                 # same heap order: same addresses on both sides
        junk = rnd.randbytes(TPOFF)
        ad = [e.alloc(TPSZ, junk + s + bytes(64)) for e in (oem, our)]
        assert ad[0] == ad[1]; tps.append(ad[0])
    rnd_fill[0] = rnd.choice([bytes(BLK), rnd.randbytes(BLK)])
    gput("tparamsP", le32(tps[0]), 4 * CH)
    ip = bytearray(rnd.randbytes(1008)) if rnd.random() < 0.3 else bytearray(1008)
    w, h = rnd.choice([(1920, 1080), (2560, 1440), (1280, 720), (rnd.randrange(64, 4096), rnd.randrange(64, 3000))])
    struct.pack_into("<HH", ip, 12, w, h)
    ip[46] = rnd.choice([10, 10, 11, 12, 15, 20, rnd.randrange(11, 21), 21, 9, rnd.getrandbits(8)])
    struct.pack_into("<I", ip, 928, rnd.choice([0x10000, 0x10000, rnd.getrandbits(20), M32]))
    struct.pack_into("<I", ip, 1000, rnd.choice([0x100, 0x100, 0x100, 0, 1, rnd.getrandbits(16), M32]))
    stip = bytearray(4032); stip[CH * 1008:CH * 1008 + 1008] = ip
    struct.pack_into("<I", stip, 904, rnd.choice([1, 1, 0, 2]))
    gput("stIspInitPar", bytes(stip))
    gput("mdnsCfgMode", bytes([rnd.choice([1, 1, 0])]))
    rv = bytearray(4); rv[CH] = rnd.choice([0, 0, 0, 1, 2]); gput("riscvEnable", bytes(rv))
    gput("riscvMdnsEn", bytes([rnd.choice([0, 0, 1])]))
    gput("stRiscvPara", rnd.randbytes(51200))
    stm = bytearray(304)
    if rnd.random() < 0.3:
        stm = bytearray(rnd.randbytes(304))
    gput("stMemInfo", bytes(stm))
    for k in REC:
        REC[k].clear()
    run(oem, "tisp_mdns_init", CH, oem.symaddr["stIspInitPar"] + CH * 1008)
    run(our, "emu_mdns_init", CH, our.symaddr["stIspInitPar"] + CH * 1008)
    CALLS += len(REC["oem"])
    if not compare(case, 0, "init"):
        continue
    cur = 0
    seq = gain_seq(a.steps)
    if rnd.random() < 0.15:                                       # the first refresh always passes the gate:
        seq[0] = rnd.choice([0x80000000, 0xffff0000, M32, 0x7fffffff, 0xa0000, 0xa8000, 0x9ffff, 0xbffff]) \
            if rnd.random() < 0.7 else rnd.getrandbits(32)       # signed gain index / table end
    for st, g in enumerate(seq, 1):
        what = "refresh %#x" % g
        k = rnd.random()
        if k < 0.1:                                               # day/night switch to the other section
            cur ^= 1; gput("tparamsP", le32(tps[cur]), 4 * CH); what = "switch"
        elif k < 0.18:
            gput("mdnsCfgMode", bytes([rnd.choice([0, 1])]))
        elif k < 0.24:
            gput("uvLimitFlag", bytes([rnd.choice([0, 1, rnd.getrandbits(8)])]), CH)
        elif k < 0.3:
            gput("stMemInfo", bytes([rnd.choice([0, 1, 2])]), CH * 76 + 11)
        elif k < 0.36:                                             # a gate edge: the tolerance +- 1 off the last gain
            po = struct.unpack("<I", oem.read(oem.symaddr["mdnsInfo"] + 4 * CH, 4))[0]
            last, thr = struct.unpack("<II", oem.read(po + 2181 * 4, 8))
            g = (last + rnd.choice([thr, thr - 1, -thr, -thr + 1, 0])) & M32; what = "refresh %#x (edge)" % g
        for kk in REC:
            REC[kk].clear()
        if what == "switch":
            run(oem, "tisp_mdns_switch_para", CH); run(our, "emu_mdns_switch", CH)
        else:
            run(oem, "tisp_mdns_refresh", CH, g); run(our, "tisp_mdns_refresh", CH, g)
        n = sum(1 for c in REC["oem"] if c[0] == "tisp_reg_write_t33")
        CALLS += len(REC["oem"]); WRITES += n; FULL += n > 400
        if not compare(case, st, what):
            break
print("mdnspr: %d cases, %d calls compared (%d register writes, %d full refreshes/switches), %d mismatches" % (
    a.n, CALLS, WRITES, FULL, FAIL))
sys.exit(1 if FAIL else 0)
