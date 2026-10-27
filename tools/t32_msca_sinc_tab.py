#!/usr/bin/env python3
"""Print the T32 MSCA polyphase kernel table (driver/t32/tx_isp_t32_msca_curve.h).

257 samples of 1535 * sinc(x) * sinc(x / 2) over x = 0 .. 6 (step 6/256): a
Lanczos-2 shaped kernel that is not cut off at x = 2.  The vendor firmware
interpolates a table of this shape (tisp_sin); ours is computed here and can
differ from the vendor's by 1 in some entries."""
import math

def sinc(x):
    return 1.0 if x == 0 else math.sin(math.pi * x) / (math.pi * x)

tab = [round(1535 * sinc(k * 6 / 256) * sinc(k * 3 / 256)) for k in range(257)]
for i in range(0, 257, 12):
    print("\t" + " ".join("%d," % v for v in tab[i:i + 12]))
