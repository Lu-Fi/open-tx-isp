#!/usr/bin/env python3
# Print the initial words of an OEM data object of Matt Davis' recovered T32
# tree (PR #15 @ 2e2e06bc) for env.c: alias from data-aliases.lds, value from
# data.c (with its line number).  .bss objects print "bss" (zero).
#   usage: data_words.py <open-tx-isp git dir> t32_data_d6d7c <bytes>
import re, subprocess, sys
g, sym, n = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
show = lambda p: subprocess.run(['git', '-C', g, 'show', '2e2e06bc:driver/t32/' + p],
                                check=True, capture_output=True, text=True).stdout
m = re.search(r'^%s = t32_section_(\w+) \+ (0x[0-9a-f]+);' % re.escape(sym), show('data-aliases.lds'), re.M)
if not m:
    sys.exit('no alias for ' + sym)
sec, off = m.group(1), int(m.group(2), 16)
if sec == '_bss':
    print('%s: .bss +0x%x, zero at load' % (sym, off)); sys.exit()
lines = show('data.c').split('\n')
s = next(i for i, l in enumerate(lines) if l.startswith('union t32_word t32_section_%s[' % sec))
for k in range(off // 4, (off + n + 3) // 4):
    print('data.c:%d +0x%x %s' % (s + 2 + k, 4 * k, lines[s + 1 + k].strip()))
