#!/usr/bin/env python3
"""diff_defog_soft.py OEM_KO HOST_BIN [--n N] [--seed S] [--frames F]: OEM Tiziano_defog_soft (vendor tx-isp-t40.ko,
Unicorn) against t40_dfg_soft of driver/t40/tx_isp_t40_defog.h (HOST_BIN from tests/t40_defog/harness.c) (#332).
Per case a random tool block (ctl, curve, kernels, luma knots, current levels) and soft state (statistics, cell
reciprocals, previous luma, caches) run for F consecutive frames on both sides (state carried over, new statistics
per frame); compared: the whole soft image after every frame (outputs, previous luma, cache flags and values), except
the two OEM pointer words.  The host side runs with t40_defog_motion_var=0 (OEM-exact).  Exit 1 on any difference."""
import argparse, os, random, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu
from unicorn.mips_const import UC_MIPS_REG_A0

ap = argparse.ArgumentParser(); ap.add_argument("ko"); ap.add_argument("host")
ap.add_argument("--n", type=int, default=300); ap.add_argument("--seed", type=int, default=332)
ap.add_argument("--frames", type=int, default=3)
a = ap.parse_args(); rnd = random.Random(a.seed)
SOFT, TOOL, CELLS = 3948, 812, 180
heap = {}
def kmalloc(e):	# kmem_cache_alloc(cache, flags): fresh garbage-filled block (the OEM writes it before reading)
	p = e.alloc(0x400); e.write(p, bytes(rnd.getrandbits(8) for _ in range(0x400))); return p
e = KoEmu(a.ko, hle={"kmem_cache_alloc": kmalloc, "kfree": lambda e: 0})

def tool_block():
	t = bytearray(rnd.getrandbits(8) for _ in range(TOOL))
	ctl = [0, 0, rnd.choice([0, 1, rnd.getrandbits(8)]), rnd.choice([255, 240, rnd.getrandbits(8)]), rnd.choice([128, 88, rnd.getrandbits(8)]),
	       1, rnd.choice([81, rnd.randrange(1, 256)]), rnd.choice([75, rnd.getrandbits(8)]), rnd.choice([63, rnd.getrandbits(8)]), rnd.choice([0, 1])]
	if rnd.random() < 0.15: ctl[0] = 1			# simple luma mode
	if rnd.random() < 0.3: ctl[1] = 1			# one curve for all channels
	if rnd.random() < 0.2: ctl[5] = 0
	t[0:10] = bytes(ctl)
	cv = sorted(rnd.getrandbits(8) for _ in range(17)) if rnd.random() < 0.8 else [rnd.getrandbits(8) for _ in range(17)]
	t[10:27] = bytes(cv)
	t[27:36] = bytes(rnd.choice([[1, 2, 1, 2, 4, 2, 1, 2, 1], [rnd.getrandbits(5) for _ in range(9)]]))
	t[36:61] = bytes(rnd.choice([[1] * 25, [rnd.getrandbits(4) for _ in range(25)]]))
	kn = sorted(rnd.sample(range(1, 255), 5)) if rnd.random() < 0.9 else [64, 96, 144, 180, 224]
	t[149:154] = bytes(kn); t[154:159] = bytes(rnd.randrange(0, 128) for _ in range(5))
	return t

def stats_words(mode):
	w = []
	for _ in range(3 * CELLS):
		v = rnd.getrandbits(21)
		if mode == 1: v >>= rnd.randrange(0, 8)
		elif mode == 2: v = rnd.randrange(0, 1 << rnd.choice([10, 14, 18]))
		w.append(v)
	return w

def soft_block(tool):
	s = bytearray(SOFT)
	s[0:2160] = struct.pack("<540I", *stats_words(rnd.randrange(3)))
	prec = rnd.choice([3, 2, 4, rnd.randrange(0, 8)])
	area = [rnd.choice([rnd.randrange(64, 70000), rnd.randrange(1, 16)]) for _ in range(4)]
	s[2160:2176] = struct.pack("<4I", *[min((1 << 31) // x, 0x7fffffff) for x in area])
	s[2176:2356] = bytes(rnd.getrandbits(8) for _ in range(CELLS))
	s[2356:2373] = tool[10:27]; s[2373:2382] = tool[27:36]; s[2382:2407] = tool[36:61]; s[2407:2417] = tool[0:10]
	s[2456] = rnd.randrange(0, 11); s[2457] = rnd.randrange(0, 19); s[2458] = prec
	s[2459:3179] = bytes(rnd.getrandbits(8) for _ in range(720))
	if rnd.random() < 0.5:					# warm caches
		for i in range(256):
			f = rnd.choice([0, 1, 0x10, 0x11]); s[3179 + i] = f
			s[3435 + i] = rnd.getrandbits(8); s[3691 + i] = rnd.getrandbits(8)
	return s

SP = e.addr("defog_main_soft_para")
cases, oem = [], []
for n in range(a.n):
	e.heap_reset()
	tool = tool_block(); soft = soft_block(tool)
	tp = e.alloc(TOOL, tool); sp = e.alloc(SOFT + 16)
	cur = bytes(soft)
	for f in range(a.frames):
		if f:
			cur = bytearray(cur); cur[0:2160] = struct.pack("<540I", *stats_words(rnd.randrange(3))); cur = bytes(cur)
		img = bytearray(cur); img[2448:2456] = struct.pack("<2I", tp + 149, tp + 154)
		e.write(sp, img); e.write(SP, struct.pack("<I", sp))
		r = e.call("Tiziano_defog_soft", sp)
		assert r.stop is None, r.stop
		out = e.read(sp, SOFT)
		cases.append(bytes(tool) + bytes(cur)); oem.append(out)
		cur = out
proc = subprocess.run([a.host, "0"], input=b"".join(cases), capture_output=True)
hout = proc.stdout
bad = 0
names = [(0, 2160, "stat"), (2176, 2356, "prev_y"), (2459, 2639, "out0"), (2639, 2819, "out1"), (2819, 2999, "out2"),
	 (2999, 3179, "out3"), (3179, 3435, "flags"), (3435, 3691, "cache_curve"), (3691, 3947, "cache_trans")]
if len(hout) != len(cases) * SOFT:
	print("diff_defog_soft: host gave %d bytes for %d cases" % (len(hout), len(cases))); sys.exit(1)
for i, o in enumerate(oem):
	h = hout[i * SOFT:(i + 1) * SOFT]
	d = [k for k in range(SOFT) if not 2417 <= k < 2456 and o[k] != h[k]]
	if d:
		bad += 1
		if bad <= 5:
			nm = next((x[2] for x in names if x[0] <= d[0] < x[1]), "?")
			print("diff_defog_soft: case %d frame %d ctl %s: %d bytes differ, first +%d (%s) OEM %d open %d" %
			      (i // a.frames, i % a.frames, list(cases[i][:10]), len(d), d[0], nm, o[d[0]], h[d[0]]))
print("diff_defog_soft: %d cases x %d frames: %d differences" % (a.n, a.frames, bad))
sys.exit(1 if bad else 0)
