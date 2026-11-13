#!/usr/bin/env python3
"""seq_yuv.py A.o B.o: the b72/b73 picture sequence on the YUV matrix path, both builds, stack fill 0x00 and 0xa5:
matrix_yuv_initialize, then saturation_strength-like sets 128, 0, 230, 128 -> matrix_yuv_update; prints differing 0x540..0x7b0 writes."""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import optdiff as o
def seq(path, fill):
    I = o.Inst(path, 3_000_000); uc = I.e.uc; I.seed = 1
    for b, d in I.snap: uc.mem_write(b, d)
    uc.mem_write(o.HEAP, bytes(0x40000)); uc.mem_write(0, bytes(o.LOW)); I.regs = {}; I.log = []
    B = o.HEAP + 0x10000; out = []
    def call(fn, *a):
        I.log = []; I.run_raw(I.funcs[fn], list(a) + [0] * (4 - len(a)), (), fill, 3_000_000)
        return I.stop, [c for c in I.log if c[0] == "system_isp_write_32"]
    out.append(("init", call("matrix_yuv_initialize", B)))
    for sat in (128, 0, 230, 128):
        uc.mem_write(B + 0xa2, bytes([sat])); out.append(("sat%d" % sat, call("matrix_yuv_update", B)))
    return out
r = {(p, f): seq(p, f) for p in sys.argv[1:3] for f in (0, 0xa5)}
ks = list(r); ref = r[ks[0]]; bad = 0
for k in ks[1:]:
    for (n, (st, w)), (n2, (st2, w2)) in zip(ref, r[k]):
        if (st, w) != (st2, w2): bad += 1; print("DIFF", k, n, st, st2, [x for x in zip(w, w2) if x[0] != x[1]][:4])
for n, (st, w) in ref: print(n, st, len(w), " ".join("%x=%x" % (c[1], c[2] & 0xffff) for c in w if 0x540 <= c[1] <= 0x56c))
print("seq_yuv:", "DIFFERENT" if bad else "identical in all 4 runs")
