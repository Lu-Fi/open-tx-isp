#!/usr/bin/env python3
"""Emulator audit of the T23 ISP mask (cover) MSCA update (tx_isp_t23_osd.h
t23_mask_emit, t23_osd_commit in tx_isp_t23_core.c) against the stock
tisp_msca_Shd_ctrl -> tisp_msca_set_omi_api -> tisp_msca_api_set_mask.

usage: mask_commit_emu.py <stock tx-isp-t23.ko>

Per case: a random mask table (12 blocks @1836, enable 0/1) and mask dirty
word @2308 (OSD / draw dirty words 0 so only the mask part runs), stock run
with its static mask_en = 0 and 0xe100 / 0xe00c not busy; the open side is
the same bracket as t23_osd_commit (gcc build of tx_isp_t23_osd.h).
Checked: the whole system_reg_write list and the mscaler bytes afterwards.
Every write must be identical, also the mask word (register 0x44, stock
mask_en: channel bits OR-ed after each channel, never cleared; the open side
keeps it in t23_mask_word, here starting at 0 like the stock run).
Prints "bad 0" when clean.
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
	if (t23_osd_dirty(m) || t23_mask_dirty(m)) {
		wr(T23_MSCA_SHD_CTRL, T23_MSCA_SHD_OPEN);
		pairs = t23_osd_emit(m, pair, NULL);
		if (pairs >= 0) {
			unsigned int word = 0;
			int mp = t23_mask_emit(m, pair, NULL, &word);
			pairs = mp < 0 ? mp : pairs + mp;
		}
		wr(T23_MSCA_SHD_CTRL, T23_MSCA_SHD_CLOSE);
		if (pairs > 0)
			wr(T23_MSCA_SHD_KICK, (((unsigned int)pairs - 1U) << 4) | 1U);
		if (pairs != (int)pairs_out || pairs > (int)T23_MASK_MAX_PAIRS) return 98;
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

random.seed(183)
N = int(os.environ.get('N', '300'))
bad = 0
same = diff_en = idle = 0
for it in range(N):
    m = Mod(STOCK); c = CPU(m)
    c.hook('isp_printf', lambda cc, R: 0)
    base = m.addr('mscaler')
    table = bytearray(c.rdbytes(base, 2316))
    for i in range(1836, 1836 + 16 * 12):
        table[i] = random.getrandbits(8)
    dirty = random.choice([0, 1, 0x10, 0x100, 0xfff, random.getrandbits(12)])
    for n in range(12):
        table[1836 + 16 * n] = random.choice([0, 1])
    struct.pack_into('<I', table, 2304, 0)
    struct.pack_into('<I', table, 2308, dirty)
    struct.pack_into('<I', table, 2312, 0)
    c.wrbytes(base, bytes(table))
    c.w32(m.addr('mask_en'), 0)
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
    chbit = [any(table[1836 + 16 * (4 * ch + i)] == 1 for i in range(4)) for ch in range(3)]
    full = sum(1 << ch for ch in range(3) if chbit[ch])
    stock_vals = []
    acc = 0
    for ch in range(3):
        acc |= (1 << ch) if chbit[ch] else 0
        stock_vals.append(acc)
    if len(sw) != len(ow):
        print('case', it, 'len', len(sw), len(ow)); bad += 1; continue
    ok = True; k = 0; differed = False
    for i, (s, o) in enumerate(zip(sw, ow)):
        is_en = i + 1 < len(ow) and ow[i + 1] == (0xe004, 0x44) and s[0] == o[0] == 0xe004
        if is_en:
            if s[1] != o[1] or s[1] != stock_vals[k]:
                ok = False; print('case', it, 'enable write', k, 'stock %x open %x want %x/%x' % (s[1], o[1], stock_vals[k], full)); break
            differed |= s[1] != o[1]
            k += 1
            continue
        if s != o:
            ok = False; print('case', it, 'write', i, 'stock %x=%x open %x=%x' % (s + o)); break
    if sm != om:
        d = [i for i in range(2316) if sm[i] != om[i]]
        print('case', it, 'mscaler differs at', d[:8]); ok = False
    if not ok:
        bad += 1
    elif differed:
        diff_en += 1
    else:
        same += 1
print('cases %d: identical %d, nothing dirty %d' % (N, same + diff_en, idle))
print('bad', bad)
sys.exit(1 if bad else 0)
