#!/usr/bin/env python3
"""Lifted-driver typed-pointer scaling, the forms lifted_ptr_scale_check.py does not see (Lu-Fi/openimp#463).

lifted_ptr_scale_check.py only sees statements `X = P + N;` / `P += N;`.  The T30 lift has the same bug in three
more shapes, each of which scales a byte offset or a counter by sizeof(*P):

  castidx  `((T *)p)[N]` with a pointer-sized T (`((void **)p)[0xe6] = 0x438`): offset N * 4 and always a word
           store (#463: sensor_fsm_clear wrote into the AE histogram instead of sensor+0xde..+0xf2)
  inline   `P + N` / `P - N` inside a larger expression on a typed pointer P (element size > 1) that is not cast to
           a byte type first (`*(uint16_t *)(arg1 + 0x1c4)` with int32_t *arg1 = byte +0x710)
  ptrctr   a typed pointer used as a loop counter / index (`int32_t *i; for (i = 0; i < 9; i++)`): i++ steps 4,
           so the loop runs every 4th entry and an index derived from it is x4

Baseline (tests/lifted_ptr_class_baseline.txt): `<tag> <function> <class> <count> <status> [note]`, status as in
lifted_ptr_scale_check.py (`ok` = compared with the vendor ko, the hit is a byte-correct form; `fixed`).  The check
fails when a (function, class) is missing from the baseline or its count grew, so a new instance of the class
must be compared with the vendor ko before it can be baselined.

usage: lifted_ptr_class_check.py [--selftest] [--baseline FILE] [--write-baseline FILE] [--list]
"""
import os, re, sys, argparse, collections

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lifted_ptr_scale_check as psc      # function splitter, inactive-code masks, RE_OFF / RE_ACC

FILES = (("t30", "../driver/t30/tx_isp_t30_recovered.c"),)
NUM = r"(0x[0-9a-fA-F]+|\d+)"
DECL_ANY = re.compile(r"\b(uintptr_t|uint32_t|int32_t|int|uint16_t|int16_t|int64_t|uint64_t|long|unsigned|void \*|char \*|uint8_t \*|struct \w+)\s*\*\s*(\w+)")
DECL_INT = re.compile(r"\b(uintptr_t|uint32_t|int32_t|int|uint16_t|int16_t|long|unsigned)\s*\*\s*(\w+)")
CASTIDX = re.compile(r"\(\(\s*[\w ]+\*\s*\*\s*\)\s*\(?\w+\)?\s*\)\s*\[\s*" + NUM + r"\s*\]")
INLINE = re.compile(r"(\(\s*[\w ]+\**\s*\)\s*)?\b([A-Za-z_]\w*)\s*([+-])\s*" + NUM + r"\b(?!\s*[\*/])")
BYTECAST = re.compile(r"char|uint8_t|uintptr_t|long|void|int\s*\)|int32_t\s*\)|uint32_t\s*\)|size_t")

def prep(lines):
    return psc.cut_hard_returns(psc.mask_inactive(psc.cut_hard_returns(psc.cut_stub_tails(lines))))

def scan(lines):
    """-> {(function, class): [(line, text)]}"""
    hits = collections.defaultdict(list)
    for name, ln, body in psc.functions(prep(lines)):
        typed, ityped = {}, {}
        for l in body:
            if l.lstrip().startswith(("/*", "*", "//")): continue
            for m in DECL_ANY.finditer(l): typed[m.group(2)] = m.group(1)
            for m in DECL_INT.finditer(l): ityped[m.group(2)] = m.group(1)
        for k, l in enumerate(body):
            s = re.sub(r"/\*.*?\*/|//.*$", "", l)
            if not s.strip(): continue
            for m in CASTIDX.finditer(s):
                hits[(name, "castidx")].append((ln + k, s.strip()))
            if psc.RE_OFF.match(s) or psc.RE_ACC.match(s): continue          # lifted_ptr_scale_check's classes
            for m in INLINE.finditer(s):
                cast, idn = m.group(1), m.group(2)
                if idn not in typed: continue
                if cast and BYTECAST.search(cast): continue
                hits[(name, "inline")].append((ln + k, s.strip()))
        txt = "\n".join(body)
        for p in ityped:
            inc = [k for k, l in enumerate(body) if re.search(r"(?<![\w>.])%s(\+\+|--)|(\+\+|--)%s\b" % (p, p), l)]
            if not inc: continue
            asint = re.search(r"\(uintptr_t\)\s*%s\b|\(uint32_t\)\s*%s\b|\b%s\s*(<<|>>)|\b%s\s*[<>]=?\s*(0x[0-9a-f]+|\d+|\w+)\s*[;)]" % (p, p, p, p), txt)
            intinit = re.search(r"\*\s*%s\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;|\b%s\s*=\s*(0x[0-9a-fA-F]+|\d+)\s*;" % (p, p), txt)
            if asint or intinit:
                hits[(name, "ptrctr")].append((ln + inc[0], body[inc[0]].strip()))
    return hits

