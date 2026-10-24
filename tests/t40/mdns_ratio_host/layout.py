#!/usr/bin/env python3
# The 7 bank offsets of regtrace_mdns_ratio_lit must hit the same par bytes as the comb pointers that
# tisp_mdns_wdr_en_lit sets: par = bank + 0x10000 + 4188 (tisp_mdns_params_refresh), lin comb[i] = par +
# mdns_comb_lin_off[i], WDR comb[i] = par + 1518 + 11 * i.
import re, sys
src = open(sys.argv[1]).read()
lin = re.search(r'mdns_comb_lin_off\[84\] = \{([^}]*)\}', src).group(1)
lin = [int(x) for x in re.findall(r'\d+', lin)]
m = re.search(r'tbl\[7\] T40C2 = \{(.*?)\};', src, re.S).group(1)
rows = [tuple(int(x) for x in r) for r in re.findall(r'\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\s*\}', m)]
bad = 0
for l, w, c, cap in rows:
    if l - 4188 != lin[c] or w - 4188 != 1518 + 11 * c:
        print("FAIL row", l, w, c, cap, "expect", lin[c] + 4188, 1518 + 11 * c + 4188); bad += 1
print("%s: %d table rows vs comb layout" % ("FAIL" if bad or len(rows) != 7 else "PASS", len(rows)))
sys.exit(1 if bad or len(rows) != 7 else 0)
