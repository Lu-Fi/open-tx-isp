#!/usr/bin/env python3
"""Emulator audit of the T23 ISP OSD MSCA update (tx_isp_t23_osd.h,
t23_osd_commit in tx_isp_t23_core.c) against the stock
tisp_msca_Shd_ctrl -> tisp_msca_set_omi_api -> tisp_msca_api_set_osd.

usage: osd_commit_emu.py <stock tx-isp-t23.ko or libt23-firmware-1.3.0-*.a>

Per case: a random mscaler OSD state (attribute bytes, 8 blocks, dirty
word; mask / draw dirty words 0 so only the OSD part runs), stock run with
its static osd_en = 0 and 0xe100 / 0xe00c not busy.  The open sequence is
rebuilt here from tx_isp_t23_osd.h (gcc, same bracket as t23_osd_commit):
0xe000 = 0x101, the pairs on 0xe004, 0xe000 = 0x01010102, 0xe100 kick.
Checked: the whole system_reg_write list and the mscaler bytes afterwards.
The one intended difference is the enable word (pair register 0x48): stock
ORs the enable bytes of the dirty blocks into osd_en, here it is the mask
of all blocks with enable 1.  Cases where both agree (every enabled block
dirty) must match bit for bit; the others must match everywhere but the
0x48 value, which must be the enable mask.  Prints "bad 0" when clean.
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
	if (t23_osd_dirty(m)) {
		wr(T23_MSCA_SHD_CTRL, T23_MSCA_SHD_OPEN);
		pairs = t23_osd_emit(m, pair, NULL);
		wr(T23_MSCA_SHD_CTRL, T23_MSCA_SHD_CLOSE);
		if (pairs > 0)
			wr(T23_MSCA_SHD_KICK, (((unsigned int)pairs - 1U) << 4) | 1U);
		if (pairs != (int)pairs_out || pairs > (int)T23_OSD_MAX_PAIRS) return 98;
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

random.seed(127)
N = int(os.environ.get('N', '300'))
bad = 0
same = diff_en = idle = 0
for it in range(N):
    m = Mod(STOCK); c = CPU(m)
    c.hook('isp_printf', lambda cc, R: 0)
    base = m.addr('mscaler')
    table = bytearray(c.rdbytes(base, 2316))
    for i in range(2172, 2316):
        table[i] = random.getrandbits(8)
    table[2300] = random.choice([0, 1, 0x8f, random.getrandbits(8)])
    table[2301] = random.getrandbits(8)
    table[2302] = random.choice([0, 1, random.getrandbits(8)])
    dirty = random.choice([0, 1, 0x80, 0xff, random.getrandbits(8)])
    struct.pack_into('<I', table, 2304, dirty | (random.choice([0, 0x100]) if dirty else 0))
    struct.pack_into('<I', table, 2308, 0)
    struct.pack_into('<I', table, 2312, 0)
    for n in range(8):
        en = random.choice([0, 1])
        if random.random() < 0.5 and not (dirty >> n) & 1:
            en = 0
        table[2172 + 16 * n + 14] = en
    c.wrbytes(base, bytes(table))
    c.w32(m.addr('osd_en'), 0)
    c.call('tisp_msca_Shd_ctrl', (0,))
    sw = [(a & 0xffff, v & 0xffffffff) for a, v in c.writes]
    sm = c.rdbytes(base, 2316)
    open('in.bin', 'wb').write(bytes(table))
    rc = subprocess.call(['./ref', 'in.bin', 'ow.txt', 'om.bin'])
    ow = [tuple(int(x, 16) for x in l.split()) for l in open('ow.txt')]
    om = open('om.bin', 'rb').read()
    if rc:
        print('case', it, 'ref rc', rc); bad += 1; continue
    if not (dirty & 0xff):
        idle += 1
        if sw or ow:
            print('case', it, 'idle but writes', sw[:4], ow[:4]); bad += 1
        continue
    mask = sum(1 << n for n in range(8) if table[2172 + 16 * n + 14] == 1)
    stock_en = 0
    for n in range(8):
        if (dirty >> n) & 1:
            stock_en |= table[2172 + 16 * n + 14] << n
    if len(sw) != len(ow):
        print('case', it, 'len', len(sw), len(ow)); bad += 1; continue
    ok = True
    for i, (s, o) in enumerate(zip(sw, ow)):
        if s == o:
            continue
        # the value written before register 0x48 (enable word)
        if i + 1 < len(ow) and ow[i + 1] == (0xe004, 0x48) and s[0] == o[0] == 0xe004 \
                and s[1] == stock_en and o[1] == mask:
            continue
        ok = False
        print('case', it, 'write', i, 'stock %x=%x open %x=%x' % (s + o)); break
    if sm != om:
        d = [i for i in range(2316) if sm[i] != om[i]]
        print('case', it, 'mscaler differs at', d[:8]); ok = False
    if not ok:
        bad += 1
    elif stock_en == mask:
        same += 1
    else:
        diff_en += 1
print('cases %d: identical %d, enable word only (beyond stock) %d, nothing dirty %d' % (N, same, diff_en, idle))
print('bad', bad)
sys.exit(1 if bad else 0)
