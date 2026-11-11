#!/usr/bin/env python3
"""diff_t30_ae_balanced.py VENDOR.ko [--header OUT.h]: T30 AE target/exposure/ratio (Lu-Fi/openimp#283).

Runs the SDK-built tx-isp-t30.ko (4ea12e1f) ae_calculate_target and then ae_calculate_exposure
(which calls ae_calculate_exposure_ratio and tail-calls cmos_set_exposure_target) in Unicorn on
random AE contexts, like AE FSM states 4 and 5.  The math helpers (log2_fixed_to_fixed, math_exp2,
sqrt32, div64_*) and the table getters are replaced by simple models with synthetic tables (no
vendor data); tests/t30_ae_balanced_host_test.sh uses the same models and inputs on our C.
Per case it records the AE context words 4/5/6/7/8/9/10/11, stab items 16/22 and the
cmos_set_exposure_target arguments.
"""
import sys, random, struct, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ko_emu import KoEmu
from unicorn.mips_const import UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3

M32 = 0xffffffff
def s32(x): x &= M32; return x - (1 << 32) if x >> 31 else x
def s64(x): x &= (1 << 64) - 1; return x - (1 << 64) if x >> 63 else x

# ---- models, identical in tests/t30_ae_balanced_host_test.sh ----
def mlog2(x, fin, fout):
    x &= M32
    if x == 0: return 0
    msb = x.bit_length() - 1
    frac = ((x << (31 - msb)) & 0x7fffffff) >> (31 - fout)
    return s32(((msb - fin) << fout) + frac)
def mexp2(v, fin, fout):
    v = s32(v); ip = v >> fin; m = (1 << fin) + (v & ((1 << fin) - 1)); sh = ip + fout - fin
    if sh >= 40 or sh <= -40: return 0
    return (m << sh if sh >= 0 else m >> -sh) & M32
def msqrt(x): return math.isqrt(x & M32)
def xorshift(s):
    s ^= (s << 13) & M32; s ^= s >> 17; s ^= (s << 5) & M32; return s & M32
def hist_of(seed, maxbin):
    s = seed; h = []
    for _ in range(256):
        s = xorshift(s); h.append((s >> 8) % (maxbin + 1))
    if sum(h) == 0: h[128] = 1
    return h

def cases():
    rnd = random.Random(283); out = []
    for k in range(400):
        c = {}
        c['mode'] = rnd.choice([0, 0, 1, 2, 3])
        c['man_exp'] = 1 if rnd.random() < 0.1 else 0
        c['item16'] = rnd.randrange(1, 1 << 20)
        c['man_ratio'] = 1 if rnd.random() < 0.15 else 0
        c['item22'] = rnd.randrange(256); c['item23'] = rnd.randrange(256); c['item53'] = rnd.randrange(256)
        c['k'] = rnd.choice([1, 2, 4, 16, rnd.randrange(1, 64)])
        c['tgt'] = rnd.choice([0x8000, 0x10000, rnd.randrange(1, 1 << 24)])
        c['t4'] = rnd.choice([0, 1, 8, 32, rnd.randrange(0, 256)])
        c['pct'] = rnd.randrange(0, 256)
        c['frames'] = rnd.choice([0, 1, 2, 4, 8])
        n = rnd.randrange(1, 9); p = sorted(rnd.sample(range(1, 1 << 22), n))
        if n > 2 and rnd.random() < 0.3: p[1] = p[0]              # equal neighbours: no interpolation
        c['pts'] = p; c['offs'] = [rnd.randrange(256) for _ in range(n)]
        c['seed'] = rnd.randrange(1, 1 << 32); c['maxbin'] = rnd.choice([1, 3, 100, 3000])
        c['step'] = rnd.randrange(-(1 << 18), 1 << 18)
        c['exp'] = rnd.randrange(0, 22 << 16)
        c['acc'] = rnd.choice([0, rnd.randrange(-(1 << 22), 1 << 24), rnd.randrange(0, 1 << 40)])
        c['pend'] = rnd.choice([0, 0, 0, rnd.randrange(-(1 << 20), 1 << 30)])
        c['filt'] = rnd.randrange(0, 1 << 12)
        c['c100'] = rnd.randrange(0, 1 << 20); c['c104'] = rnd.randrange(0, 1 << 19); c['c740'] = rnd.randrange(0, 1 << 19)
        c['read'] = rnd.randrange(0, 1 << 32)
        out.append(c)
    return out

