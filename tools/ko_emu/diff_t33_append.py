#!/usr/bin/env python3
"""diff_t33_append.py OEM.ko OUR.o [--n 2000] [--seed 1]

T33 binary oracle, block "append" (openimp #99): the vendor's tisp_get_module_enum and tisp_bin_append_data_parse
against driver/t32/tx_isp_t33_append.h (tests/t33_binoracle/append.c) in Unicorn.  Generated append data: chains of
records (APPEND_LDC with good / bad flag and size, APPEND_LSC with 0..3 tables, other and unknown names), next
offsets in order, 0, past the end or back to the first record (the 32 record limit), on random LSC states
(section 7 count, stLutInfo size / table buffer, lutMaxSize).  Compared: return value, the parameter block
(section 7), stLutInfo, the table buffer, lutMaxSize, ldcParExist, stLdcPar, the messages and the vmalloc sizes.
Cases the port refuses on purpose are not generated (more than 8 LSC tables, records outside the data).
Exit 0 = equal.  Nothing of the vendor module is stored here.
"""
import argparse, os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu  # noqa: E402
from unicorn.mips_const import UC_MIPS_REG_A0 as A0, UC_MIPS_REG_SP as SP  # noqa: E402

NAMES = ['TOP', 'AE', 'AF', 'AWB', 'BCSH', 'CCM', 'CDNS', 'CLM', 'CSC', 'CSCCR', 'DEFOG', 'DMSC', 'DPC', 'GAMMA', 'LCE',
         'LDC', 'LSC', 'MDNS', 'MSCA', 'RDNS', 'SDNS', 'TMO', 'YDNS', 'YSP']
TP = 0x4450 + 0x98 + 0x40
log = []


def mk(path):
    def vmalloc(e):
        n = e.get_reg(A0); a = e.alloc(n + 64, bytes(n + 64)); log.append(('vmalloc', n)); e.last = (a, n); return a

    def printf(e):
        ln = e.get_reg(A0 + 3); v = struct.unpack('<I', e.read(e.get_reg(SP) + 16, 4))[0]
        log.append(('printf', e.get_reg(A0), ln, v if ln in (946, 391) else 0)); return 0
    e = KoEmu(path, hle={'private_vmalloc': vmalloc, 'private_vfree': lambda e: (log.append(('vfree',)), 0)[1],
                         'isp_printf': printf, 'printk': lambda e: 0}, max_insns=20_000_000)
    for n in ('private_vmalloc', 'private_vfree', 'isp_printf'):      # functions of the vendor module: trap their entry
        if n in e.symaddr and n not in e.stubs:
            e.stubname[e.symaddr[n]] = n
    e._regions = lambda: []      # no write log needed (and the table buffers are large)
    return e


def rec(r, name, flag, nxt, data, size=None):
    n = name.encode() + b'\0'
    n += r.randbytes(16 - len(n))
    return n[:16] + struct.pack('<III', flag, nxt, len(data) if size is None else size) + data


def gen(r):
    """-> (ch, app bytes, size arg or None for a NULL pointer, n0, state...)"""
    ch = r.randint(0, 3); n0 = r.choice((0, 1, 2, 3, 3, 3, 4, 5)); left = 8 - n0
    recs = []; k = r.choice((0, 1, 1, 2, 2, 3, 4, 6))
    for i in range(k):
        t = r.choice(('ldc', 'ldc', 'lsc', 'lsc', 'lsc', 'other', 'unk'))
        if t == 'ldc':
            sz = r.choice((0x50, 0x50, 0x50, 0x4f, 0x54, 0))      # records stay word aligned (the vendor reads words)
            recs.append(('APPEND_LDC', r.choice((1, 1, 1, 0, 2)), r.randbytes(max(sz, 0x50)), sz))
        elif t == 'lsc':
            n = min(r.choice((0, 1, 1, 2, 3)), left); fl = r.choice((1, 1, 1, 1, 0, 2))
            if fl == 1:
                left -= n
            ex = r.choice((0, 0, 4, 0x100, 0x3600)) if n or r.random() < .5 else 0
            ex += -(n * 0x3606 + ex) & 3            # records stay word aligned (the vendor reads words)
            d = r.randbytes(n * 0x3606 + ex)
            recs.append(('APPEND_LSC', fl, d, None))
        elif t == 'other':
            recs.append(('APPEND_' + r.choice(NAMES[:15] + NAMES[17:]), r.choice((0, 1)), bytes(r.getrandbits(8) for _ in range(r.choice((0, 24, 128)))), None))
        else:
            recs.append((r.choice(('', 'APPEND', 'APPEND_LS', 'APPEND_LSCX', 'append_ldc', 'APPEND_LDD', 'XPPEND_LSC')), 1, bytes(4 * r.randint(0, 10)), None))
    w4 = r.choice((8, 8, 8, 8, 1, 0x12345678, 0))
    app = bytearray(struct.pack('<II', r.getrandbits(32), w4)); loop = r.random() < .08 and all(x[0] != 'APPEND_LSC' for x in recs)
    for i, (nm, fl, d, sz) in enumerate(recs):
        end = len(app) + 0x1c + len(d); last = i == len(recs) - 1
        nxt = end if not last else (8 if loop else r.choice((0, 0, end, end + 100, 0xffffffff)))
        if not last and r.random() < .04:
            nxt = r.choice((0, 0x7fffffff))          # early end of the chain
        app += rec(r, nm, fl, nxt, d, sz)
    size = len(app)
    if not recs:
        w = r.choice((0, 1)); size = r.choice((0, 4, 7, 0x24, 0x40)) if w else 0x40
        app += bytes(0x40)
    null = r.random() < .02
    lutmax = [r.choice((0, 0x3602, 0xa206, 0x1b010)) for _ in range(4)]
    s16 = (n0 * 0x3602) & 0xffff
    mode = r.choice((0, 0, 0, 1, 2))
    if mode == 1:
        lutmax[ch] = 8 * 0x3602
    elif mode == 2 and n0 < 5:
        lutmax[ch] = s16 + 0x3602
    else:
        lutmax[ch] = s16
    return dict(ch=ch, app=bytes(app), size=size, null=null, n0=n0, s16=s16, lutmax=lutmax,
                tp=r.randbytes(TP), info=r.randbytes(0x2c),
                lut=r.randbytes(max(lutmax[ch], s16)), nolut=(n0 == 0 and r.random() < .5),
                gl=r.randbytes(0x70 + 8 + 640))


