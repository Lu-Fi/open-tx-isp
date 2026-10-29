#!/usr/bin/env python3
"""Emulator audit of the T23 ISP draw-block (line / window) MSCA update
(tx_isp_t23_osd.h t23_draw_emit, t23_osd_commit in tx_isp_t23_core.c) against
the stock tisp_msca_Shd_ctrl -> tisp_msca_set_omi_api -> tisp_msca_api_set_line.

usage: draw_commit_emu.py <stock tx-isp-t23.ko>

Per case: six random draw entries @2028 (type 0..3, points with x1 < x0,
equal x or y, flag byte 0 / not 0) and a random draw dirty word @2312 (OSD /
mask dirty 0), stock run with 0xe100 / 0xe00c not busy; the open side is the
same bracket as t23_osd_commit (gcc build of tx_isp_t23_osd.h).  Every write
and the mscaler bytes afterwards must be identical.  Prints "bad 0" when clean.
"""
import os, sys, subprocess, tempfile, random, struct
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from memu import Mod, CPU
STOCK = sys.argv[1]
INC = os.environ.get('INC') or os.path.join(HERE, '..', 'tx_isp_t23_osd.h')
TMP = tempfile.mkdtemp()
os.chdir(TMP)
SRC = r'''
#include <stdio.h>
#include "%s"
static FILE *out;
static unsigned int pairs_out;
static void wr(unsigned int reg, unsigned int v) { fprintf(out, "%%x %%x\n", reg, v); }
static int pair(void *ctx, unsigned int value, unsigned int reg)
{
	(void)ctx;
	wr(T23_MSCA_SHD_PORT, value);
	wr(T23_MSCA_SHD_PORT, reg);
	pairs_out++;
	return 0;
}
int main(int argc, char **argv)
{
	uint8_t m[2316];
	FILE *f = fopen(argv[1], "rb");
	int pairs;

	(void)argc;
	if (fread(m, 1, sizeof(m), f) != sizeof(m)) return 99;
	fclose(f);
	out = fopen(argv[2], "w");
	if (t23_osd_dirty(m) || t23_mask_dirty(m) || t23_draw_dirty(m)) {
		wr(T23_MSCA_SHD_CTRL, T23_MSCA_SHD_OPEN);
		pairs = t23_osd_emit(m, pair, NULL);
		if (pairs >= 0) {
			unsigned int word = 0;
			int mp = t23_mask_emit(m, pair, NULL, &word);
			pairs = mp < 0 ? mp : pairs + mp;
		}
		if (pairs >= 0) {
			int dp = t23_draw_emit(m, pair, NULL);
			pairs = dp < 0 ? dp : pairs + dp;
		}
		wr(T23_MSCA_SHD_CTRL, T23_MSCA_SHD_CLOSE);
		if (pairs > 0)
			wr(T23_MSCA_SHD_KICK, (((unsigned int)pairs - 1U) << 4) | 1U);
		if (pairs != (int)pairs_out || pairs > (int)T23_DRAW_MAX_PAIRS) return 98;
	}
	fclose(out);
	f = fopen(argv[3], "wb");
	fwrite(m, 1, sizeof(m), f);
	fclose(f);
	return 0;
}
''' % INC
open('ref.c', 'w').write(SRC)
subprocess.check_call(['gcc', '-O1', '-Wall', '-Wextra', '-o', 'ref', 'ref.c'])

random.seed(311)
N = int(os.environ.get('N', '400'))
bad = 0
same = idle = nokick = 0
for it in range(N):
    m = Mod(STOCK); c = CPU(m)
    c.hook('isp_printf', lambda cc, R: 0)
    base = m.addr('mscaler')
    table = bytearray(c.rdbytes(base, 2316))
    for n in range(6):
        e = 2028 + 24 * n
        for i in range(24):
            table[e + i] = random.getrandbits(8)
        table[e] = random.choice([0, 1, 2, 2, 2, 3])
        table[e + 4] = random.choice([0, 1, random.getrandbits(8)])
        x0 = random.randrange(0, 2000); y0 = random.randrange(0, 1200)
        r = random.random()
        if r < 0.15: x1, y1 = x0, random.randrange(0, 1200)
        elif r < 0.3: x1, y1 = random.randrange(0, 2000), y0
        elif r < 0.35: x1, y1 = x0, y0
        else: x1, y1 = random.randrange(0, 2000), random.randrange(0, 1200)
        struct.pack_into('<HHHH', table, e + 6, x0, y0, x1, y1)
        table[e + 20] = random.choice([0, 1, 4, random.getrandbits(8)])
    dirty = random.choice([0, 1, 0x20, 0x3f, random.getrandbits(6)])
    struct.pack_into('<I', table, 2304, 0)
    struct.pack_into('<I', table, 2308, 0)
    struct.pack_into('<I', table, 2312, dirty)
    c.wrbytes(base, bytes(table))
    c.call('tisp_msca_Shd_ctrl', (0,))
    sw = [(a & 0xffff, v & 0xffffffff) for a, v in c.writes]
    sm = c.rdbytes(base, 2316)
    open('in.bin', 'wb').write(bytes(table))
    rc = subprocess.call(['./ref', 'in.bin', 'ow.txt', 'om.bin'])
    ow = [tuple(int(x, 16) for x in l.split()) for l in open('ow.txt')]
    om = open('om.bin', 'rb').read()
    if rc:
        print('case', it, 'ref rc', rc); bad += 1; continue
    if not dirty:
        idle += 1
        if ow:
            print('case', it, 'idle but open writes', ow[:4]); bad += 1
        continue
    # stock kicks 0xe100 with a pair count of -1 when every dirty block has a type > 2 (0 pairs); the open
    # side does not kick an empty update (intended difference, counted apart)
    if sw == ow + [(0xe100, 0xfffffff1)] and sm == om:
        nokick += 1; continue
    if sw != ow:
        k = next((i for i, (a, b) in enumerate(zip(sw, ow)) if a != b), min(len(sw), len(ow)))
        print('case', it, 'write', k, 'stock', sw[k:k+2], 'open', ow[k:k+2], 'len', len(sw), len(ow)); bad += 1; continue
    if sm != om:
        d = [i for i in range(2316) if sm[i] != om[i]]
        print('case', it, 'mscaler differs at', d[:8]); bad += 1; continue
    same += 1
print('cases %d: identical %d, only types > 2 dirty (stock kicks an empty update, open does not) %d, nothing dirty %d' % (N, same, nokick, idle))
print('bad', bad)
sys.exit(1 if bad else 0)
