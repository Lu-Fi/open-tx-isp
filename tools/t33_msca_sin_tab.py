#!/usr/bin/env python3
"""Print the T33 MSCA polyphase kernel tables (driver/t32/tx_isp_t33_msca_curve.h).

tisp_sin on the T33 interpolates one 257-entry table per sample count
(8, 12, 16, 24 intervals: the 2-, 3-, 4- and 6-tap filters).  Ours are
computed: 1535 * sinc(x) * sinc(2x) over x = 0 .. R (R = 1, 1.5, 2, 3).
Checked against the vendor module: the 8 and 12 tables are equal, the 16
and 24 tables differ by 1 in 41 / 58 entries; the driver carries the
vendor 16 / 24 tables as transcribed data instead."""
import math

def sinc(x):
    return 1.0 if x == 0 else math.sin(math.pi * x) / (math.pi * x)

for n, r in ((8, 1.0), (12, 1.5), (16, 2.0), (24, 3.0)):
    tab = [round(1535 * sinc(k * r / 256) * sinc(2 * k * r / 256)) for k in range(257)]
    print("static const short t33_msca_tab%d[257] = {" % n)
    for i in range(0, 257, 12):
        print("\t" + " ".join("%d," % v for v in tab[i:i + 12]))
    print("};")
