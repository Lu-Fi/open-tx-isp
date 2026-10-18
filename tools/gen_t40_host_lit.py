#!/usr/bin/env python3
# genhost.py <workdir> <subdir> <prefix> <oem.ko> <objdump> <out.c> : host (x86-64 -no-pie) build of a literal OEM chain
import sys, re, subprocess
W, SUB, PFX, KO, OD, OUT = sys.argv[1:7]
fns = open(f"{W}/{SUB}/fnlist").read().split()
syms = {}
for l in open(f"{W}/prod.sym"):
    p = l.split()
    if len(p) >= 6 and p[2] == 'O' and p[3] in ('.bss', '.data', '.rodata'):
        syms[p[5]] = (p[3], int(p[0], 16), int(p[4], 16))
SHARED = {"tparamsN", "tispinfo"}
bodies = {}; used = set()
for f in fns:
    b = '\n'.join(l for l in open(f"{W}/{SUB}/{f}.c").read().split('\n') if not l.startswith('/*'))
    used |= set(re.findall(r'&([A-Za-z_$.][A-Za-z_0-9$.]*)', b)); bodies[f] = b
priv = sorted(s for s in used if s in syms and s not in SHARED)
def secbytes(sec):
    out = subprocess.run([OD, '-s', '-j', sec, KO], capture_output=True, text=True).stdout
    data = {}
    for l in out.split('\n'):
        m = re.match(r'\s*([0-9a-f]+) ((?:[0-9a-f]{2,8} ){1,4})', l)
        if not m: continue
        a = int(m.group(1), 16); hx = ''.join(m.group(2).split())
        for i in range(0, len(hx), 2): data[a + i // 2] = int(hx[i:i+2], 16)
    return data
secs = {s: secbytes(s) for s in ('.data', '.rodata')}
ext = sorted(s for s in used if s not in syms and s not in fns and not s.startswith('$') and not s.startswith('.'))
o = ['#include <stdint.h>\n#include <string.h>\n#include <stdio.h>\n#include "hostshim.h"',
     'extern uint32_t tparamsN[2];', 'extern uint8_t tispinfo[104];']
for s in priv:
    sec, a, z = syms[s]
    if sec == '.bss':
        o.append(f"static uint8_t {PFX}_{s}[{max(z,1)}] __attribute__((aligned(8)));")
    else:
        b = secs[sec]; vals = ', '.join(f"0x{b.get(a+i,0):02x}" for i in range(z))
        o.append(f"static uint8_t {PFX}_{s}[{max(z,1)}] __attribute__((aligned(8))) = {{ {vals} }};")
o.append(f"static const char {PFX}_str[] = \"lc\";")
anon = sorted({int(m, 16) for b in bodies.values() for m in re.findall(r'/\*UNMAPPED \.rodata\+0x([0-9a-f]+)\*/0', b)})
ro_starts = sorted(a for (sec, a, z) in syms.values() if sec == '.rodata')
for i, a in enumerate(anon):
    nxt = [x for x in ro_starts if x > a] + ([anon[i+1]] if i + 1 < len(anon) else [])
    z = min(min(nxt) - a if nxt else 256, 1024)
    vals = ', '.join(f"0x{secs['.rodata'].get(a+k,0):02x}" for k in range(z))
    o.append(f"static const uint8_t {PFX}_ro_{a:x}[{z}] __attribute__((aligned(8))) = {{ {vals} }};")
# host anchors for function addresses (OEM names, distinct addresses)
for f in fns + ext:
    o.append(f"void {PFX}_anchor_{f}(void) {{ __asm__ volatile(\"\"); }}")
for f in fns:
    o.append(f"static int32_t {f}_lit(uint32_t a0_in, uint32_t a1_in, uint32_t a2_in, uint32_t a3_in, uint32_t sp_in, uint32_t *v1_io, uint32_t v0_in);")
o.append(f"static uint32_t {PFX}_dispatch(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t sp, uint32_t *v1, uint32_t v0);")
o.append(f"#define ADR_DISPATCH(r) {PFX}_dispatch((r), r_a0, r_a1, r_a2, r_a3, r_sp, &r_v1, r_v0)")
def fix(b):
    b = re.sub(r'&\$LC\d+', f'{PFX}_str', b)
    b = b.replace('&.sbss', f'{PFX}_str')
    b = re.sub(r'/\*UNMAPPED \.rodata\+0x([0-9a-f]+)\*/0', lambda m: f'{PFX}_ro_{m.group(1)}', b)
    def rp(m):
        n = m.group(1)
        if n in priv: return f'{PFX}_{n}'
        if n in fns or n in ext: return f'&{PFX}_anchor_{n}'
        return m.group(0)
    return re.sub(r'&([A-Za-z_][A-Za-z_0-9]*)', rp, b)
for f in fns:
    o.append(f'''static int32_t {f}_lit(uint32_t a0_in, uint32_t a1_in, uint32_t a2_in, uint32_t a3_in, uint32_t sp_in, uint32_t *v1_io, uint32_t v0_in)
{{
    uint32_t r_v0 = v0_in, r_v1 = v1_io ? *v1_io : 0, r_t0 = 0, r_t1 = 0, r_t2 = 0, r_t3 = 0, r_t4 = 0, r_t5 = 0;
    uint32_t r_t6 = 0, r_t7 = 0, r_t8 = 0, r_t9 = 0, r_s0 = 0, r_s1 = 0, r_s2 = 0, r_s3 = 0;
    uint32_t r_s4 = 0, r_s5 = 0, r_s6 = 0, r_s7 = 0, r_s8 = 0, r_ra = 0, r_at = 0;
    uint32_t mips_lo = 0, mips_hi = 0, mips_cond = 0;
    uint32_t r_a0 = a0_in, r_a1 = a1_in, r_a2 = a2_in, r_a3 = a3_in, r_sp = sp_in;
    (void)r_at; (void)r_ra; (void)r_t8; (void)r_t9; (void)mips_lo; (void)mips_hi; (void)mips_cond;
{fix(bodies[f])}
fn_exit:
    if (v1_io) *v1_io = r_v1;
    return (int32_t)r_v0;
}}''')
d = [f'static uint32_t {PFX}_dispatch(uint32_t fn, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t sp, uint32_t *v1, uint32_t v0)\n{{']
for f in fns:
    d.append(f"    if (fn == A32(&{PFX}_anchor_{f})) return (uint32_t){f}_lit(a0, a1, a2, a3, sp, v1, v0);")
for f in ext:
    d.append(f"    if (fn == A32(&{PFX}_anchor_{f})) return host_ext(\"{f}\", a0, a1, a2, a3, sp, v1);")
d.append('    host_bad_call(fn); return 0;\n}')
o.append('\n'.join(d))
# public entry points
for f in fns:
    o.append(f"uint32_t {PFX}_call_{f}(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t sp) {{ return (uint32_t){f}_lit(a0, a1, a2, a3, sp, NULL, 0); }}")
o.append(f"uint8_t *{PFX}_sym(const char *n) {{")
for s in priv: o.append(f"    if (!strcmp(n, \"{s}\")) return {PFX}_{s};")
o.append("    return NULL;\n}")
open(OUT, 'w').write('\n'.join(o) + '\n')
print("priv", priv, "ext", ext)
