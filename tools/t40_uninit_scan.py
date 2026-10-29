#!/usr/bin/env python3
"""t40_uninit_scan.py - static check for decompiler artefacts in the lifted T40 driver (issue #139).

  python3 tools/t40_uninit_scan.py [driver/t40/tx_isp_t40_recovered.c] [--ioctl]

--ioctl: only hits in functions within call depth 4 of the T40 control/tuning ioctl handlers
(column 3 is the depth, ioctl+0 = the handler itself; references through function pointers count).

Flags, per function (heuristic, linear order, no control flow; second filter next to
tools/t40_uninit_gcc.sh which compiles with -Wuninitialized -Wmaybe-uninitialized -O2):

  STORE   store through a local pointer that is declared without initialiser and never
          assigned before the store (`int32_t *var_14; ... var_14[0] = x;`), or through a
          pointer-typed OEM frame slot that is never written (`(*__p_70)[0] = x`).
          In the OEM these were stack slots (debug printf arguments); compiled they write to
          stack garbage or address 0.
  LEAK    copy_to_user family whose kernel source is a frame slot / local that the function
          never writes (no store, no memcpy/memset/call with it as argument).  `__frame` with
          `= { 0 }` is reported as ZERO (no stack leak, but the getter call is probably lost).
  ARITY   a call through a cast function pointer type with fewer parameters than the callee's
          prototype (`((uintptr_t (*)(uintptr_t))tisp_g_Gamma)(a0)` for a 2-argument getter):
          the callee takes the lost arguments from stale registers.

Output: one line per hit `KIND line function: detail`.  Exit 0 always (it is a report).
Preprocessor: only the first branch of every #if/#else is scanned.
"""
import re
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
src = args[0] if args else "driver/t40/tx_isp_t40_recovered.c"
raw = open(src, errors="replace").read()


def strip(text):
    """blank comments and string/char literals, keep newlines (line numbers stay valid)"""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j])); i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + " " * (j - i - 1) + c); i = j + 1
        else:
            out.append(c); i += 1
    return "".join(out)


code = strip(raw)
lines = code.split("\n")
# blank preprocessor lines (incl. continuations) and #else/#elif branches (first branch kept),
# so that brace counting sees one consistent version of the code
_skip = []          # stack: True while inside an #else branch being dropped
_cont = False
for _i, _l in enumerate(lines):
    _s = _l.lstrip()
    if _cont or _s.startswith("#"):
        _cont = _l.rstrip().endswith("\\")
        if not _s.startswith("#"):
            lines[_i] = ""
            continue
        _d = _s[1:].lstrip()
        if _d.startswith("if"):
            # `#if 0` blocks hold superseded lift bodies: drop them, keep their #else
            _skip.append(bool(re.match(r"if\s+0\b", _d)))
        elif _d.startswith(("else", "elif")) and _skip:
            _skip[-1] = not _skip[-1] if _d.startswith("else") else True
        elif _d.startswith("endif") and _skip:
            _skip.pop()
        lines[_i] = ""
        continue
    if any(_skip):
        lines[_i] = ""
code = "\n".join(lines)

# ---- functions: top-level brace blocks preceded by a header with '(' ----
funcs = []      # (name, start_line_idx, end_line_idx)
depth = 0
start = None
for idx, ln in enumerate(lines):
    if ln.startswith("#"):
        continue
    for ch in ln:
        if ch == "{":
            if depth == 0:
                start = idx
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                hdr = " ".join(lines[max(0, start - 3):start + 1])
                m = re.findall(r"(\w+)\s*\([^;{}]*\)\s*(?:__asm__\([^)]*\)\s*)?\{?\s*$", hdr)
                if m and not re.search(r"=\s*\{?\s*$", lines[start].split("{")[0] + "{"):
                    funcs.append((m[-1], start, idx))
                start = None