def main():
    ko = sys.argv[1]
    out = sys.argv[sys.argv.index("--header") + 1] if "--header" in sys.argv else None
    st = {}
    def a(e, r): return e.get_reg(r)
    def div64(signed):
        def f(e):
            n = a(e, UC_MIPS_REG_A0) | a(e, UC_MIPS_REG_A1) << 32
            d = a(e, UC_MIPS_REG_A2) | a(e, UC_MIPS_REG_A3) << 32
            if signed: n, d = s64(n), s64(d); q = (abs(n) // abs(d)) * (1 if (n < 0) == (d < 0) else -1) if d else 0
            else: q = n // d if d else 0
            q &= (1 << 64) - 1
            return (q & M32, q >> 32)
        return f
    def uptr(e):
        i = a(e, UC_MIPS_REG_A0)
        if i == 146 * 16 + st['mode']: return st['lut']
        if i == 206: return st['pts']
        raise RuntimeError("_GET_UINT_PTR(%d)" % i)
    def cptr(e):
        i = a(e, UC_MIPS_REG_A0)
        if i == 41 * 16 + st['mode']: return st['offs']
        raise RuntimeError("_GET_UCHAR_PTR(%d)" % i)
    def glen(e):
        if a(e, UC_MIPS_REG_A0) == 206: return st['n']
        raise RuntimeError("_GET_LEN")
    def cap(e): st['cap'] = (a(e, UC_MIPS_REG_A0), a(e, UC_MIPS_REG_A1), a(e, UC_MIPS_REG_A2)); return 0
    hle = {"div64_s64": div64(True), "div64_u64": div64(False), "printk": lambda e: 0,
           "log2_fixed_to_fixed": lambda e: mlog2(a(e, UC_MIPS_REG_A0), s32(a(e, UC_MIPS_REG_A1)), a(e, UC_MIPS_REG_A2) & 0xff) & M32,
           "math_exp2": lambda e: mexp2(a(e, UC_MIPS_REG_A0), a(e, UC_MIPS_REG_A1) & 0xff, a(e, UC_MIPS_REG_A2) & 0xff),
           "sqrt32": lambda e: msqrt(a(e, UC_MIPS_REG_A0)),
           "_GET_HDR_TABLE_INDEX": lambda e: (a(e, UC_MIPS_REG_A0) * 16 + a(e, UC_MIPS_REG_A1)) & M32,
           "_GET_UINT_PTR": uptr, "_GET_UCHAR_PTR": cptr, "_GET_LEN": glen,
           "APICAL_READ_32": lambda e: st['read'] if a(e, UC_MIPS_REG_A0) == 0x40114 else 0xdead,
           "cmos_set_exposure_target": cap, "get_apical_api_buffer": lambda e: st['buf']}
    e = KoEmu(ko, hle=hle, max_insns=400000)
    for n in hle:
        if n in e.symaddr: e.stubname[e.symaddr[n]] = n
    stab = e.symaddr["stab"]
    ctx = e.alloc(0x1600); ae = e.alloc(0x440)
    st['lut'] = e.alloc(64); st['pts'] = e.alloc(64); st['offs'] = e.alloc(16)
    st['buf'] = e.alloc(0x400)
    # ae_read_full_histogram_data: register words from xorshift(seed), request byte +0x438
    hrows = []
    size = e.symaddr["apical_api_buffer_data_size"]
    for k in range(64):
        seed = random.Random(2830 + k).randrange(1, 1 << 32); req = k & 1; regs = []; x = seed
        for i in range(256): x = xorshift(x); regs.append(x & (0xffff if k % 3 else 0xfff))
        st['regs'] = regs
        e.hle["APICAL_READ_32"] = lambda e_: st['regs'][(a(e_, UC_MIPS_REG_A0) - 0x10000) >> 2]
        e.write(ae, bytes(0x440)); e.write(ae + 0x438, bytes([req])); e.write(st['buf'], bytes(0x400)); e.write(size, bytes(4))
        r = e.call("ae_read_full_histogram_data", ae)
        if r.stop: print("STOP hist %d: %s" % (k, r.stop)); sys.exit(1)
        h = struct.unpack("<256I", e.read(ae + 0x30, 1024)); b = struct.unpack("<256I", e.read(st['buf'], 1024))
        hrows.append((seed, k % 3 != 0, req, struct.unpack("<I", e.read(ae + 0x430, 4))[0],
                      sum((v * (i + 1)) for i, v in enumerate(h)) & M32, sum((v * (i + 1)) for i, v in enumerate(b)) & M32,
                      e.read(ae + 0x438, 1)[0], struct.unpack("<I", e.read(size, 4))[0]))
    e.hle["APICAL_READ_32"] = lambda e_: st['read'] if a(e_, UC_MIPS_REG_A0) == 0x40114 else 0xdead
    rows = []; bad = 0
    for k, c in enumerate(cases()):
        st['mode'] = c['mode']; st['read'] = c['read']; st['n'] = len(c['pts']); st['cap'] = None
        e.write(stab, bytes(0x3c))
        e.write(stab + 1, bytes([c['man_exp'], c['man_ratio']]))
        e.write(stab + 16, struct.pack("<I", c['item16']))
        e.write(stab + 22, bytes([c['item22'], c['item23']])); e.write(stab + 53, bytes([c['item53']]))
        e.write(st['lut'], struct.pack("<6I", c['k'], c['tgt'], c['t4'], c['pct'], c['frames'], 0))
        e.write(st['pts'], struct.pack("<%dI" % st['n'], *c['pts'])); e.write(st['offs'], bytes(c['offs']))
        e.write(ctx, bytes(0x1600)); e.write(ctx + 5412, bytes([c['mode']]))
        e.write(ctx + 100, struct.pack("<ii", c['c100'], c['c104'])); e.write(ctx + 740, struct.pack("<i", c['c740']))
        h = hist_of(c['seed'], c['maxbin'])
        e.write(ae, bytes(0x440)); e.write(ae, struct.pack("<I", ctx))
        e.write(ae + 16, struct.pack("<iiqq", c['step'], c['exp'], c['acc'], c['pend']))
        e.write(ae + 0x2c, struct.pack("<I", c['filt']))
        e.write(ae + 0x30, struct.pack("<256I", *h)); e.write(ae + 0x430, struct.pack("<I", sum(h)))
        r1 = e.call("ae_calculate_target", ae)
        r2 = e.call("ae_calculate_exposure", ae)
        if r1.stop or r2.stop or st['cap'] is None:
            bad += 1; print("STOP case %d: %s / %s" % (k, r1.stop, r2.stop)); continue
        w = struct.unpack("<12i", e.read(ae, 48))
        s16 = struct.unpack("<I", e.read(stab + 16, 4))[0]; s22 = e.read(stab + 22, 1)[0]
        cap = st['cap']
        rows.append((c, (w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], s16, s22, cap[0] - ctx, s32(cap[1]), s32(cap[2]))))
    print("hist: %d cases" % len(hrows))
    print("ae_balanced: %d cases, %d stopped, %d distinct exposures, %d distinct ratios" %
          (len(rows), bad, len(set(r[1][1] for r in rows)), len(set(r[1][6] for r in rows))))
    if out:
        with open(out, "w") as f:
            f.write("/* generated by tools/ko_emu/diff_t30_ae_balanced.py from the SDK-built tx-isp-t30.ko 4ea12e1f\n"
                    " * (ae_calculate_target 0x3182c, then ae_calculate_exposure 0x31cdc -> ae_calculate_exposure_ratio).\n"
                    " * in:  mode man_exp item16 man_ratio item22 item23 item53 k tgt t4 pct frames n pts[8] offs[8]\n"
                    " *      seed maxbin step exp acc pend filt c100 c104 c740 read\n"
                    " * out: ae[4..11] item16 item22 cmos_arg0-ctx cmos_exposure cmos_ratio */\n")
            f.write("struct t30_aeb_case { int mode, man_exp; unsigned item16; int man_ratio, item22, item23, item53;\n"
                    "\tint k; unsigned tgt, t4, pct, frames, n, pts[8]; unsigned char offs[8];\n"
                    "\tunsigned seed, maxbin; int step, exp; long long acc, pend; unsigned filt; int c100, c104, c740; unsigned read;\n"
                    "\tint out[8]; unsigned item16_out, item22_out; int cmos[3]; };\n")
            f.write("static const struct t30_aeb_case t30_aeb_cases[] = {\n")
            for c, o in rows:
                pts = c['pts'] + [0] * (8 - len(c['pts'])); offs = c['offs'] + [0] * (8 - len(c['offs']))
                f.write("\t{ %d, %d, %#x, %d, %d, %d, %d, %d, %#x, %d, %d, %d, %d, { %s }, { %s },\n" % (
                    c['mode'], c['man_exp'], c['item16'], c['man_ratio'], c['item22'], c['item23'], c['item53'],
                    c['k'], c['tgt'], c['t4'], c['pct'], c['frames'], len(c['pts']),
                    ", ".join("%#x" % x for x in pts), ", ".join("%d" % x for x in offs)))
                f.write("\t  %#x, %d, %d, %d, %dLL, %dLL, %d, %d, %d, %d, %#x,\n" % (
                    c['seed'], c['maxbin'], c['step'], c['exp'], c['acc'], c['pend'], c['filt'],
                    c['c100'], c['c104'], c['c740'], c['read']))
                f.write("\t  { %s }, %#x, %d, { %d, %d, %d } },\n" % (", ".join("%d" % x for x in o[:8]), o[8], o[9], o[10], o[11], o[12]))
            f.write("};\n")
            f.write("/* ae_read_full_histogram_data 0x31e78: { seed, 16-bit regs, request, total, sum (i+1) hist[i],\n"
                    " * sum (i+1) buf[i], request byte after, apical_api_buffer_data_size } */\n")
            f.write("static const unsigned t30_aeb_hist[][8] = {\n")
            for h in hrows: f.write("\t{ %#x, %d, %d, %#x, %#x, %#x, %d, %d },\n" % h)
            f.write("};\n")
    sys.exit(1 if bad or not rows else 0)

main()
