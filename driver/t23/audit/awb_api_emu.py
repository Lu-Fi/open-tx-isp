#!/usr/bin/env python3
"""Emulator check: T23 AwbClust (0x0800000e), AwbCtTrend (0x0800000f) and
the unhandled SetMask/GetMask (0x080000e5) against the stock tx-isp-t23.ko.

usage: awb_api_emu.py <stock tx-isp-t23.ko>

Runs the stock apical_isp_core_ops_s_ctrl / g_ctrl with random user blocks
and random previous state (objects, api_para, api_status) and compares the
stock objects after the set, the user copy sizes, and the get block with the
C helpers of driver/t23/tx_isp_t23_tuning_ext.h (built here with gcc).
The mask ids must return -1 without any user copy (not handled by stock).
Prints "bad 0" per check when identical.
"""
import os, sys, struct, subprocess, tempfile, random
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from memu import Mod, CPU
STOCK = sys.argv[1]
INC = os.path.join(HERE, '..', 'tx_isp_t23_tuning_ext.h')
TMP = tempfile.mkdtemp()
os.chdir(TMP)
src = r'''
#include <stdio.h>
#include <stdlib.h>
#include "%s"
/* argv: kind(c|t) file with state: obj api status in ; prints obj api status out */
int main(int argc, char **argv)
{
	uint32_t obj[10], api[10], st[2], in[10], out[10];
	int c = argv[1][0] == 'c';
	unsigned n = c ? 10 : 7, u = c ? 10 : 6, i;
	FILE *f = fopen(argv[2], "rb");
	if (fread(obj, 4, n, f) != n || fread(api, 4, n, f) != n || fread(st, 4, 2, f) != 2 ||
	    fread(in, 4, u, f) != u) return 1;
	fclose(f);
	if (c) { t23x_awb_cluster_set(obj, st, api, in); t23x_awb_cluster_get(obj, out); }
	else   { t23x_awb_trend_set(obj, st, api, in);   t23x_awb_trend_get(obj, out); }
	for (i = 0; i < n; i++) { printf("%%u ", obj[i]); } printf("\n");
	for (i = 0; i < n; i++) { printf("%%u ", api[i]); } printf("\n");
	printf("%%u %%u\n", st[0], st[1]);
	for (i = 0; i < u; i++) { printf("%%u ", out[i]); } printf("\n");
	return 0;
}
''' % INC
open('ref.c', 'w').write(src)
subprocess.check_call(['gcc', '-O1', '-Wall', '-o', 'ref', 'ref.c'])

tot = 0
for kind, cid, objn, api, stn, ubytes, uw in (
        ('c', 0x800000e, '_awb_cluster', 'awb_cluster_api_para', 'awb_cluster_api_status', 40, 10),
        ('t', 0x800000f, '_awb_trend', 'awb_trend_api_para', 'awb_trend_api_status', 24, 6)):
    n = 10 if kind == 'c' else 7
    bad = 0
    for seed in range(200):
        random.seed(seed * 7 + ord(kind))
        m = Mod(STOCK); c = CPU(m)
        log = []
        def mk(name):
            def f(cc, R):
                log.append((name, R[6]))
                cc.wrbytes(R[4], cc.rdbytes(R[5], R[6]))
                return 0 if name != 'memcpy' else R[4]
            return f
        for nm in ('private_copy_from_user', 'private_copy_to_user'):
            c.hook(nm, mk(nm))
        rnd = lambda: random.choice([0, 1, 2, random.getrandbits(32)])
        obj = [random.getrandbits(32) for _ in range(n)]
        apiv = [random.getrandbits(32) for _ in range(n)]
        st = [random.choice([0, 1, 2]), random.choice([0, 1, 2])]
        inw = [rnd() for _ in range(uw)]
        c.wrbytes(m.addr(objn), struct.pack('<%dI' % n, *obj))
        c.wrbytes(m.addr(api), struct.pack('<%dI' % n, *apiv))
        c.wrbytes(m.addr(stn), struct.pack('<2I', *st))
        dev = c.heapp; c.heapp += 0x20000
        ctrl = c.heapp; c.heapp += 64
        usr = c.heapp; c.heapp += 0x2000
        c.wrbytes(usr, bytes(0xA5 for _ in range(0x100)))
        c.wrbytes(usr, struct.pack('<%dI' % uw, *inw))
        c.w32(ctrl, cid); c.w32(ctrl + 4, usr)
        rv = c.call('apical_isp_core_ops_s_ctrl', (dev, ctrl))
        sz = [x for x in log if x[0] == 'private_copy_from_user']
        ok = rv == 0 and sz == [('private_copy_from_user', ubytes)]
        open('in.bin', 'wb').write(struct.pack('<%dI' % n, *obj) + struct.pack('<%dI' % n, *apiv) + struct.pack('<2I', *st) + struct.pack('<%dI' % uw, *inw))
        ref = subprocess.check_output(['./ref', kind, 'in.bin']).decode().split('\n')
        gobj = ' '.join(map(str, struct.unpack('<%dI' % n, c.rdbytes(m.addr(objn), 4 * n)))) + ' '
        gapi = ' '.join(map(str, struct.unpack('<%dI' % n, c.rdbytes(m.addr(api), 4 * n)))) + ' '
        gst = '%u %u' % struct.unpack('<2I', c.rdbytes(m.addr(stn), 8))
        if (gobj, gapi, gst) != (ref[0], ref[1], ref[2]):
            ok = False
        # get through g_ctrl: user block poisoned beyond the struct
        log.clear()
        c.wrbytes(usr, bytes(0xA5 for _ in range(0x100)))
        c.w32(ctrl, cid); c.w32(ctrl + 4, usr)
        rv = c.call('apical_isp_core_ops_g_ctrl', (dev, ctrl))
        gsz = [x for x in log if x[0] == 'private_copy_to_user']
        got = ' '.join(map(str, struct.unpack('<%dI' % uw, c.rdbytes(usr, 4 * uw)))) + ' '
        tail = c.rdbytes(usr + ubytes, 16)
        if rv != 0 or gsz != [('private_copy_to_user', ubytes)] or got != ref[3] or tail != bytes([0xA5]) * 16:
            ok = False
        if not ok:
            bad += 1
            if bad < 3:
                print('seed', seed, kind, 'rv', rv, sz, gsz); print(gobj, '|', ref[0]); print(gapi, '|', ref[1]); print(gst, '|', ref[2]); print(got, '|', ref[3])
    print(kind, 'ctrl %#x: bad %d / 200' % (cid, bad))
    tot += bad

# mask: not handled by the stock module (default branch, -1, no user copy)
for fn in ('apical_isp_core_ops_s_ctrl', 'apical_isp_core_ops_g_ctrl'):
    m = Mod(STOCK); c = CPU(m)
    log = []
    for nm in ('private_copy_from_user', 'private_copy_to_user'):
        c.hook(nm, lambda cc, R, nm=nm: log.append(nm) or 0)
    dev = c.heapp; c.heapp += 0x20000
    ctrl = c.heapp; c.heapp += 64
    c.w32(ctrl, 0x80000e5); c.w32(ctrl + 4, c.heapp)
    rv = c.call(fn, (dev, ctrl))
    print('mask', fn, 'rv', rv - (1 << 32) if rv >= 1 << 31 else rv, 'copies', log)
    if rv != 0xffffffff or log:
        tot += 1
print('total bad', tot)
sys.exit(1 if tot else 0)