# ---- prototypes for ARITY ----
proto = {}
for m in re.finditer(r"^[A-Za-z_][\w \t\*]*?\b(\w+)\s*\(([^;{}()]*)\)\s*(?:__asm__\([^)]*\))?\s*[;{]?\s*$", code, re.M):
    name, plist = m.group(1), m.group(2).strip()
    if name in ("if", "while", "for", "switch", "return", "sizeof"):
        continue
    if "..." in plist:
        proto[name] = None
        continue
    cnt = 0 if plist in ("", "void") else plist.count(",") + 1
    if name in proto and proto[name] is not None and proto[name] != cnt:
        proto[name] = max(proto[name], cnt)
    else:
        proto.setdefault(name, cnt)

hits = []
COPY_TO = r"(?:private_copy_to_user|copy_to_user|__copy_to_user|__copy_user_to|regtrace_copy_to_user)"
decl_ptr = re.compile(r"^\s*(?:const\s+|volatile\s+|unsigned\s+|signed\s+|struct\s+\w+\s+)*[A-Za-z_]\w*(?:\s+[A-Za-z_]\w*)?\s*((?:\*+\s*\w+\s*(?:,\s*)?)+);\s*$")

for name, s, e in funcs:
    body = lines[s:e + 1]
    text = "\n".join(body)
    # --- STORE: never-assigned local pointers ---
    for k, ln in enumerate(body):
        m = decl_ptr.match(ln)
        if not m or "(" in ln:
            continue
        for v in re.findall(r"\*+\s*(\w+)", m.group(1)):
            assigned = False
            for k2 in range(k + 1, len(body)):
                l2 = body[k2]
                if re.search(r"(?<![\w.>])" + v + r"\s*(?:=[^=]|\+=|-=|\+\+|--)|&\s*" + v + r"\b|\(\s*" + v + r"\s*\)\s*=[^=]", l2):
                    assigned = True
                    break
                if re.search(r"(?<![\w.>])" + v + r"\s*\[[^\]]*\]\s*=[^=]|\*\s*\(?\s*" + v + r"\s*\)?\s*=[^=]|(?<![\w.>])" + v + r"\s*->\s*\w+\s*=[^=]", l2) and \
                        not re.search(r"\b\w+\s*\*\s*" + v + r"\s*=", l2):
                    hits.append(("STORE", s + k2 + 1, name, "store through never-assigned local pointer '%s'" % v))
                    break
            del assigned
    # --- STORE through pointer-typed frame slots never written ---
    slots = set(re.findall(r"\*\*\s*const\s+(__p_\w+)\s*=", text))
    for v in slots:
        written = re.search(r"(?<![\w(])\*\s*" + v + r"\s*=[^=]|" + v + r"\s*\[\s*0\s*\]\s*=[^=]", text) or \
            re.search(r"\w+\s*\([^;]*\b" + v + r"\b(?!\s*\))[^;]*\)", text.replace("(*" + v + ")", "")) or \
            re.search(r"\(\w[^;]*\(\s*[^;]*\b" + v + r"\b", text.replace("(*" + v + ")", "X"))
        if written:
            continue
        for k, ln in enumerate(body):
            if re.search(r"\(\s*\*\s*" + v + r"\s*\)\s*\[[^\]]*\]\s*=[^=]|\*\s*\(\s*\*\s*" + v + r"\b[^;]*=[^=]", ln):
                hits.append(("STORE", s + k + 1, name, "store through never-written pointer frame slot '%s'" % v))
                break
    # --- LEAK: copy_to_user from never-written frame slots / locals ---
    zero_frame = re.search(r"__frame\s*\[[^\]]*\]\s*=\s*\{\s*0\s*\}", text) is not None
    for k, ln in enumerate(body):
        for m in re.finditer(COPY_TO + r"\s*\)?\s*\(([^;]*)\)\s*;", ln):
            argv = [a.strip() for a in re.split(r",(?![^()]*\))", m.group(1))]
            if len(argv) < 2:
                continue
            srcarg = re.sub(r"^\([^()]*\)\s*", "", argv[1])
            mv = re.match(r"^&?\s*(__p_\w+|\w+)$", srcarg)
            if not mv:
                continue
            v = mv.group(1)
            if v in ("arg1", "arg2", "arg3", "arg4", "a0", "a1", "a2", "a3") or re.match(r"^(s|v|t|a)\d+(_\w+)?$", v):
                continue
            # is v (or the slot it aliases) written anywhere else in the function?
            others = "\n".join(l for l in text.replace(m.group(0), "").split("\n")
                                if not re.search(r"\bconst\s+" + v + r"\s*=", l))
            w = re.search(r"(?<![\w.>])\*?\s*" + v + r"\s*(?:\[[^\]]*\])?\s*=[^=]", others) or \
                re.search(r"\(\s*\*\s*" + v + r"\s*\)\s*(?:\[[^\]]*\])?\s*=[^=]", others) or \
                re.search(r"[(,]\s*(?:\([^()]*\)\s*)*&?\s*" + v + r"\s*[,)]", others)
            if v.startswith("__p_"):
                # writes through other aliases into the same bytes are invisible here: report only when
                # no other slot alias is written by a call (memcpy/copy_from_user into the frame)
                pass
            if not w:
                kind = "ZERO" if (v.startswith("__p_") and zero_frame) else "LEAK"
                hits.append((kind, s + k + 1, name, "copy to user from never-written '%s'" % v))
    # --- ARITY: casted calls with fewer params than the prototype ---
    for k, ln in enumerate(body):
        for m in re.finditer(r"\(\s*\(\s*[\w\s\*]+\(\s*\*\s*\)\s*\(([^()]*)\)\s*\)\s*(?:\(\s*uintptr_t\s*\)\s*)?&?\s*(\w+)\s*\)\s*\(", ln):
            plist, callee = m.group(1).strip(), m.group(2)
            if callee not in proto or proto[callee] is None:
                continue
            cnt = 0 if plist in ("", "void") else plist.count(",") + 1
            if cnt < proto[callee]:
                hits.append(("ARITY", s + k + 1, name, "%s called with %d of %d arguments" % (callee, cnt, proto[callee])))

