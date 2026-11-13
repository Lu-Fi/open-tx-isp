#!/usr/bin/env python3
# openimp #280: every table of driver/t32/tx_isp_t32_tmotab.h must equal the vendor
# words of Matt Davis' recovered T32 tree (PR #15 @ 2e2e06bc, driver/t32/data.c, offset
# from data-aliases.lds).  The header is compiled on the host and dumped, so the check
# sees the values the driver gets.  Also every table must have the size listed below and
# the env helper must hand out exactly these tables.
#   usage: tmotab_check.py <open-tx-isp git dir> [header]
import os, re, subprocess, sys, tempfile

g = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
hdr = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '../../driver/t32/tx_isp_t32_tmotab.h'))
show = lambda p: subprocess.run(['git', '-C', g, 'show', '2e2e06bc:driver/t32/' + p],
                                check=True, capture_output=True, text=True).stdout
aliases, lines = show('data-aliases.lds'), show('data.c').split('\n')

# vendor symbol, element type, element count (what the port reads; d88a4 .. d88f4 the whole adjacent block)
TABS = [('c0444', 's16', 33), ('c0488', 's16', 13), ('c04a4', 's16', 9), ('d8120', 'int', 128), ('d8320', 'int', 1),
        ('d8324', 'int', 256), ('d87a0', 's16', 129), ('d88a4', 's16', 24), ('d88d4', 's16', 16), ('d88f4', 's16', 16),
        ('d8914', 's16', 17)]
# the tables of the init / interrupt chain (tx_isp_t32_tmoblk.h): not in the fpga env
TABS2 = [('c04b8', 's16', 7), ('c029c', 'u16', 201), ('d89a0', 'u16', 201), ('c0544', 'u16', 201), ('c0430', 'u16', 10)]


def vendor(sym, typ, n):
    m = re.search(r'^t32_data_%s = t32_section_(\w+) \+ (0x[0-9a-f]+);' % sym, aliases, re.M)
    if not m:
        sys.exit('no alias for t32_data_' + sym)
    sec, off = m.group(1), int(m.group(2), 16)
    if sec == '_bss':
        sys.exit('t32_data_%s is .bss' % sym)
    s = next(i for i, l in enumerate(lines) if l.startswith('union t32_word t32_section_%s[' % sec))
    sz = 4 if typ == 'int' else 2
    raw = b''
    for k in range(off // 4, (off + n * sz + 3) // 4):
        w = re.search(r'\.value = 0x([0-9a-f]{8})U', lines[s + 1 + k])
        if not w:
            sys.exit('t32_data_%s: data.c:%d is not a plain value: %s' % (sym, s + 2 + k, lines[s + 1 + k].strip()))
        raw += int(w.group(1), 16).to_bytes(4, 'little')
    raw = raw[off % 4:off % 4 + n * sz]
    return [int.from_bytes(raw[i:i + sz], 'little', signed=typ != 'u16') for i in range(0, len(raw), sz)]


def name(sym):
    return 'T32_TMO_TAB_D8320' if sym == 'd8320' else 't32_tmo_tab_' + sym


src = ['#include <stdio.h>', '#include <string.h>', '#include <stdint.h>',
       'typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int16_t s16; typedef int32_t s32;',
       'typedef uint64_t u64; typedef int64_t s64;', '#include "tx_isp_t32_tmo.h"', '#include "%s"' % hdr, 'int main(void)', '{', '\tunsigned i;',
       '\tstruct t32_tmo_fpga_env e; s16 d4[16];', '\tmemset(&e, 0, sizeof(e));', '\tt32_tmo_fpga_env_tables(&e, d4);',
       '\tmemset(d4, 0x5a, sizeof(d4));', '\tt32_tmo_fpga_d88d4_init(d4);']
for sym, typ, n in TABS + TABS2:
    if sym == 'd8320':
        src.append('\tprintf("d8320 0 %%d\\n", (int)%s);' % name(sym))
        src.append('\tprintf("env d8320 %d\\n", e.d8320 == T32_TMO_TAB_D8320);')
        continue
    src.append('\tprintf("size %s %%u\\n", (unsigned)(sizeof(%s) / sizeof(%s[0])));' % (sym, name(sym), name(sym)))
    src.append('\tfor (i = 0; i < sizeof(%s) / sizeof(%s[0]); i++) printf("%s %%u %%d\\n", i, (int)%s[i]);'
               % (name(sym), name(sym), sym, name(sym)))
    if (sym, typ, n) in TABS2:
        continue
    if sym == 'd88d4':
        src.append('\tprintf("env d88d4 %d\\n", e.d88d4 == d4 && !memcmp(d4, t32_tmo_tab_d88d4, sizeof(d4)));')
    else:
        src.append('\tprintf("env %s %%d\\n", (const void *)e.%s == (const void *)%s);' % (sym, sym, name(sym)))
src += ['\treturn 0;', '}', '']

with tempfile.TemporaryDirectory() as d:
    c, exe = os.path.join(d, 'dump.c'), os.path.join(d, 'dump')
    open(c, 'w').write('\n'.join(src))
    subprocess.run([os.environ.get('HOSTCC', 'cc'), '-O0', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-I', os.path.dirname(hdr), '-I', os.path.join(here, '../../driver/t32'), '-I', here, c, '-o', exe], check=True)
    out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout.split('\n')

got, bad = {}, []
for l in out:
    f = l.split()
    if len(f) == 3 and f[0] == 'size':
        got.setdefault(f[1], {})['size'] = int(f[2])
    elif len(f) == 3 and f[0] == 'env':
        if f[2] != '1':
            bad.append('t32_tmo_fpga_env_tables: env.%s is not the table' % f[1])
    elif len(f) == 3:
        got.setdefault(f[0], {})[int(f[1])] = int(f[2])
nval = 0
for sym, typ, n in TABS + TABS2:
    v = vendor(sym, typ, n)
    h = got.get(sym, {})
    if sym != 'd8320' and h.get('size') != n:
        bad.append('%s: %s entries, expected %d' % (name(sym), h.get('size'), n))
    for i in range(n):
        nval += 1
        if h.get(i) != v[i]:
            bad.append('%s[%d] = %s, data.c t32_data_%s: %d' % (name(sym), i, h.get(i), sym, v[i]))
if bad:
    print('tmotab_check: FAIL, %d differences, first: %s' % (len(bad), bad[0]))
    sys.exit(1)
print('tmotab_check: PASS, %d values of %d tables equal data.c @ 2e2e06bc' % (nval, len(TABS + TABS2)))
