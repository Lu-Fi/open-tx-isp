# T41 night NR/sharpen host check

Runs the driver's own MDNS, SDNS, YSP and DMSC interpolation and register packing
(`driver/t41/tx_isp_t41_{mdns,sdns,ysp,dmsc}.h`) on the day or night bank of a tuning bin
at a given total gain (log2, 16.16, the value the AE hands `tisp_long_tgain_update`) and
compares the result with ISP core register dumps (`dump.regs`: one hex word per line, line n =
core offset 4n).

```
./run.sh gc5603-t41.bin 1 38519 open.regs vendor.regs   # table, '*' marks words that differ
./run.sh gc5603-t41.bin 1 scan vendor.regs              # per block: gains (step 256) that reproduce the dump
```

Bank layout (non-WDR, as `tisp_params_copy` places it): MDNS bank+0x17188 (0x61a bytes),
SDNS bank+0x1e140 (0x384), YSP bank+0x1cc74 (0xa78), DMSC bank+0x14768 (0x1076).
Day bank = bin+0x40, night bank = day + le32(bin+0x34).

Result 2026-10-25 (.32 gc5603, night, light off): the open dump equals the computed registers
at its own gain (38519) word for word (317 words). The vendor registers match
the night bank at a lower gain (MDNS/SDNS/YSP 0-3840, DMSC 34816-36608). The vendor ran
at 1x analog gain most of the time, the open stack at 1.28-1.5x. The interpolation and the
OEM rounding (bit 15 of the product) are the same as the vendor's.
