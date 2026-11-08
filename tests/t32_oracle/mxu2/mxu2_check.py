#!/usr/bin/env python3
"""Check Matt Davis' C translation of the module's MXU2 instructions against t32_mxu2.h.

usage: mxu2_check.py <PM.md> <module.ko> <objdump> <recovered-dir-or-git:rev:path> <outdir> <symbol:recovered-file>...

Every MXU2 word of a function (decoded from the manual's tables, mxu2_decode.py) is
paired in order with the vx[] statement the recovered C has for it.  A C test is
generated per distinct pair: random vector registers and GPRs go through Matt's
statement and through the model (t32_mxu2.h) built from the decoded fields
(register numbers, immediates, formats), results compared.
"""
import collections, re, subprocess, sys, random
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from mxu2_decode import parse_pm, table, decode

REGS = ['zero', 'at', 'v0', 'v1', 'a0', 'a1', 'a2', 'a3', 't0', 't1', 't2', 't3', 't4', 't5', 't6', 't7',
        's0', 's1', 's2', 's3', 's4', 's5', 's6', 's7', 't8', 't9', 'k0', 'k1', 'gp', 'sp', 's8', 'ra']
ES = {'B': 1, 'H': 2, 'W': 4, 'D': 8}
ARITH = {'ADD': 'MX_ADD', 'SUB': 'MX_SUB', 'MUL': 'MX_MUL', 'MADD': 'MX_MADD', 'DIVU': 'MX_DIVU', 'ADDSS': 'MX_ADDSS',
         'ADDUU': 'MX_ADDUU', 'SUBSS': 'MX_SUBSS', 'SUBUU': 'MX_SUBUU', 'SUBSA': 'MX_SUBSA', 'SUBUA': 'MX_SUBUA',
         'CEQ': 'MX_CEQ', 'CLTS': 'MX_CLTS', 'CLTU': 'MX_CLTU', 'CLEU': 'MX_CLEU', 'SLL': 'MX_SLL', 'SRA': 'MX_SRA',
         'SRL': 'MX_SRL', 'SRLR': 'MX_SRLR'}
SHI = {'SLLI': 'MX_SLLI', 'SRAI': 'MX_SRAI', 'SRLI': 'MX_SRLI', 'SRARI': 'MX_SRARI'}
Z2R = {'CEQZ': 'MX_CEQZ', 'CNEZ': 'MX_CNEZ', 'CLTZ': 'MX_CLTZ'}


def fields_of(tab, w):
    for mask, match, fields, x in tab:
        if w & mask == match:
            return {k: (w >> p) & ((1 << wd) - 1) for k, (p, wd) in fields.items()}
    return {}


