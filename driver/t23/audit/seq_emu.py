#!/usr/bin/env python3
"""MMIO order of one frame-channel start: stock tisp_channel_start() vs ours.

usage: seq_emu.py <stock tx-isp-t23.ko> <built tx-isp-t23.ko> [ch [live]]

live: channel 0 is already enabled (msca[0].en = 1, 0xd040 = 1), i.e. the
second channel start on a running output.

Both modules get the same channel configuration in msca[ch] (56-byte
tisp channel attr: en, crop, scaler in/out, stride) and run
tisp_channel_start(ch).  Prints every system_reg_write / system_reg_read in
order (R = read, W = write).  The scaler LUT block (0xd2xx..0xd8xx curve
writes) is summarised.
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from memu import Mod, CPU

STOCK, OURS = sys.argv[1:3]
CH = int(sys.argv[3]) if len(sys.argv) > 3 else 0
LIVE = len(sys.argv) > 4 and sys.argv[4] == 'live'
W, H = 1920, 1080
OW, OH = (1920, 1080) if CH == 0 else (640, 360)


def setup(path):
    m = Mod(path)
    log = []
    c = CPU(m)

    def wr(c_, R):
        log.append(('W', R[4], R[5])); c_.regs_hw[R[4]] = R[5]; return 0

    def rd(c_, R):
        v = c_.regs_hw.get(R[4], 0); log.append(('R', R[4], v)); return v
    c.hook('system_reg_write', wr)
    c.hook('system_reg_read', rd)
    for n in ('vmalloc', 'private_vmalloc', '__kmalloc', 'kmalloc'):
        if n in m.stubs:
            c.intercept[m.stubs[n]] = 'private_kmalloc'
    # tisp_par_info: sensor size
    if 'tisp_par_info' in m.byname:
        c.w32(m.addr('tisp_par_info'), W); c.w32(m.addr('tisp_par_info') + 4, H)
    if 'msca' in m.byname:
        msca = m.addr('msca')
        hp = m.addr('mscaHardPar')
        for i in range(3):          # tisp_msca_init: mscaHardPar[i] = &msca[i]
            c.w32(hp + 4 * i, msca + 56 * i)
    else:                           # ours: locals stripped, learn msca from the call
        got = []
        saved = dict(c.intercept)
        c.hook('tisp_msca_chx_cfg_load', lambda c_, R: got.append(R[6]) or 0)
        c.call('tisp_channel_start', (CH,))
        c.intercept = saved; del c.hooks['tisp_msca_chx_cfg_load']
        msca = got[0] - CH * 56
        log.clear()
    cfg = msca + CH * 56
    if LIVE and CH:                 # start channel 0 first, same instance
        c0 = msca
        for off, v in ((0x10, 0), (0x14, 0), (0x18, W), (0x1c, H),
                       (0x20, W), (0x24, H), (0x28, W), (0x2c, H),
                       (0x30, W), (0x34, H)):
            c.w32(c0 + off, v)
        c.w8(c0, 1)
        c.call('tisp_channel_start', (0,))
        log.clear()
    c.w8(cfg, 1)
    for off, v in ((0x10, 0), (0x14, 0), (0x18, W), (0x1c, H),
                   (0x20, OW), (0x24, OH), (0x28, OW), (0x2c, OH),
                   (0x30, OW), (0x34, OH)):
        c.w32(cfg + off, v)
    return m, c, log


def show(name, log):
    print('== %s tisp_channel_start(%d)%s: %d accesses' % (name, CH, ' (ch0 live)' if LIVE else '', len(log)))
    lut = 0
    for k, a, v in log:
        if 0xd200 <= a < 0xd000 + 0x100 * 0 + 0x1000 and (a & 0xff) >= 0x00 and False:
            pass
        if a & 0xf000 == 0xd000 and 0x200 <= (a & 0xfff) < 0x1000 and (a & 0xff) not in (0x30, 0x38, 0x3c, 0x40, 0x44, 0x48, 0x50, 0x58, 0x60, 0x64, 0x68):
            lut += 1
            continue
        if lut:
            print('   ... %d channel coefficient/curve writes' % lut); lut = 0
        print('   %s 0x%04x %s 0x%08x' % (k, a, '=' if k == 'W' else '->', v))
    if lut:
        print('   ... %d channel coefficient/curve writes' % lut)


for name, path in (('stock', STOCK), ('ours', OURS)):
    m, c, log = setup(path)
    try:
        c.call('tisp_channel_start', (CH,))
    except Exception as e:
        print('   %s: emulation stopped: %s' % (name, e))
    show(name, log)
