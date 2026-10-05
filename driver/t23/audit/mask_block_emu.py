#!/usr/bin/env python3
"""Emulator audit of SetMaskBlock / GetMaskBlock (0x08000183) against the
stock tx-isp-t23.ko (md5 8237acb1).

usage: mask_block_emu.py <stock tx-isp-t23.ko>

Runs the stock apical_isp_core_ops_s_ctrl / g_ctrl (-> tisp_s/g_mscaler_mask_
block_attr) with random mscaler tables and random 20 byte user blocks inside
the valid range (chx 0..2, pinum 0..3) and compares, with the C helpers of
driver/t23/tx_isp_t23_tuning_ext.h (built here with gcc):
  - the whole mscaler object (2316 bytes) after the set (mask table, dirty bit
    mask, everything else untouched) and the user copy size (20 bytes)
  - the block of the get: the bytes the stock code defines (mask_en = 0, top,
    left, width, height, the three colour bytes), the copy-out size (20) and
    no write past the struct.  Stock's get works on an uninitialised stack
    local, the audit pokes chx / pinum into it.
The bytes stock leaves undefined (chx, pinum, mask_type, padding) are not
compared.  Prints "bad 0" per check when identical.
"""
import os, sys, struct, subprocess, tempfile, random
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from memu import Mod, CPU
STOCK = sys.argv[1]
INC = os.path.join(HERE, '..', 'tx_isp_t23_tuning_ext.h')
TMP = tempfile.mkdtemp()
os.chdir(TMP)
SRC = r'''
#include <stdio.h>
#include "%s"
int main(int argc, char **argv)
{
	uint8_t msca[T23X_MSCA_BYTES], blk[T23X_MASK_BLOCK_BYTES], out[T23X_MASK_BLOCK_BYTES];
	int ret;
	FILE *f = fopen(argv[2], "rb");
	if (fread(msca, 1, sizeof(msca), f) != sizeof(msca) || fread(blk, 1, sizeof(blk), f) != sizeof(blk)) return 2;
	fclose(f);
	f = fopen(argv[3], "wb");
	if (argv[1][0] == 's') {
		ret = t23x_mask_block_set(msca, blk);
		fwrite(msca, 1, sizeof(msca), f);
	} else {
		ret = t23x_mask_block_get(msca, blk, out);
		fwrite(out, 1, sizeof(out), f);
	}
	fclose(f);
	return ret ? 1 : 0;
}
''' % INC
open('ref.c', 'w').write(SRC)
subprocess.check_call(['gcc', '-O1', '-Wall', '-o', 'ref', 'ref.c'])

# layout cross-check with the public header's struct (host ABI, fixed width fields)
CHK = r'''
#include <stddef.h>
#include <stdint.h>
typedef enum { M_A = 0, M_B = 1 } IMPISP_MASK_TYPE;
typedef union color_value {
	struct { unsigned char r_value, g_value, b_value; } argb;
	struct { unsigned char y_value, u_value, v_value; } ayuv;
} IMP_ISP_COLOR_VALUE;
typedef struct isp_mask_block_par {
    uint8_t chx; uint8_t pinum; uint8_t mask_en;
	uint16_t mask_pos_top; uint16_t mask_pos_left; uint16_t mask_width; uint16_t mask_height;
	IMPISP_MASK_TYPE mask_type; IMP_ISP_COLOR_VALUE mask_value;
} IMPISPMaskBlockAttr;
_Static_assert(sizeof(IMPISPMaskBlockAttr) == 20, "size");
_Static_assert(offsetof(IMPISPMaskBlockAttr, mask_pos_top) == 4, "top");
_Static_assert(offsetof(IMPISPMaskBlockAttr, mask_pos_left) == 6, "left");
_Static_assert(offsetof(IMPISPMaskBlockAttr, mask_width) == 8, "w");
_Static_assert(offsetof(IMPISPMaskBlockAttr, mask_height) == 10, "h");
_Static_assert(offsetof(IMPISPMaskBlockAttr, mask_type) == 12, "type");
_Static_assert(offsetof(IMPISPMaskBlockAttr, mask_value) == 16, "val");
int main(void) { return 0; }
'''
open('chk.c', 'w').write(CHK)
subprocess.check_call(['gcc', '-o', 'chk', 'chk.c'])

