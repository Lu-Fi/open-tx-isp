# T40 C chains: host check against the literal OEM chains

The T40 ISP blocks that were brought up as literal mips2c translations of the OEM module
(`driver/t40/tx_isp_t40_*_lit.inc`) are being rebuilt as readable C. This harness compiles the
literal chain (taken from git at `LIT_REF`, default `6d63667b`, the last `next` commit that still
built it into the driver) and the new driver code into one host program, runs the same steps on
both and compares:

- every `system_reg_write` (address, value, order),
- the chain state (interpolated values, remembered gain/EV).

Steps per bin: init on the day and on the night bank, fixed gains (0, sub-threshold moves,
node values, the last node, beyond it, negative/huge values), 6 x 120 random gain walks with
small moves around the refresh threshold and jumps, a day/night bank switch after each walk,
and the WDR table set where the chain has one.

    ./run.sh dpc <sensor-t40.bin>... [fuzz:<seed>...]
    ./run.sh lce <sensor-t40.bin>... [fuzz:<seed>...]
    ./run.sh sdns <sensor-t40.bin>... [fuzz:<seed>...]
    ./run.sh defog <sensor-t40.bin>... [fuzz:<seed>...]
    ./run.sh adr <sensor-t40.bin>... [fuzz:<seed>...]
    ./run.sh cdns <sensor-t40.bin>... [fuzz:<seed>...]
    ./run.sh bcsh <sensor-t40.bin>... [fuzz:<seed>...]

LCE runs whole frames: init at six resolutions (3840x2160 ... 640x360, the last one below the
OEM minimum cell area, so init must fail on both sides), 160 frames each through the statistics
IRQ (ping, pong, both, none) and the event, with synthetic statistics read from the RAM ports
(realistic cells whose counts sum to half the cell area, flat, random up to the area, zero and
full 32-bit garbage), total gain changes, bank switches, WDR light lock on/off with changing
`wdr_lce_para`, session cleanup. Before the frames, 20000 random calls of every helper
(smoothing, head/tail, shaping, CDF, converge, light correction, light lock, normalisation,
distance) compare the helpers alone.

Defog runs whole frames as well (literal chain from `LIT_REF_DEFOG`, default `1bc9b8e2`): per bin six
parameter variants (the bin as is; per-channel curve + motion weighting + output smoothing; one curve
for all channels; the simple luma mode; manual levels with zoomed position weights; bank weights for
1920x1080), each with init at six resolutions (bank weights or count_weight), 70 frames of statistics
through IRQ 10 (the four DMA slots and a foreign address; garbage, realistic, dark and saturated 21-bit
sums) and event 4, EV walks (small steps, jumps, exactly on the knots, huge values), IMP strength
changes (0..255, enable off/on), day/night refreshes, the WDR table switch and session cleanups.
Compared: every register write and the parameter block, soft state and register image. The comparison
runs with the three better-than-OEM fixes off (`t40_defog_dn_cache_reset`, `t40_defog_trans_reset_all`,
`t40_defog_motion_var` = 0); a separate C-only test turns them on and checks that no cached curve or
transmission value is stale after a day/night switch and after an EV change, and that the motion weights
equal an independently computed 5x5 window variance (with the fixes off the same test must find the
OEM flaws).

ADR (literal from `LIT_REF_ADR`, default `7471ad73`): 200000 random calls of every arithmetic and
table helper first (bit-serial divisions, split multiply, exp2, table interpolation, resampling,
knee search, Gaussian weights, curve shaping, curve smoothing), then per bin six parameter variants
(local light modes 0..3, curve selections 0..4 with mixing weights, the three curve filters, EV
overrides, rate limit and fade, envelope, luma gains, tone curve, Gaussian weighting, debug readout)
at four resolutions with frames through IRQ 9 (four DMA slots and a foreign address; garbage,
realistic, empty and saturated statistics) and event 2, EV and total gain walks, DRC strength 0..255,
day/night refreshes, session cleanups. The module-lifetime words (frame counter, `adr_kneepoint_x`,
Gaussian weights, cut points, change flags) are compared as well.

CDNS (literal from `LIT_REF_CDNS`, default `e96b92ea`): both channels, linear and WDR half, init, fixed EVs,
8 x 120 random steps per mode (EV walks with 1/256, 1.0 and random thresholds, the tgain refresh, day/night
refresh, tuning set with a random or bank section, WDR switch without init), `cdns_remove` and re-init.
Compared: register writes, interpolated values, section copy, selected field offsets, remembered gain, WDR flag,
threshold and counters.

BCSH (literal from `LIT_REF_BCSH`, default `e96b92ea`; the CSC tables come from the driver at the same commit):
the driver flows per bin (init in every CSC mode and with WDR + user CCM, AE EV follow, CT update, the four user
controls, CSC switch, day/night re-init, the vendor-AWB EV/CT follow at and around its thresholds), then 3000 calls
on random inter/outer images (unordered knots, EV/CT on the knots, brightness at its clamp, contrast curve off).
Compared: register writes, both OEM byte images and the OEM pointer table (field by field).

`run.sh` instruments the literal code: a division by zero (the MIPS result is unpredictable)
or an LCE histogram knot past bin 31 (the OEM writes past its stack array) counts as undefined
OEM behaviour and stops that bin; the C code has a defined result there. Only garbage statistics
reach those inputs (for example a first bin heavier than the whole histogram).

`fuzz:<seed>` is a bin of random bytes: it drives fields that every real bin leaves equal
(e.g. DPC black levels 26 and 28, check modes above 4).

The literal code keeps addresses in `uint32_t`: everything lives below 4 GiB (`-no-pie`,
`MAP_32BIT` heap and worker stack).

## Results

| chain | driver code | bins | steps | writes compared | differences |
|-------|-------------|------|-------|-----------------|-------------|
| DPC | `tx_isp_t40_dpc.h` | 26 SDK 1.3.1 + 5 Eufy + 8 fuzz | 58890 | 2835126 | 0 |
| SDNS | `tx_isp_t40_sdns.h` | 26 SDK 1.3.1 + 5 Eufy + 8 fuzz | 48009 (init, EV walks with 1/256 and 1.0 thresholds, re-init on bank switch) | 844137 | 0 |
| BCSH | `tx_isp_t40_bcsh.h` | 26 SDK 1.3.1 + 5 Eufy + 8 fuzz | 210834 | 3502575 | 0 |
| CDNS | `tx_isp_t40_cdns.h` | 26 SDK 1.3.1 + 5 Eufy + 8 fuzz | 155220 | 1726959 | 0 |
| LCE | `tx_isp_t40_lce.h` | 26 SDK 1.3.1 + 5 Eufy + 8 fuzz | 46309 + 2 x 10 x 20000 helper calls | 17155278 | 0 (1 fuzz bin stopped at undefined OEM behaviour) |

Mutation check (a deliberate one-character change in the C code must be detected): DPC black
level / threshold compare; LCE converge rounding, WDR lock step, light-correction ratio and
limit, head/tail compare, lock strength, filter remainder and excess, normalisation quirk,
peak/median, reset stepping, `wdr_lce_para` use: all detected.