def model_call(name, f):
    """C statement(s) running the model for one instruction; vr[], g[] (GPR array), mem is a pointer."""
    def e(s):
        return s
    if name in ('ANDV', 'ORV'):
        return 'mx_%s(vr, %d, %d, %d);' % (name.lower(), f['vrd'], f['vrs'], f['vrt'])
    if name in ('BSELV', 'SHUFV'):
        return 'mx_%s(vr, %d, %d, %d, %d);' % (name.lower(), f['vrd'], f['vrs'], f['vrt'], f['vrr'])
    if name in ('LU1Q', 'SU1Q'):
        off = f['offset'] - (1 << 10) if f['offset'] & 0x200 else f['offset']
        off = f['offset'] | (-1 << 10) if f['offset'] & 0x200 else f['offset']
        a = 'g[%d] + (%d)' % (f['base'], off)
        return ('memcpy(&vr[%d], (void *)(uintptr_t)(%s), 16);' if name == 'LU1Q' else
                'memcpy((void *)(uintptr_t)(%s), &vr[%d], 16);') % ((f['vrd'], a) if name == 'LU1Q' else (a, f['vrd']))
    if name in ('LU1QX', 'SU1QX'):
        a = 'g[%d] + g[%d]' % (f['base'], f['index'])
        return ('memcpy(&vr[%d], (void *)(uintptr_t)(%s), 16);' if name == 'LU1QX' else
                'memcpy((void *)(uintptr_t)(%s), &vr[%d], 16);') % ((f['vrd'], a) if name == 'LU1QX' else (a, f['vrd']))
    m = re.fullmatch(r'(.*?)([BHWD])', name)
    if not m:
        return None
    base, es = m.group(1), ES[m.group(2)]
    if base in ARITH:
        return 'mx_3r(vr, %s, %d, %d, %d, %d);' % (ARITH[base], es, f['vrd'], f['vrs'], f['vrt'])
    if base in SHI:
        return 'mx_shi(vr, %s, %d, %d, %d, %d);' % (SHI[base], es, f['vrd'], f['vrs'], f['imm'])
    if base in Z2R:
        return 'mx_2rz(vr, %s, %d, %d, %d);' % (Z2R[base], es, f['vrd'], f['vrs'])
    if base == 'REPI':
        return 'mx_repi(vr, %d, %d, %d, %d);' % (es, f['vrd'], f['vrs'], f['imm'])
    if base == 'INSFMXU':
        return 'mx_insfmxu(vr, %d, %d, %d, %d);' % (es, f['vrd'], f['vrs'], f['imm'])
    if base == 'MFCPU':
        return 'mx_mfcpu(vr, %d, %d, g[%d]);' % (es, f['vrd'], f['rs'])
    if base in ('MTCPUS', 'MTCPUU'):
        return 'g[%d] = mx_mtcpu(vr, %d, %d, %d, %d);' % (f['rd'], es, f['vrs'], f['imm'], 1 if base == 'MTCPUS' else 0)
    if base == 'LI':
        return 'mx_li(vr, %d, %d, %d);' % (es, f['vrd'], f['imm'])
    return None


def stmt_of(line):
    line = re.sub(r' goto L_[0-9a-f]+;', '', line.strip())
    if '{' not in line:
        return re.sub(r'^branch = \([^;]*\); ', '', line)
    i = line.index('{')
    d = 0
    for j in range(i, len(line)):
        d += line[j] == '{'
        d -= line[j] == '}'
        if d == 0:
            return line[i:j + 1]
    return line[i:]