# ---- ioctl reachability: call depth from the T40 control/tuning ioctl handlers ----
ROOTS = ["tx_isp_core_ops_s_ctrl", "tx_isp_core_ops_g_ctrl", "tisp_code_tuning_ioctl", "tx_isp_unlocked_ioctl",
         "isp_core_tunning_unlocked_ioctl", "regtrace_tx_isp_ioctl", "regtrace_framechan_ioctl", "regtrace_tisp_ioctl"]
fnames = {}
for fn, s0, e0 in funcs:
    fnames.setdefault(fn, []).append((s0, e0))
tok = re.compile(r"\b([A-Za-z_]\w*)\b")
edges = {}
for fn, s0, e0 in funcs:
    refs = set(t for t in tok.findall("\n".join(lines[s0:e0 + 1])) if t in fnames and t != fn)
    edges.setdefault(fn, set()).update(refs)
depth_of = {}
front = [r for r in ROOTS if r in fnames]
for r in front:
    depth_of[r] = 0
d = 0
while front and d < 4:
    d += 1
    nxt = []
    for f in front:
        for g in edges.get(f, ()):
            if g not in depth_of:
                depth_of[g] = d
                nxt.append(g)
    front = nxt

seen = set()
for kind, line, fn, msg in sorted(hits, key=lambda h: (h[1], h[0])):
    if (kind, line) in seen:
        continue
    seen.add((kind, line))
    r = ("ioctl+%d" % depth_of[fn]) if fn in depth_of else "-"
    if "--ioctl" in sys.argv and fn not in depth_of:
        continue
    print("%-5s %6d %-8s %s: %s" % (kind, line, r, fn, msg))
print("# %d hits in %d functions scanned (%s)" % (len(seen), len(funcs), src), file=sys.stderr)
