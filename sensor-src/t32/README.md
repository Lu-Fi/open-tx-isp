# gc4653 T32 (PRJ007) sensor port

`gc4653.c` is the T32 port of the gc4653 sensor driver (module `sensor_gc4653_t32.ko`, board
`prj007_t32nq_gc4653_eth`). Build out of tree against the T32 kernel with
`-DSENSOR_T32_PORT -DSENSOR_PROC_OWNED_BY_ISP` (see `Kbuild`, `K200` = the PRJ007 sensor header dir).

Vendor-equal since openimp #923 / #924 (Tassadar SDK PRJ007 gc4653.c, identical in 2.0.0 and 2.1.0):
- stream-on / stream-off register lists (:449 / :463) and state back to INIT at stream-off (:986), so the
  MIPI output stops at stream-off and starts again with the next enable (#923).
- integration time `vts - 8`, minimum 2 in linear mode (:1166, :736); analog gain table up to 75.83x
  (:102-131), max again 409249, no sensor digital gain (#924).

Not changed (needs a device proof, night A/B): the T40-derived timing (MCLK 27 MHz, MIPI 648 Mbit/s,
25 fps, HTS/VTS 3000 x 1920 units, 0x0326/0x0336 = 0x60, 0x02c3 = 0x40, extra 0x0277/0x0276/0x0059
writes). The vendor uses MCLK 24 MHz, MIPI 864 Mbit/s, 30 fps, 1600 x 1500, 0x6c, 0x20; both give the
same pixel rate and the port works on the device. Guard: `tests/check_t32_gc4653_port.sh`.
