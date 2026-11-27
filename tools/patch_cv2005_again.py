#!/usr/bin/env python3
"""Fix the again table order of the vendor sensor_cv2005_t33.ko (openimp #925).

The vendor module (version H20260509c, md5 6351e2d9fa60983b17c697e5696ff54c) has the codes of
cv2005_again_lut entries 13..15 out of order: {0x0f, 4882}, {0x0d, 5330}, {0x0e, 5687}.  Tassadar SDK 2.1.0
PRJ008 cv2005.c:109-111 fixed it to {0x0d, 4882}, {0x0e, 5330}, {0x0f, 5687}.  Entries are 8 bytes
{u16 code, u16 pad, u32 gain}; the three code bytes are patched in place.

usage: patch_cv2005_again.py IN.ko OUT.ko     (refuses any input that is not the known vendor ko or the fixed one)
"""
import hashlib, struct, sys

VENDOR_MD5 = "6351e2d9fa60983b17c697e5696ff54c"
OFF = 0x2068
OLD = [(0x0f, 4882), (0x0d, 5330), (0x0e, 5687)]
NEW = [(0x0d, 4882), (0x0e, 5330), (0x0f, 5687)]


def entries(d):
    return [struct.unpack_from("<HxxI", d, OFF + 8 * i) for i in range(3)]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d = bytearray(open(sys.argv[1], "rb").read())
    cur = entries(d)
    if cur == NEW:
        print("already fixed")
    elif cur == OLD and hashlib.md5(d).hexdigest() == VENDOR_MD5:
        for i, (c, _) in enumerate(NEW):
            struct.pack_into("<H", d, OFF + 8 * i, c)
    else:
        sys.exit("unknown ko (table %s, md5 %s)" % (cur, hashlib.md5(d).hexdigest()))
    assert entries(d) == NEW
    open(sys.argv[2], "wb").write(d)
    print("wrote %s md5 %s" % (sys.argv[2], hashlib.md5(d).hexdigest()))


main()
