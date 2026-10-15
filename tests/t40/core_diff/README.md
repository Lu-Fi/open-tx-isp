# T40 core chain: host differential test (oracle vs readable driver code)

The readable functions of `driver/t40/tx_isp_t40_recovered.c` are compiled on the host
next to a literal translation of the same OEM functions (the oracle) and both are run
with identical state and register reads; register writes, return values and state are
compared. Nothing here is built into the driver.

1. Oracle: `tools/mips2c_literal.py` (dispatch mode) on `objdump -dr` of the vendor
   `tx-isp-t40.ko` for the functions in `fnlist`, then
   `gen_oracle.py <work> or or <ko> <objdump> or/oem.c`.
2. Readable side: `mkrd.py <driver .c> fnlist funcs.c`, assembled with `shim.h`,
   `decls.txt` and `rdsym.inc` into one host unit.
3. `gcc -O0 -no-pie -fno-pie -w -std=gnu99 -Wno-error=int-conversion
   -Wno-error=incompatible-pointer-types -o diff_msca diff_msca.c or/oem.c rd_msca.c`
   (`-no-pie` keeps every address below 4 GiB, as the 32-bit code expects).

Layout note: this driver's MSCA state follows the reference vendor build (0x268 bytes
per channel block, 0x744 bytes); the Eufy module used as the oracle has 0x260 / 0x72c.
Data words of the second and third block therefore differ by design; register
addresses, counts and control flow must match.

Result 2026-10-15 (MSCA/OSD chain): 365 differences before, 4 after (layout only).