def main():
    pm, ko, objdump, recdir, outdir = sys.argv[1:6]
    tab = table(parse_pm(pm))
    st = subprocess.check_output([objdump, '-t', ko]).decode()
    dis = subprocess.check_output([objdump, '-d', ko]).decode().split('\n')
    pairs = collections.OrderedDict()
    total = 0
    unmodelled = collections.Counter()
    for spec in sys.argv[6:]:
        sym, rec = spec.split(':')
        m = re.search(r'^([0-9a-f]+) .* F \.text\s+([0-9a-f]+) ' + re.escape(sym) + '$', st, re.M)
        a, sz = int(m.group(1), 16), int(m.group(2), 16)
        words = []
        for l in dis:
            p = l.split('\t')
            if len(p) < 3 or not re.match(r'^\s*[0-9a-f]+:$', p[0]):
                continue
            ad = int(p[0].strip().rstrip(':'), 16)
            mn = p[2].split()[0] if p[2].strip() else ''
            if a <= ad < a + sz and (mn == 'c2' or mn.startswith('udi') or mn.startswith('.word')):
                words.append(int(p[1].strip(), 16))
        src = subprocess.check_output(['git', '-C', recdir, 'show', '2e2e06bc:driver/t32/recovered/' + rec]).decode()
        stm = [stmt_of(l) for l in src.split('\n') if 'vx[' in l and 'union t32_vec vx[32]' not in l]
        if len(words) != len(stm):
            print('%s: %d words but %d statements, skipped' % (sym, len(words), len(stm)))
            continue
        for w, s in zip(words, stm):
            total += 1
            pairs.setdefault((w, s), sym)
    out = ['#include <stdio.h>', '#include <stdlib.h>', '#include <stdint.h>', '#include <string.h>',
           '#include "t32_mxu2.h"', 'typedef int32_t s32; typedef int16_t s16; typedef int8_t s8; typedef int64_t s64; typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8; typedef uint64_t u64;',
           'union t32_vec { u8 b[16]; u16 h[8]; u32 w[4]; u64 d[2]; };',
           'static uint32_t rs = 0x1234567u; static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }',
           'static uint8_t mem1[4096] __attribute__((aligned(64))), mem2[4096] __attribute__((aligned(64)));',
           'static void rv(union t32_vec *v, int n) { int i, k; for (i = 0; i < n; i++) for (k = 0; k < 4; k++) { uint32_t x = rnd(); if (rnd() % 4 == 0) x = (rnd() & 1) ? 0 : 0xffffffffu; if (rnd() % 5 == 0) x &= 0x00ff00ffu; v[i].w[k] = x; } }',
           'static unsigned fails, tests;']
    calls = []
    skipped = collections.Counter()
    for k, ((w, s), sym) in enumerate(pairs.items()):
        d = decode(tab, w)
        name = d[0]
        f = fields_of(tab, w)
        mc = model_call(name, f)
        if mc is None:
            skipped[name] += 1
            continue
        mem = name in ('LU1Q', 'SU1Q', 'LU1QX', 'SU1QX')
        out.append('static void t%d(void)\n{' % k)
        out.append('\tunion t32_vec vx[32]; mx_v vr[32]; u32 g[32]; int i, bad = 0;')
        out.append('\tu32 ' + ', '.join(REGS[1:]) + ';')
        out.append('\tfor (i = 0; i < 20; i++) {')
        out.append('\t\trv(vx, 32); memcpy(vr, vx, sizeof(vr)); for (int q = 0; q < 32; q++) g[q] = rnd() % 4 ? rnd() : 3;')
        out.append('\t\tg[0] = 0;')
        if mem:
            # base register -> buffer, index register small
            out.append('\t\tmemset(mem1, 0, 4096); for (int q = 0; q < 4096; q++) mem1[q] = (u8)rnd(); memcpy(mem2, mem1, 4096);')
            out.append('\t\tg[%d] = (u32)(uintptr_t)(mem1 + 2048);' % f['base'])
            if 'index' in f:
                out.append('\t\tif (%d != %d) g[%d] = (u32)(rnd() %% 64);' % (f['index'], f['base'], f['index']))
        out.append('\t\t' + '; '.join('%s = g[%d]' % (REGS[q], q) for q in range(1, 32)) + ';')
        if mem:
            # the recovered code uses the real pointer values; make the model use the second buffer
            out.append('\t\tu32 ob = g[%d]; g[%d] = (u32)(uintptr_t)(mem2 + 2048);' % (f['base'], f['base']))
        out.append('\t\t' + s)
        out.append('\t\t' + mc)
        if name.startswith('MTCPU'):
            out.append('\t\tif (%s != g[%d]) bad = 1;' % (REGS[f['rd']], f['rd']))
        out.append('\t\tif (memcmp(vx, vr, sizeof(vr))) bad = 1;')
        if mem:
            out.append('\t\tif (memcmp(mem1, mem2, 4096)) bad = 1; (void)ob;')
        out.append('\t}')
        out.append('\ttests++; if (bad) { fails++; printf("MISMATCH %s: %08x %s: %s\\n", "' + sym + '", ' + str(w) + ', "' + name + '", "' + s.replace('\\', '\\\\').replace('"', '\\"')[:150] + '"); }')
        out.append('}')
        calls.append('\tt%d();' % k)
    out.append('int main(void)\n{')
    out += calls
    out.append('\tprintf("mxu2_check: %u distinct (word, statement) pairs tested, %u mismatches\\n", tests, fails);')
    out.append('\treturn fails != 0;\n}')
    open(outdir + '/mxu2_check_gen.c', 'w').write('\n'.join(out))
    print('statements %d, distinct pairs %d, tested %d, not modelled: %s' % (total, len(pairs), len(calls), dict(skipped)))


if __name__ == '__main__':
    main()
