#!/usr/bin/env python3
"""Emulator audit of SetOSDAttr / SetOSDBlock / SetDrawBlock (0x08000181 /
182 / 180) and their getters against the stock tx-isp-t23.ko (md5 8237acb1).

usage: osd_draw_emu.py <stock tx-isp-t23.ko>

Runs the stock apical_isp_core_ops_s_ctrl / g_ctrl with random mscaler and
msca objects and random user blocks and compares, with the C helpers of
driver/t23/tx_isp_t23_tuning_ext.h (built here with gcc):
  - the whole mscaler object (2316 bytes) after every set (state, dirty
    words), the user copy sizes (12 / 20 / 32 bytes), the return value
    (stock: always 0 after the copy; a block beyond the limits is logged and ignored) and the stock log for the stale-size quirk
  - the user block of the get (all bytes: the bytes the stock code leaves
    are the uninitialised stack local, which the audit pokes with the user
    block, as the driver does)
Stock Get of the OSD block clears its local first, so it always reports
block 0; the audit therefore asks block 0 (the driver honours the index).
Blocks above the stock-unchecked range (OSD 8.., draw 6..) are not run on
the stock code, the helpers must refuse them.  Prints "bad 0" per check
when identical.
"""
import os, sys, subprocess, tempfile, random
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from memu import Mod, CPU
STOCK = sys.argv[1]
INC = os.environ.get('INC') or os.path.join(HERE, '..', 'tx_isp_t23_tuning_ext.h')
TMP = tempfile.mkdtemp()
os.chdir(TMP)
SRC = r'''
#include <stdio.h>
#include "%s"
int main(int argc, char **argv)
{
	uint8_t m[T23X_MSCA_BYTES], msca[168], blk[32], out[32];
	int ret = 0, quirk = 0;
	FILE *f = fopen(argv[3], "rb");
	if (fread(m, 1, sizeof(m), f) != sizeof(m) || fread(msca, 1, sizeof(msca), f) != sizeof(msca) ||
	    fread(blk, 1, sizeof(blk), f) != sizeof(blk)) return 99;
	fclose(f);
	memset(out, 0, sizeof(out));
	switch (argv[1][0]) {
	case 'a': if (argv[2][0] == 's') t23x_osd_attr_set(m, blk); else t23x_osd_attr_get(m, out); break;
	case 'b': if (argv[2][0] == 's') ret = t23x_osd_block_set(m, msca, blk, &quirk);
		  else ret = t23x_osd_block_get(m, blk, out); break;
	case 'd': if (argv[2][0] == 's') ret = t23x_draw_block_set(m, blk);
		  else ret = t23x_draw_block_get(m, blk, out); break;
	}
	f = fopen(argv[4], "wb");
	fwrite(m, 1, sizeof(m), f); fwrite(out, 1, sizeof(out), f);
	fputc(quirk, f);
	fclose(f);
	return ret ? (ret == -ERANGE ? 2 : 1) : 0;
}
''' % INC
open('ref.c', 'w').write(SRC)
subprocess.check_call(['gcc', '-O1', '-Wall', '-o', 'ref', 'ref.c'])

random.seed(23)
STACK_LOCAL = 0x7f200000 - 184 + 16
CIDS = {'a': 0x8000181, 'b': 0x8000182, 'd': 0x8000180}
SIZES = {'a': 12, 'b': 20, 'd': 32}
bad = {}
N = int(os.environ.get('N', '400'))
stats = {}

def rnd_block(kind, lim):
    if kind == 'a':
        b = bytearray(random.getrandbits(8) for _ in range(12))
        # keep the first word small so that equal / different both occur
        b[1:4] = b'\0\0\0'
        b[0] = random.choice([0, 1, 2, 255, random.getrandbits(8)])
        if random.random() < 0.15: b[1] = 1          # word != byte
        return bytes(b)
    if kind == 'b':
        b = bytearray(random.getrandbits(8) for _ in range(20))
        b[0] = random.randint(0, 7)
        b[1] = random.choice([0, 1, 1, 1, 1, 2, 255])
        W, H = lim
        def u16(o, v): b[o] = v & 255; b[o + 1] = (v >> 8) & 255
        mx = max(W, 1)
        u16(2, random.randint(0, mx)); u16(4, random.randint(0, H))
        slack = random.choice([0, 0, 0, 3, mx])      # sometimes beyond the limits
        u16(6, random.randint(0, max(1, mx - (b[2] | b[3] << 8)) + slack))
        slack = random.choice([0, 0, 0, 3, H])
        u16(8, random.randint(0, max(1, H - (b[4] | b[5] << 8)) + slack))
        return bytes(b)
    b = bytearray(random.getrandbits(8) for _ in range(32))
    b[0] = random.randint(0, 5)
    b[4:8] = (random.choice([0, 1, 2, 3, 7, 255])).to_bytes(4, 'little')
    return bytes(b)

