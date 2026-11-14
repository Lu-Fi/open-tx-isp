#!/usr/bin/env python3
"""diff_expo_fs.py OEM.ko HARNESS [--n 400] [--seed 7] [--kinds TGFDV]

T40 frame-start sensor apply (openimp #505): driver/t40/tx_isp_t40_expo_fs.h (built into tests/t40_expo_fs/harness.c) against
the instructions of the vendor tx-isp-t40.ko under Unicorn, on the same random state:
  T  sensor_set_integration_time(it, ctx)   (ctx+0xc = channel)      vs t40_efs_set_it
  G  sensor_set_analog_gain(again, ctx)     (*ctx = channel)         vs t40_efs_set_again
  F  ispcore_irq_main_fs_work()                                      vs t40_efs_slots_take(fs = 1)
  D  ispcore_irq_main_fd_work()                                      vs t40_efs_slots_take(fs = 0)
  V  isp_vic_interrupt_service_routine (VIC0 / VIC1 / VIC2, status1 0) vs t40_vic_irq_account
Compared: the core slot block 0x300..0x38f and the sensor attribute (T/G), the sensor commands in order (F/D: the vendor's
ispcore_sensor_ops_ioctl is replaced by a recorder; tisp_get_ae_tgain by a stub, the fd worker's core-ioctl pointer is NULL),
the VIC device counters 0x2c0..0x2ef and the queued work (V).  Nothing of the vendor module is stored here.
"""
import argparse, os, random, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu, UC_MIPS_REG_A1, UC_MIPS_REG_A2
ap = argparse.ArgumentParser(); ap.add_argument("oem"); ap.add_argument("harness")
ap.add_argument("--n", type=int, default=400); ap.add_argument("--seed", type=int, default=7); ap.add_argument("--kinds", default="TGFDV")
a = ap.parse_args(); rnd = random.Random(a.seed)

calls = []; works = []
def rec(e):
    p = e.get_reg(UC_MIPS_REG_A2); calls.append((e.get_reg(UC_MIPS_REG_A1),) + struct.unpack("<II", e.read(p, 8))); return 0
HLE = {"queue_work_on": lambda e: (works.append(e.get_reg(UC_MIPS_REG_A2)), 1)[1], "isp_printf": lambda e: 0, "printk": lambda e: 0,
       "_rec_ioctl": rec, "_stub_zero": lambda e: 0}
e = KoEmu(a.oem, hle=HLE)
def patch(fn, stub):
    s = e._stub(stub); assert s < 0x70000000 + 0x1000, "stub page"
    e.write(e.addr(fn), struct.pack("<IIII", 0x3c190000 | (s >> 16), 0x37390000 | (s & 0xffff), 0x03200008, 0))
patch("ispcore_sensor_ops_ioctl", "_rec_ioctl"); patch("tisp_get_ae_tgain", "_stub_zero")
MAIN, SEC = e.addr("main_fs_work"), e.addr("sec_fs_work")

def rv(): return rnd.getrandbits(32) if rnd.random() < 0.15 else rnd.getrandbits(16)
def slots():
    b = bytearray(0x90)
    for k in range(18):
        struct.pack_into("<II", b, 8 * k, rnd.choice([0, 0, 1, 1, 1, 2, 0x80000000]), rv())
    return bytes(b)
def attr_blk(v):
    b = bytearray(rnd.getrandbits(8) for _ in range(0x130))
    struct.pack_into("<I", b, 0x124, rnd.choice([0, 1, 1, 2]))
    if rnd.random() < 0.3: struct.pack_into("<I", b, 0xb0, v)
    if rnd.random() < 0.3: struct.pack_into("<I", b, 0xa0, v)
    return bytes(b)

def oem_core(sl, at):
    e.heap_reset()
    core = e.alloc(0x400, bytes(0x300) + sl + bytes(0x70)); attr = e.alloc(0x130, at)
    e.write(core + 0x170, struct.pack("<I", attr)); e.write(core + 0x1d0, struct.pack("<I", attr))
    e.write(e.addr("g_ispcore"), struct.pack("<I", core))
    ops = e.alloc(0x20, bytes(0x20)); sd = e.alloc(0x200, bytes(0x200))
    e.write(sd + 0x10c, struct.pack("<I", core)); e.write(sd + 0xfc, struct.pack("<I", ops))
    e.write(e.addr("ispcore_sd"), struct.pack("<I", sd))
    return core, attr

