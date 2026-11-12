#!/usr/bin/env python3
"""diff_t30_child_slake.py VENDOR.ko OPEN.ko: vin activate/slake and csi slake, vendor vs open, executed (#2 reload).

ispcore_slake_module (fd close) calls the child slakes; the register lift of tx_isp_vin_slake_subdev and
tx_isp_csi_slake_subdev kept byte offsets on `uintptr_t *` registers (mutex taken 4x too far: the QEMU T30 close
hung in mutex_lock), and tx_isp_vin_activate_subdev stepped the open count by 4.  Both modules run the same
pseudo-random vin / csi device words in Unicorn; every callee (vin_s_stream, tx_isp_vin_init,
subdev_sensor_ops_set_input with its index, subdev_sensor_ops_release_all_sensor, csi_video_s_stream,
csi_core_ops_init, mutex lock/unlock with the mutex offset, clk_disable) is hooked at its entry and logged with
normalised arguments; return value and the state / count words must match.  Exit 1 on any difference.
"""
import sys, os, random, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2

HOOK = ("vin_s_stream", "tx_isp_vin_init", "subdev_sensor_ops_set_input", "subdev_sensor_ops_release_all_sensor",
        "csi_video_s_stream", "csi_core_ops_init", "private_mutex_lock", "private_mutex_unlock", "private_clk_disable",
        "isp_printf", "printk")


def make(path, log):
    def rec(name):
        def h(e):
            a = [e.get_reg(r) for r in (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2)]
            if name == "subdev_sensor_ops_set_input":
                a[2] = struct.unpack("<i", e.read(a[1], 4))[0]          # the index the callee gets
            log.append((name, a))
            return 0
        return h
    e = KoEmu(path, hle={n: rec(n) for n in HOOK}, max_insns=100000)
    for n in HOOK:
        if n in e.symaddr: e.stubname[e.symaddr[n]] = n
    return e


def run(e, log, fn, words, base_kind):
    e.heap_reset()
    dev = e.alloc(0x400, bytes(0x400))
    for off, v in words.items():
        if isinstance(off, int): e.write(dev + off, struct.pack("<I", v & 0xffffffff))
    if base_kind == 'csi':
        sd = e.alloc(0x200, bytes(0x200)); clks = e.alloc(16, struct.pack("<4I", 0xc1c10000, 0xc1c10010, 0xc1c10020, 0xc1c10030))
        e.write(sd + 0xd4, struct.pack("<I", words.get('csd', dev))); e.write(sd + 0xbc, struct.pack("<II", words.get('clks', clks), words.get('nclk', 2)))
        a0 = words.get('a0', sd)
    else:
        sd = dev; a0 = dev
        if words.get('list_empty'): e.write(dev + 0xdc, struct.pack("<I", dev + 0xdc))
    del log[:]
    r = e.call(fn, a0)
    if r.stop: return ("STOP", r.stop)
    def norm(v):
        if dev <= v < dev + 0x400: return 'dev+%#x' % (v - dev)
        if sd <= v < sd + 0x200: return 'sd+%#x' % (v - sd)
        return v
    calls = []
    for n, a in log:
        if n in ("isp_printf", "printk"): calls.append((n,)); continue
        if n == "subdev_sensor_ops_set_input": calls.append((n, norm(a[0]), a[2])); continue
        if n == "private_clk_disable": calls.append((n, a[0])); continue
        k = 2 if n in ("vin_s_stream", "tx_isp_vin_init", "csi_core_ops_init", "csi_video_s_stream") else 1
        calls.append((n,) + tuple(norm(x) for x in a[:k]))
    w = e.read(dev + 0xf4, 8) if base_kind != 'csi' else e.read(dev + 0x124, 4)
    return (r.v0 & 0xffffffff, calls, w)


def main():
    lv, lo = [], []
    ev, eo = make(sys.argv[1], lv), make(sys.argv[2], lo)
    rnd = random.Random(7); bad = 0; n = 0
    for k in range(400):
        st = rnd.choice([0, 1, 2, 3, 4, 5]); cnt = rnd.choice([0, 1, 2, 5])
        vw = {0xf4: st, 0xf8: cnt, 0xe4: rnd.choice([0, 0x81230000]), 0xe8: 1, 'list_empty': rnd.random() < 0.5,
              0xdc: 0x81240000}
        cw = {0x124: st, 0x128: 1}
        kind = rnd.choice(['ok', 'ok', 'ok', 'sd_null', 'csd_null', 'csd_err', 'clk_null'])
        if kind == 'sd_null': cw['a0'] = 0
        if kind == 'csd_null': cw['csd'] = 0
        if kind == 'csd_err': cw['csd'] = 0xfffffff4
        if kind == 'clk_null': cw['clks'] = 0
        cw['nclk'] = rnd.randrange(0, 4)
        for fn, w, b in (("tx_isp_vin_slake_subdev", vw, 'vin'), ("tx_isp_vin_activate_subdev", vw, 'vin'),
                         ("tx_isp_csi_slake_subdev", cw, 'csi')):
            a = run(ev, lv, fn, w, b); o = run(eo, lo, fn, w, b); n += 1
            if a != o:
                bad += 1
                if bad <= 4: print("DIFF %s case %d %s\n vendor %s\n open   %s" % (fn, k, w, a, o))
    print("vin activate/slake + csi slake: %d runs, %d differ" % (n, bad))
    sys.exit(1 if bad else 0)

main()
