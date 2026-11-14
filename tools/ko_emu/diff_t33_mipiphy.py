#!/usr/bin/env python3
"""diff_t33_mipiphy.py OEM.ko OUR.o [--n 2000] [--seed 1]

T33 binary oracle, block "mipiphy" (openimp #99): the vendor's mipi_phy_stream_on.constprop.5 (with its
get_settle_time) against driver/t32/tx_isp_t33_mipi.h t33_mipi_phy_stream_on (tests/t33_binoracle/mipiphy.c) in
Unicorn, on generated CSI devices: mode 1 / 2 / other, sensor 0 / 1, states, bus types, CSI hosts, 0..5 lanes,
MIPI clocks over the settle table, the fast-boot flags.  Compared: return value, the register blocks of both CSI
hosts and of the D-PHY (random before the call), the 1 ms wait (msleep or mdelay, with host 0's 0x0c / 0x10 at
that moment: the reset pulse) and the messages (level; the settle time of the dual-mode ones).
Exit 0 = equal.  Nothing of the vendor module is stored here.
"""
import argparse, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu  # noqa: E402
from unicorn.mips_const import UC_MIPS_REG_A0 as A0  # noqa: E402

log = []
CLK = [0, 1, 79, 80, 109, 110, 149, 150, 199, 200, 299, 300, 400, 648, 999, 1000, 1500, 2199, 2200, 2399, 2400, 2500, 0xffffffff, 0x80000000]


def mk(path):
    def slp(name):
        def f(e):
            h0 = e.h0; log.append((name, e.get_reg(A0), e.read(h0 + 0xc, 8))); return 0
        return f

    def printf(e):
        lv = e.get_reg(A0); log.append(('printf', lv, e.get_reg(A0 + 2) if lv == 0 else 0)); return 0
    e = KoEmu(path, hle={'private_msleep': slp('msleep'), 'private_mdelay': slp('mdelay'), 'isp_printf': printf,
                         'printk': lambda e: 0}, max_insns=2_000_000)
    for n in ('private_msleep', 'private_mdelay', 'isp_printf'):      # functions of the vendor module: trap their entry
        if n in e.symaddr and n not in e.stubs:
            e.stubname[e.symaddr[n]] = n
    e._regions = lambda: []
    return e


def gen(r, i):
    c = dict(mode=r.choice((1, 1, 1, 2, 2, 0, 3)), idx=r.choice((0, 0, 1)), state=[r.choice((2, 3, 4, 4, 1, 0, 0xffffffff)) for _ in range(2)],
             sens=[(r.choice((1, 1, 1, 1, 2, 0, 3)), r.choice(CLK) if r.random() < .6 else r.randint(0, 2600), r.choice((0, 0, 1, 1, 2)),
                    r.choice((1, 2, 2, 4, 4, 3, 0, 5))) for _ in range(2)],
             fast=r.choice(((0, 0), (0, 0), (1, 0), (0, 1))), null=r.choice((0,) * 30 + (1, 2)),
             h0=r.randbytes(0x200), h1=r.randbytes(0x200), phy=r.randbytes(0x400), pad=r.randbytes(0x400))
    return c


def run(e, fn, c):
    e.heap_reset(); del log[:]
    h0 = e.alloc(0x200, c['h0']); h1 = e.alloc(0x200, c['h1']); phy = e.alloc(0x400, c['phy']); e.h0 = h0
    csi = bytearray(c['pad'])
    for i in range(2):
        d, clk, ix, ln = c['sens'][i]
        a = bytearray(c['pad'][0x100 * i:0x100 * i + 0x40]); a[0x14:0x18] = struct.pack('<I', d); a[0x1c:0x20] = struct.pack('<I', clk)
        a[0x28] = ix; a[0x29] = ln
        csi[0x150 + 0x68 * i:0x154 + 0x68 * i] = struct.pack('<I', e.alloc(0x40, a))
        csi[0x2bc + 4 * i:0x2c0 + 4 * i] = struct.pack('<I', c['state'][i])
    csi[0xf0:0xf8] = struct.pack('<II', h0, h1); csi[0x2dc:0x2e0] = struct.pack('<I', phy); csi[0x358:0x35c] = struct.pack('<I', c['mode'])
    ca = e.alloc(0x400, csi)
    sd = bytearray(c['pad'][:0x200]); sd[0x114:0x118] = struct.pack('<I', 0 if c['null'] == 2 else ca); sda = e.alloc(0x200, sd)
    at = e.alloc(16, struct.pack('<II', 1, c['idx']))
    e.write(e.addr('zeratul_open'), struct.pack('<I', c['fast'][0])); e.write(e.addr('atlas_open'), struct.pack('<I', c['fast'][1]))
    r = e.call(fn, 0 if c['null'] == 1 else sda, at)
    return (r.stop, r.v0, e.read(h0, 0x200), e.read(h1, 0x200), e.read(phy, 0x400), list(log))


WHAT = ('stop', 'return', 'CSI host 0 registers', 'CSI host 1 registers', 'D-PHY registers', 'wait / messages')


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('oem'); ap.add_argument('our'); ap.add_argument('--n', type=int, default=2000)
    ap.add_argument('--seed', type=int, default=1); a = ap.parse_args()
    try:
        oem = mk(a.oem); our = mk(a.our)
    except Exception as ex:  # noqa: BLE001
        print('diff_t33_mipiphy: setup error: %s' % ex); return 2
    r = random.Random(a.seed); bad = 0; st = {}
    for i in range(a.n):
        c = gen(r, i); ro = run(oem, 'mipi_phy_stream_on.constprop.5', c); ru = run(our, 'bo_mipi_phy_stream_on', c)
        k = 'ret %d%s' % (ro[1] - (1 << 32) if ro[1] >> 31 else ro[1], ' wrote' if (ro[2], ro[3], ro[4]) != (c['h0'], c['h1'], c['phy']) else '')
        st[k] = st.get(k, 0) + 1
        if ro != ru or ro[0]:
            bad += 1
            if bad <= 5:
                j = next((j for j in range(len(ro)) if ro[j] != ru[j]), 0)
                d = ''
                if j in (2, 3, 4):
                    d = ' at ' + ','.join('%#x' % x for x in range(0, len(ro[j]), 4) if ro[j][x:x + 4] != ru[j][x:x + 4])[:80]
                print('MISMATCH case %d (mode %d idx %d state %s sens %s): %s%s: vendor %s ours %s' % (
                    i, c['mode'], c['idx'], c['state'], c['sens'], WHAT[j], d, str(ro[j])[:120] if j in (0, 1, 5) else '', str(ru[j])[:120] if j in (0, 1, 5) else ''))
    print('mipi_phy_stream_on: %d cases (%s)' % (a.n, ', '.join('%s %d' % kv for kv in sorted(st.items()))))
    print('diff_t33_mipiphy: %d cases, %d mismatches' % (a.n, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
