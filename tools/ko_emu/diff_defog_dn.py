#!/usr/bin/env python3
"""diff_defog_dn.py OEM_KO BIN HOST_BIN [--n N] [--seed S] [--air-day 0|1] [--trans 0|1 --trans-day 0|1 --ev EV
--night-start 0|1]: the OEM defog day/night flow (vendor
tx-isp-t40.ko, Unicorn: tisp_defog_init with the day bank, D x tisp_defog_main_process, tisp_defog_dn_params_refresh
with the night bank, N x tisp_defog_main_process) against the same sequence of driver/t40/tx_isp_t40_defog.h (HOST_BIN
from tests/t40_defog/dn_harness.c) (#332).  The night frames get the statistics of the day frames again (steady state:
every value was seen under the day bank, so the OEM serves the day air light curve from its per-value cache).
Compared: the three air light outputs after every night frame; --trans 1 adds the fourth output (transmission): the day
frames then cover every luma 180..255 (bright cells), the night starts with an EV update to --ev (the OEM drops the
transmission cache of luma 0..179 only, so 180..255 keep the day value), --night-start 1 runs the host in the order of
this driver (night bank active at the defog init, no day frames in the day bank).  Exit 1 on any difference."""
import argparse, os, random, struct, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_PC

ap = argparse.ArgumentParser(); ap.add_argument("ko"); ap.add_argument("bin"); ap.add_argument("host")
ap.add_argument("--n", type=int, default=6); ap.add_argument("--seed", type=int, default=332)
ap.add_argument("--air-day", type=int, default=1)
ap.add_argument("--trans", type=int, default=0); ap.add_argument("--trans-day", type=int, default=1)
ap.add_argument("--ev", type=int, default=0); ap.add_argument("--night-start", type=int, default=0)
a = ap.parse_args(); rnd = random.Random(a.seed)
BK = 0x18ff4; B = open(a.bin, "rb").read()
RES = [(3840, 2160)]	# the bank resolution: no count_weight (tests/t40/c_chains_host covers it), Unicorn stays fast

def kmalloc(e):
	p = e.alloc(0x1000); e.write(p, bytes(rnd.getrandbits(8) for _ in range(0x1000))); return p
def vmalloc(e):	# vmalloc(size): count_weight needs a large buffer
	n = e.get_reg(UC_MIPS_REG_A0); return e.alloc(max(n, 16))
def memset(e):
	p, v, n = e.get_reg(UC_MIPS_REG_A0), e.get_reg(UC_MIPS_REG_A1), e.get_reg(UC_MIPS_REG_A2)
	if n: e.write(p, bytes([v & 255]) * n)
	return p

def stats():
	sh = rnd.choice([0, 1, 2, 3, 4])	# dark night scenes: small sums
	return [rnd.getrandbits(21) >> sh >> rnd.randrange(0, 3) for _ in range(540)]