def oem(case):
    k = case[0]; del calls[:]; del works[:]
    if k in "TGFD":
        _, ch, v, sl, at = case; core, attr = oem_core(sl, at)
        if k in "TG":
            ctx = e.alloc(0x20, bytes(0x20)); e.write(ctx + (0xc if k == "T" else 0), struct.pack("<I", ch))
            r = e.call("sensor_set_integration_time" if k == "T" else "sensor_set_analog_gain", v, ctx)
            return (r.stop, e.read(core + 0x300, 0x90), e.read(attr, 0x130))
        r = e.call("ispcore_irq_main_fs_work" if k == "F" else "ispcore_irq_main_fd_work")
        return (r.stop, [(c, val) for c, ch0, val in calls], [ch0 for c, ch0, val in calls], e.read(core + 0x300, 0x90))
    _, idx, pend, s0, s1, vb = case
    e.heap_reset(); REGS = 0x30400; regs = bytearray(REGS)
    struct.pack_into("<IIII", regs, (0, 0x10000, 0x20000)[idx] + 0x1e0, pend, 0, 0, 0)
    regs_a = e.alloc(REGS, regs); vic = bytearray(0x800); vic[0x2c0:0x2f0] = vb
    struct.pack_into("<II", vic, 0x24c, s0, s1); struct.pack_into("<I", vic, 0xe8, regs_a)
    vic_a = e.alloc(0x800, vic); dev = bytearray(0x300)
    struct.pack_into("<II", dev, 0x90, 0x11, 0x22); struct.pack_into("<I", dev, 0xe8, regs_a); struct.pack_into("<I", dev, 0x10c, vic_a)
    dev_a = e.alloc(0x300, dev)
    r = e.call("isp_vic_interrupt_service_routine", dev_a, (0x11, 0x22, 0x33)[idx])
    w = -1 if not works else (0 if works[0] == MAIN else 1 if works[0] == SEC else 99)
    if (pend & 0x10) and idx < 2 and not works: w = -2
    return (r.stop, w, e.read(vic_a + 0x2c0, 0x30))

cases = []
for i in range(a.n):
    k = a.kinds[i % len(a.kinds)]
    if k in "TG":
        v = rv(); cases.append((k, rnd.choice([0, 0, 1]), v, slots(), attr_blk(v)))
    elif k in "FD":
        cases.append((k, 0, 0, slots(), attr_blk(rv())))
    else:
        pend = rnd.getrandbits(9) if rnd.random() < 0.8 else rnd.choice([0, 1, 0x10, 0x11, 0x1f])
        cases.append(("V", rnd.choice([0, 0, 1, 2]), pend, rnd.choice([0, 0, 1, 2, 7]), rnd.choice([0, 1, 1, 3]), bytes(rnd.getrandbits(8) for _ in range(0x30))))
lines = []
for c in cases:
    if c[0] == "V": lines.append("V %d %x %x %x %s" % (c[1], c[2], c[3], c[4], c[5].hex()))
    else: lines.append("%s %d %x %s %s" % (c[0], c[1], c[2], c[3].hex(), c[4].hex()))
out = subprocess.run([a.harness], input="\n".join(lines) + "\n", capture_output=True, text=True).stdout.splitlines()
if len(out) != len(cases): print("harness gave %d lines for %d cases" % (len(out), len(cases))); sys.exit(1)
bad = 0; shown = 0; seen = {}
for c, l in zip(cases, out):
    o = oem(c); t = l.split(); k = c[0]
    if o[0] is not None: print("OEM run of %s stopped: %s" % (k, o[0])); sys.exit(1)
    if k in "TG": ok = bytes.fromhex(t[1]) == o[1] and bytes.fromhex(t[2]) == o[2]
    elif k in "FD":
        n = int(t[0]); ours = [tuple(int(x, 16) for x in p.split(":")) for p in t[1:1 + n]]
        ok = ours == o[1] and all(ch == 0 for ch in o[2]) and bytes.fromhex(t[1 + n]) == o[3]; seen[k] = seen.get(k, 0) + n
    else:
        ok = int(t[0]) == o[1] and bytes.fromhex(t[1]) == o[2]; seen["work%d" % o[1]] = seen.get("work%d" % o[1], 0) + 1
    if not ok:
        bad += 1
        if shown < 4: shown += 1; print("MISMATCH %s: ours %s / oem %s" % (k, l[:160], repr(o[1:])[:160]))
print("expo_fs oracle: %d cases (%s), %d mismatches, commands/works seen %s" % (len(cases), a.kinds, bad, seen))
sys.exit(1 if bad else 0)
