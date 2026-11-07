#!/usr/bin/env python3
"""diff_t30_core_slake.py VENDOR.ko OPEN.ko: ispcore_slake_module, vendor vs open, executed (#2 reload).

tx_isp_release (fd close) calls ispcore_slake_module (SDK-built ko 0xe688) for the ISP core; it stops
the core, puts the output channels back to state 1, notifies the device (dev+0x40d4, event 0x4000001),
calls every child's internal slake (vin -> releases the sensor i2c client) WITH THE CHILD in a0, and
disables the clocks.  Both modules run the same pseudo-random subdev/core/child layouts in Unicorn;
ispcore_core_ops_init, isp_printf, private_clk_disable, the device callback and the child slake are
hooked and logged with their arguments.  The ordered call logs, the return value and the core / channel /
child words must be identical.  Exit 1 on any difference.
"""
import sys, os, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2

CB = 0x71000000            # extra stub page: device callback, child slake

def make(path, log):
    def rec(name, ret=0):
        def h(e):
            log.append((name, e.get_reg(UC_MIPS_REG_A0), e.get_reg(UC_MIPS_REG_A1), e.get_reg(UC_MIPS_REG_A2)))
            return ret() if callable(ret) else ret
        return h
    st = {'child_ret': 0}
    hle = {"private_clk_disable": rec("clk_disable"), "isp_printf": rec("isp_printf"),
           "printk": lambda e: 0}
    e = KoEmu(path, hle=hle, max_insns=200000)
    for n in ("private_clk_disable", "isp_printf"):
        if n in e.symaddr: e.stubname[e.symaddr[n]] = n         # defined in the module: hook the entry
    e.hle["core_init"] = rec("core_ops_init")
    e.stubname[e.symaddr["ispcore_core_ops_init"]] = "core_init"
    e.uc.mem_map(CB, 0x1000)
    e.stubname[CB] = "dev_cb"; e.hle["dev_cb"] = rec("dev_cb")
    e.stubname[CB + 0x10] = "child_slake"; e.hle["child_slake"] = rec("child_slake", lambda: st['child_ret'])
    return e, st

def scenario(rnd, k):
    s = {}
    s['sd_kind'] = 'ok' if k > 3 else ['null', 'err', 'core_null', 'core_err'][k]
    s['state'] = rnd.choice([1, 2, 3, 4])
    s['nch'] = rnd.randrange(0, 4)
    s['nclk'] = rnd.randrange(0, 4)
    s['children'] = [rnd.choice(['none', 'none', 'err', 'ok', 'ok', 'noint', 'noslake']) for _ in range(16)]
    s['child_ret'] = rnd.choice([0, 0, -515, -5])
    return s

def run(e, st, log, s):
    e.heap_reset()
    sd = e.alloc(0x200, bytes(0x200)); core = e.alloc(0x200, bytes(0x200))
    dev = e.alloc(0x4100, bytes(0x4100)); chs = e.alloc(0xa0 * 4, b"\x07\x00\x00\x00" * (0x28 * 4))
    clks = e.alloc(16, struct.pack("<4I", 0xc1c10000, 0xc1c10010, 0xc1c10020, 0xc1c10030))
    e.write(sd + 0xd4, struct.pack("<I", {'core_null': 0, 'core_err': 0xfffffff2}.get(s['sd_kind'], core)))
    e.write(sd + 0xbc, struct.pack("<II", clks, s['nclk']))
    e.write(core + 0xe8, struct.pack("<I", s['state']))
    e.write(core + 0x154, struct.pack("<II", chs, s['nch']))
    e.write(core + 0x1a0, struct.pack("<I", dev)); e.write(dev + 0x40d4, struct.pack("<I", CB))
    kids = []
    for i, kind in enumerate(s['children']):
        if kind == 'none': v = 0
        elif kind == 'err': v = 0xfffffffe
        else:
            c = e.alloc(0x100, bytes(0x100)); ops = e.alloc(0x20, bytes(0x20)); it = e.alloc(8, bytes(8))
            e.write(c + 0x80, struct.pack("<I", 0x55aa55aa)); e.write(c + 8, struct.pack("<I", 0x1234 + i))
            e.write(c + 0xc4, struct.pack("<I", ops))
            if kind != 'noint': e.write(ops + 0x10, struct.pack("<I", it))
            if kind == 'ok': e.write(it + 4, struct.pack("<I", CB + 0x10))
            v = c; kids.append(c)
        e.write(core + 0x38 + 4 * i, struct.pack("<I", v))
    st['child_ret'] = s['child_ret']
    a0 = {'null': 0, 'err': 0xfffffff0}.get(s['sd_kind'], sd)
    del log[:]
    r = e.call("ispcore_slake_module", a0)
    if r.stop: return ("STOP", r.stop)
    norm = lambda v: ('sd' if v == sd else 'core' if v == core else 'dev' if v == dev else
                      'kid%d' % kids.index(v) if v in kids else v)
    nargs = {'core_ops_init': (0, 1), 'dev_cb': (0, 1, 2), 'child_slake': (0,), 'clk_disable': (0,),
             'isp_printf': (0, 2)}      # isp_printf a1 = format string (module address), a2 = child name
    calls = [(n,) + tuple(norm(a[i]) for i in nargs[n]) for n, *a in log]
    words = (e.read(core + 0xe8, 4), e.read(chs, 0xa0 * 4), b"".join(e.read(c + 0x80, 4) for c in kids))
    return (r.v0, calls, words)

def main():
    vk, ok = sys.argv[1], sys.argv[2]
    lv, lo = [], []
    ev, sv = make(vk, lv); eo, so = make(ok, lo)
    rnd = random.Random(2)
    bad = 0; n = 600
    for k in range(n):
        s = scenario(rnd, k)
        a = run(ev, sv, lv, s); b = run(eo, so, lo, s)
        if a != b:
            bad += 1
            if bad <= 3: print("DIFF case %d %s\n vendor %s\n open   %s" % (k, s, a[:2], b[:2]))
    print("ispcore_slake_module: %d cases, %d differ" % (n, bad))
    sys.exit(1 if bad else 0)

main()