def bright(w, h):
	"""cell sums whose luma (all three channels equal) covers 180..255 in random cells, the rest random"""
	ch, cw = (h >> 1) // 10 * 2, (w >> 1) // 18 * 2
	ar = [(ch + 2) * (cw + 2), (ch + 2) * cw, ch * (cw + 2), ch * cw]
	rc = [((x >> 1) + 0x80000000) // x for x in ar]
	rh, rw = (h >> 1) % 10, (w >> 1) % 18
	prec = 0 if ar[0] <= 8192 else 1 if ar[0] <= 16384 else 2 if ar[0] <= 32768 else 3
	sh = 31 - prec
	ys = list(range(180, 256)); rnd.shuffle(ys); ys += [rnd.randrange(0, 256) for _ in range(180 - len(ys))]
	rnd.shuffle(ys); st = []
	for k, y in enumerate(ys):
		r, c = divmod(k, 18); q = rc[(0 if r < rh else 2) + (0 if c < rw else 1)]; b = (q >> (30 - prec)) & 1
		s = max(0, ((y - b) << sh) // q)
		while ((s * q) >> sh) + b < y: s += 1
		assert ((s * q) >> sh) + b == y and s < 1 << 21, (y, s)
		st.append(s)
	return st * 3

def oem(w, h, frames, nd):
	e = KoEmu(a.ko, hle={"kmem_cache_alloc": kmalloc, "__kmalloc": kmalloc, "vmalloc": vmalloc, "memset": memset})
	for s in list(e.stubname.values()): e.hle.setdefault(s, lambda e: 0)
	rb = e.alloc(0x100000); sd = e.alloc(0x100); e.write(sd + 232, struct.pack("<I", rb))
	e.write(e.addr("ispcore_sd"), struct.pack("<I", sd))	# system_reg_write target
	banks = [e.alloc(BK, B[0x18:0x18 + BK]), e.alloc(BK, B[0x18 + BK:0x18 + 2 * BK])]
	tp = e.addr("tparamsN"); e.write(tp, struct.pack("<I", banks[0]))
	r = e.call("tisp_defog_init", 0, w, h); assert r.stop is None and r.insns < e.max_insns, (r.stop, r.insns)
	out = b""
	for f, st in enumerate(frames):
		if f == nd:
			e.write(tp, struct.pack("<I", banks[1]))
			r = e.call("tisp_defog_dn_params_refresh", 0, w, h); assert r.stop is None, r.stop
			if a.ev:
				r = e.call("tisp_defog_ev_update", 0, 0, (a.ev << 10) & 0xffffffff, a.ev >> 22); assert r.stop is None, r.stop
		sp = struct.unpack("<I", e.read(e.addr("defog_main_soft_para"), 4))[0]
		e.write(sp, struct.pack("<540I", *st))
		r = e.call("tisp_defog_main_process", 0, 0, 0, 0); assert r.stop is None, (r.stop, f, nd, e.where(e.get_reg(UC_MIPS_REG_PC)))
		if f >= nd: out += e.read(sp + 2459, OUT)
	return out

OUT = 720 if a.trans else 540
bad = 0
for n in range(a.n):
	w, h = RES[n % len(RES)]; nd = rnd.randrange(2, 5)
	day = [bright(w, h) if a.trans and f == 0 else stats() for f in range(nd)]; frames = day + day
	o = oem(w, h, frames, nd)
	p = subprocess.run([a.host, a.bin, str(w), str(h), str(nd), str(nd), str(a.air_day), str(a.trans_day), str(a.ev),
			    str(a.night_start)], input=b"".join(struct.pack("<540I", *s) for s in frames), capture_output=True)
	ho = b"".join(p.stdout[i:i + OUT] for i in range(0, len(p.stdout), 720))
	if p.returncode or len(ho) != len(o):
		print("diff_defog_dn: host failed rc %d (%d bytes)" % (p.returncode, len(p.stdout))); sys.exit(1)
	d = [k for k in range(len(o)) if o[k] != ho[k]]
	if d:
		bad += 1
		if bad <= 3:
			k = d[0]
			tr = [o[i] for i in range(len(o)) if i % OUT >= 540]; th = [ho[i] for i in range(len(o)) if i % OUT >= 540]
			print("diff_defog_dn: case %d %dx%d: %d bytes differ, first night frame %d out%d cell %d OEM %d open %d (OEM mean %.1f open %.1f%s)" %
			      (n, w, h, len(d), k // OUT, k % OUT // 180, k % 180, o[k], ho[k], sum(o) / len(o), sum(ho) / len(o),
			       ", transmission mean OEM %.1f open %.1f" % (sum(tr) / len(tr), sum(th) / len(th)) if tr else ""))
print("diff_defog_dn: %d cases (day -> night, air_day %d, trans %d, trans_day %d, ev %d, night_start %d): %d differences" %
      (a.n, a.air_day, a.trans, a.trans_day, a.ev, a.night_start, bad))
sys.exit(1 if bad else 0)
