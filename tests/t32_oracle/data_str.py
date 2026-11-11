#!/usr/bin/env python3
# Print the NUL-terminated string of an OEM .rodata.str object of Matt Davis' recovered T32 tree (PR #15 @ 2e2e06bc)
# as a C literal, with its data.c line, for the debug-print oracles (t32_dbg_*): alias from data-aliases.lds, bytes from data.c.
#   usage: data_str.py <open-tx-isp git dir> t32_data_c3f0c [more symbols]   (one line each: "sym<TAB>c literal")
import re, subprocess, sys
g = sys.argv[1]
show = lambda p: subprocess.run(['git', '-C', g, 'show', '2e2e06bc:driver/t32/' + p], check=True, capture_output=True, text=True).stdout
al = show('data-aliases.lds')
lines = show('data.c').split('\n')
sec = {}
def words(name):
    if name not in sec:
        s = next(i for i, l in enumerate(lines) if l.startswith('union t32_word t32_section_%s[' % name))
        w = []
        for l in lines[s + 1:]:
            m = re.match(r'\s*\{ \.value = 0x([0-9a-f]+)U \},', l)
            if not m: break
            w.append(int(m.group(1), 16))
        sec[name] = w
    return sec[name]
for sym in sys.argv[2:]:
    m = re.search(r'^%s = t32_section_(\w+) \+ (0x[0-9a-f]+);' % re.escape(sym), al, re.M)
    if not m: sys.exit('no alias for ' + sym)
    w = words(m.group(1)); off = int(m.group(2), 16)
    b = b''.join(x.to_bytes(4, 'little') for x in w)[off:]
    s = b[:b.index(0)]
    lit = ''.join('\\n' if c == 10 else '\\t' if c == 9 else '\\"' if c == 34 else '\\\\' if c == 92 else chr(c) if 32 <= c < 127 else '\\x%02x' % c for c in s)
    print('%s\t"%s"' % (sym, lit))