def run(e, fn, c):
    e.heap_reset(); del log[:]
    ch = c['ch']; tp = bytearray(c['tp']); tp[0x4450 + 0x94] = c['n0']
    info = bytearray(c['info']); info[2:4] = struct.pack('<H', c['s16'])
    lut = 0 if c['nolut'] else e.alloc(len(c['lut']) + 64, c['lut'] + bytes(64))
    info[0x14:0x18] = struct.pack('<I', lut)
    tpa = e.alloc(TP, tp); ia = e.alloc(0x2c, info)
    for i in range(4):
        e.write(e.addr('tparamsP') + 4 * i, struct.pack('<I', tpa if i == ch else 0))
        e.write(e.addr('stLutInfo') + 4 * i, struct.pack('<I', ia if i == ch else 0))
    lm = bytearray(c['gl'][:0x70])
    for i in range(4):
        lm[i * 28 + 4:i * 28 + 8] = struct.pack('<I', 0 if c['nolut'] and i == ch else c['lutmax'][i])
    e.write(e.addr('lutMaxSize'), lm); e.write(e.addr('ldcParExist'), c['gl'][0x70:0x74]); e.write(e.addr('stLdcPar'), c['gl'][0x78:0x78 + 320])
    aa = e.alloc(len(c['app']) + 0x80, c['app'] + bytes(0x80))
    r = e.call(fn, ch, 0 if c['null'] else aa, c['size'])
    info2 = bytearray(e.read(ia, 0x2c)); lp = struct.unpack('<I', info2[0x14:0x18])[0]; info2[0x14:0x18] = bytes(4)
    cap = struct.unpack('<I', e.read(e.addr('lutMaxSize') + ch * 28 + 4, 4))[0]
    return (r.stop, r.v0, e.read(tpa, TP), bytes(info2), lp != 0, lp == lut, e.read(lp, cap) if lp else b'', e.read(e.addr('lutMaxSize'), 0x70),
            e.read(e.addr('ldcParExist'), 4), e.read(e.addr('stLdcPar'), 320), list(log))


WHAT = ('stop', 'return', 'parameter block / section 7', 'stLutInfo', 'table pointer set', 'table buffer kept', 'tables', 'lutMaxSize',
        'ldcParExist', 'stLdcPar', 'messages / vmalloc')


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('oem'); ap.add_argument('our'); ap.add_argument('--n', type=int, default=2000)
    ap.add_argument('--seed', type=int, default=1); a = ap.parse_args()
    try:
        oem = mk(a.oem); our = mk(a.our)
    except Exception as ex:  # noqa: BLE001
        print('diff_t33_append: setup error: %s' % ex); return 2
    r = random.Random(a.seed); bad = 0; total = 0
    names = ['APPEND_' + x for x in NAMES]
    cases = names + [x[:-1] for x in names] + [x + 'X' for x in names] + [x.lower() for x in names] + ['', 'A', 'APPEND_', 'APPEND']
    for i in range(a.n // 4):
        s = r.choice(names); k = r.randrange(len(s)); cases.append(s[:k] + chr(r.randint(1, 255)) + s[k + 1:])
    for s in cases:
        b = s.encode('latin1') + b'\0' + r.randbytes(20); v = []
        for e, fn in ((oem, 'tisp_get_module_enum'), (our, 'bo_tisp_get_module_enum')):
            e.heap_reset(); p = e.alloc(64, b + bytes(32)); x = e.call(fn, p); v.append((x.stop, x.v0))
        total += 1
        if v[0] != v[1] or v[0][0]:
            bad += 1; print('MISMATCH tisp_get_module_enum(%r): vendor %s ours %s' % (s, v[0], v[1]))
    print('tisp_get_module_enum: %d cases' % len(cases))
    stat = {}
    for i in range(a.n):
        c = gen(r); ro = run(oem, 'tisp_bin_append_data_parse', c); ru = run(our, 'bo_tisp_bin_append_data_parse', c); total += 1
        for x in ro[10]:
            k = x[0] if x[0] != 'printf' else 'printf %d' % x[2]; stat[k] = stat.get(k, 0) + 1
        stat['ldc set'] = stat.get('ldc set', 0) + (ro[8] != c['gl'][0x70:0x74] or ro[9] != c['gl'][0x78:0x78 + 320])
        stat['lsc added'] = stat.get('lsc added', 0) + (ro[2][0x4450 + 0x94] != c['n0'])
        if ro != ru:
            bad += 1
            if bad <= 5:
                k = next(j for j in range(len(ro)) if ro[j] != ru[j])
                print('MISMATCH case %d: %s: vendor %s ours %s' % (i, WHAT[k], str(ro[k])[:200], str(ru[k])[:200]))
    print('tisp_bin_append_data_parse: %d cases (%s)' % (a.n, ', '.join('%s %d' % kv for kv in sorted(stat.items()))))
    print('diff_t33_append: %d cases, %d mismatches' % (total, bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