def one(kind, op, it):
    m = Mod(STOCK); c = CPU(m)
    log = []
    def mk(name):
        def f(cc, R):
            log.append((name, R[6] if name != 'isp_printf' else R[4]))
            if name != 'isp_printf':
                cc.wrbytes(R[4], cc.rdbytes(R[5], R[6]))
            return 0
        return f
    for nm in ('private_copy_from_user', 'private_copy_to_user', 'isp_printf'):
        c.hook(nm, mk(nm))
    table = bytearray(random.getrandbits(8) for _ in range(2316))
    if random.random() < 0.5:           # sane old sizes so the quirk is not always hit
        for n in range(8):
            for o in (4, 6):
                v = random.randint(0, 600); base = 1836 + 16 * (21 + n) + o
                table[base] = v & 255; table[base + 1] = v >> 8
    msca = bytearray(random.getrandbits(8) for _ in range(168))
    W = random.choice([640, 1280, 1920, 2304, random.randint(1, 4000)])
    H = random.choice([360, 720, 1080, 1296, random.randint(1, 4000)])
    msca[48:52] = W.to_bytes(4, 'little'); msca[52:56] = H.to_bytes(4, 'little')
    c.wrbytes(m.addr('mscaler'), bytes(table))
    c.wrbytes(m.addr('msca'), bytes(msca))
    blk = rnd_block(kind, (W, H))
    if op == 'g':
        # stock get: the local is poked with the user block
        gb = bytearray(blk)
        if kind == 'b': gb[0] = 0
        blk = bytes(gb)
    dev = c.heapp; c.heapp += 0x20000
    ctrl = c.heapp; c.heapp += 64
    usr = c.heapp; c.heapp += 0x1000
    c.w32(ctrl, CIDS[kind]); c.w32(ctrl + 4, usr)
    sz = SIZES[kind]
    if op == 's':
        c.wrbytes(usr, blk)
        rv = c.call('apical_isp_core_ops_s_ctrl', (dev, ctrl))
        got = c.rdbytes(m.addr('mscaler'), 2316)
        gout = b''
    else:
        c.wrbytes(STACK_LOCAL, blk)
        c.wrbytes(usr, bytes([0xA5]) * 0x100)
        rv = c.call('apical_isp_core_ops_g_ctrl', (dev, ctrl))
        got = c.rdbytes(m.addr('mscaler'), 2316)
        gout = c.rdbytes(usr, 0x100)
    open('in.bin', 'wb').write(bytes(table) + bytes(msca) + blk.ljust(32, b'\0'))
    rc = subprocess.call(['./ref', kind, op, 'in.bin', 'out.bin'])
    ref = open('out.bin', 'rb').read()
    rm, rout, rq = ref[:2316], ref[2316:2348], ref[2348]
    rv32 = rv & 0xffffffff
    ok = (log[:1] == [('private_copy_from_user' if op == 's' else 'private_copy_to_user', sz)] or
          (op == 's' and not log))
    if op == 's':
        k = (kind, 'ignored' if len(log) > 2 else ('quirk' if len(log) > 1 else 'ok'))
        stats[k] = stats.get(k, 0) + 1
        ok = log and log[0] == ('private_copy_from_user', sz)
        stock_ret = -1 if rv32 == 0xffffffff else rv32
        quirk_log = len(log) == 2 and log[1][0] == 'isp_printf' and stock_ret == 0
        # the stock s_ctrl discards the helper result: always 0 after the copy;
        # the helper's -ERANGE (exit 2) is that ignored geometry error
        err_log = sum(1 for n, a in log[1:] if n == 'isp_printf') == 2
        ok = ok and stock_ret == 0 and (rc == 0 or (rc == 2 and err_log and got == rm))
        ok = ok and got == rm and quirk_log == bool(rq)
    else:
        ok = ok and rv32 == 0 and rc == 0 and log == [('private_copy_to_user', sz)]
        ok = ok and gout[:sz] == rout[:sz] and gout[sz:] == bytes([0xA5]) * (0x100 - sz)
        ok = ok and got == bytes(table)
    if not ok:
        bad[(kind, op)] = bad.get((kind, op), 0) + 1
        if bad[(kind, op)] < 3:
            print('mismatch', kind, op, it, hex(rv32), rc, log[:3], blk.hex())
            if op == 's':
                d = [i for i in range(2316) if got[i] != rm[i]]
                print(' diff at', d[:12], [(got[i], rm[i]) for i in d[:6]])
            else:
                print(' stock', gout[:sz].hex()); print(' ref  ', rout[:sz].hex())

for kind in 'abd':
    for op in 'sg':
        bad.setdefault((kind, op), 0)
        for it in range(N):
            one(kind, op, it)
        print('%s %s: bad %d / %d' % ({'a': 'osd attr', 'b': 'osd block', 'd': 'draw block'}[kind], op, bad[(kind, op)], N))

print('set outcomes (stock):', dict(sorted(stats.items())))
# refused ranges: nothing written by the helpers
for kind, idx in (('b', 8), ('b', 255), ('d', 6), ('d', 255)):
    blk = bytearray(32); blk[0] = idx; blk[1] = 1
    open('in.bin', 'wb').write(bytes(2316) + bytes(168) + bytes(blk))
    for op in 'sg':
        r = subprocess.call(['./ref', kind, op, 'in.bin', 'out.bin'])
        out = open('out.bin', 'rb').read()
        if r != 1 or out[:2316] != bytes(2316):
            bad[('range', kind)] = bad.get(('range', kind), 0) + 1
print('range check done (bad %d)' % sum(v for k, v in bad.items() if k[0] == 'range'))
sys.exit(1 if any(bad.values()) else 0)
