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

`fuzz:<seed>` is a bin of random bytes: it drives fields that every real bin leaves equal
(e.g. DPC black levels 26 and 28, check modes above 4).

The literal code keeps addresses in `uint32_t`: everything lives below 4 GiB (`-no-pie`,
`MAP_32BIT` heap and worker stack).

## Results

| chain | driver code | bins | steps | writes compared | differences |
|-------|-------------|------|-------|-----------------|-------------|
| DPC | `tx_isp_t40_dpc.h` | 26 SDK 1.3.1 + 5 Eufy + 8 fuzz | 58890 | 2835126 | 0 |