random.seed(11)
MSCA_SYM = 'mscaler'
STACK_LOCAL = 0x7f200000 - 184 + 16     # g_ctrl frame 184, local at sp + 16
bad_s = bad_g = 0
N = 300
for it in range(N):
    m = Mod(STOCK); c = CPU(m)
    log = []

    def mk(name):
        def f(cc, R):
            log.append((name, R[6]))
            cc.wrbytes(R[4], cc.rdbytes(R[5], R[6]))
            return 0
        return f
    for nm in ('private_copy_from_user', 'private_copy_to_user'):
        c.hook(nm, mk(nm))
    table = bytes(random.getrandbits(8) for _ in range(2316))
    c.wrbytes(m.addr(MSCA_SYM), table)
    blk = bytearray(random.getrandbits(8) for _ in range(20))
    blk[0] = random.randint(0, 2)
    blk[1] = random.randint(0, 3)
    blk[2] = random.choice([0, 1, 1, 2, 255])
    dev = c.heapp; c.heapp += 0x20000
    ctrl = c.heapp; c.heapp += 64
    usr = c.heapp; c.heapp += 0x1000
    c.wrbytes(usr, bytes(blk)); c.w32(ctrl, 0x8000183); c.w32(ctrl + 4, usr)
    rv = c.call('apical_isp_core_ops_s_ctrl', (dev, ctrl))
    got = c.rdbytes(m.addr(MSCA_SYM), 2316)
    open('in.bin', 'wb').write(table + bytes(blk))
    rc = subprocess.call(['./ref', 's', 'in.bin', 'out.bin'])
    ref = open('out.bin', 'rb').read()
    ok = rv == 0 and rc == 0 and got == ref and log == [('private_copy_from_user', 20)]
    if not ok:
        bad_s += 1
        if bad_s < 3:
            print('set mismatch', it, rv, rc, log, bytes(blk).hex())
            d = [i for i in range(2316) if got[i] != ref[i]]
            print(' diff at', d[:10], [(got[i], ref[i]) for i in d[:5]])
    # get: the stock table as left by the set
    log.clear()
    gblk = bytearray(random.getrandbits(8) for _ in range(20))
    gblk[0], gblk[1] = blk[0], blk[1]
    c.wrbytes(STACK_LOCAL, bytes(gblk))      # uninitialised local of stock g_ctrl
    c.wrbytes(usr, bytes([0xA5]) * 0x100)
    c.w32(ctrl, 0x8000183); c.w32(ctrl + 4, usr)
    rv = c.call('apical_isp_core_ops_g_ctrl', (dev, ctrl))
    gout = c.rdbytes(usr, 0x100)
    tail = gout[20:]
    open('in.bin', 'wb').write(got + bytes(gblk))
    rc = subprocess.call(['./ref', 'g', 'in.bin', 'out.bin'])
    ref = open('out.bin', 'rb').read()
    defined = [2, 4, 5, 6, 7, 8, 9, 10, 11, 16, 17, 18]
    ok = (rv == 0 and rc == 0 and log == [('private_copy_to_user', 20)] and tail == bytes([0xA5]) * 0xec
          and all(gout[i] == ref[i] for i in defined) and ref[0] == gblk[0] and ref[1] == gblk[1])
    if not ok:
        bad_g += 1
        if bad_g < 3:
            print('get mismatch', it, rv, rc, log, gout[:20].hex(), ref.hex())
print('set: bad %d / %d, get: bad %d / %d' % (bad_s, N, bad_g, N))
# refused ranges: chx >= 3 or pinum >= 4 write nothing
for ch, pi in ((3, 0), (0, 4), (255, 255)):
    blk = bytes([ch, pi, 1]) + bytes(17)
    open('in.bin', 'wb').write(bytes(2316) + blk)
    r1 = subprocess.call(['./ref', 's', 'in.bin', 'out.bin'])
    r2 = subprocess.call(['./ref', 'g', 'in.bin', 'out.bin'])
    if r1 != 1 or r2 != 1:
        bad_s += 1
print('range check done (bad %d)' % bad_s)
sys.exit(1 if bad_s or bad_g else 0)
