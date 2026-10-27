# T40 BCSH chain: day -> night -> day host check

Runs `driver/t40/tx_isp_t40_bcsh.h` (the C BCSH chain; the literal `tx_isp_t40_bcsh_lit.inc` was removed) on an x86-64 host in the order of `regtrace_t40_refresh_daynight_pipeline`
(init from the bank, brightness, contrast, hue, saturation; night with saturation 0) and checks that the BCSH registers
0x11000..0x110fc after three day -> night -> day cycles equal the first day init.

    ./run.sh sc830ai-t40.bin            # EV 286830 (dark, max gain)
    ./run.sh sc830ai-t40.bin 3375       # EV of the IR-lit night

Result 2026-10-25 (sc830ai bin of the Eufy 10-10 image): 0 words differ at both EVs. The device dump agrees: on agg46
the registers the chain writes (0x1103c..0x11070) come back bit-identical after day -> night -> day at the same EV.
The CSC tables are copies of the driver's OEM .data tables.