SELFTEST = r"""
int32_t sensor_fsm_clear(void *arg1)
{
	uint8_t *p = (uint8_t *)arg1;

	((void **)p)[0xe6] = 0x438;
	return 0x438;
}

int32_t cmos_antiflicker_update(int32_t *arg1)
{
	uint16_t val = *(uint16_t *)(arg1 + 0x1c4);
	return val;
}

int32_t scene_mode(void *arg1, int32_t arg2)
{
	int32_t *i;
	for (i = 0; i < 9; i++)
		*(uint16_t *)((char *)arg1 + 0xf98 + (uintptr_t)i * 2) = 0;
	return 0;
}

int32_t good_bytes(void *arg1, int32_t *arg2)
{
	uint8_t *p = (uint8_t *)arg1;
	uint32_t i;

	*(uint16_t *)(p + 0xe6) = 0x438;
	*(uint16_t *)((uint8_t *)arg2 + 0x1c4) = 1;
	for (i = 0; i < 9; i++)
		p[i] = 0;
	return 0;
}
"""

def selftest():
    got = sorted((f, c) for (f, c) in scan(SELFTEST.split("\n")))
    want = [("cmos_antiflicker_update", "inline"), ("scene_mode", "ptrctr"), ("sensor_fsm_clear", "castidx")]
    if got != want:
        print("selftest FAIL: got %s want %s" % (got, want)); return 1
    print("lifted_ptr_class_check selftest: ok (cast-index word store, inline typed offset, pointer counter flagged; byte forms not)")
    return 0

def load_baseline(path):
    b = {}
    for l in open(path):
        l = l.strip()
        if not l or l.startswith("#"): continue
        t = l.split(None, 5)
        b[(t[0], t[1], t[2])] = (int(t[3]), t[4], t[5] if len(t) > 5 else "")
    return b

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--baseline"); ap.add_argument("--write-baseline"); ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    if a.selftest: return selftest()
    cur = {}
    for tag, rel in FILES:
        lines = open(os.path.join(HERE, rel), errors="replace").read().split("\n")
        for (f, c), v in scan(lines).items():
            cur[(tag, f, c)] = v
            if a.list:
                for ln, t in v: print("%s %s:%d %s %s" % (tag, f, ln, c, t[:140]))
    if a.write_baseline:
        old = load_baseline(a.write_baseline) if os.path.exists(a.write_baseline) else {}
        with open(a.write_baseline, "w") as o:
            o.write("# lifted_ptr_class_check.py baseline: tag function class count status [note] (#463)\n")
            for k in sorted(cur):
                st, note = (old[k][1], old[k][2]) if k in old else ("open", "")
                o.write("%s %s %s %d %s%s\n" % (k + (len(cur[k]), st, " " + note if note else "")))
        print("wrote %s (%d entries)" % (a.write_baseline, len(cur))); return 0
    if not a.baseline: return 0
    base = load_baseline(a.baseline); bad = 0
    for k, v in sorted(cur.items()):
        if k not in base:
            bad += 1; print("NEW  %s %s %s: %d hit(s), first %s:%d %s" % (k + (len(v), k[1], v[0][0], v[0][1][:100])))
        elif len(v) > base[k][0]:
            bad += 1; print("GREW %s %s %s: %d > baseline %d" % (k + (len(v), base[k][0])))
        elif len(v) < base[k][0]:
            print("note: %s %s %s shrank %d -> %d (tighten the baseline)" % (k + (base[k][0], len(v))))
    opn = [k for k, v in base.items() if v[1] == "open" and k in cur]
    print("lifted_ptr_class_check: %d (function, class) entries, %d open, %s" % (len(cur), len(opn), "FAIL" if bad else "ok"))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
