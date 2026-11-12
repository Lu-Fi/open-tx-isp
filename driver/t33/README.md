# Open T33 (PRJ008) TX-ISP driver

Planned, not a release gate. Step 1 of the T33 plan (issue Lu-Fi/openimp#383).

The T33 shares the T32 ioctl ABI, register map and interrupt layout, so this
directory builds `tx-isp-t33.ko` from the `driver/t32` sources with
`-DTX_ISP_T33`. Each `tx_isp_t33_*.c` only includes its `../t32` counterpart;
the per-SoC constants are in `../t32/tx_isp_t32_soc.h`:

| Item | T32 (PRJ007) | T33 (PRJ008) |
|---|---|---|
| DRIVER_VERSION / DEFAULT_BIN SoC string | PRJ007 | PRJ008 |
| DEFAULT_BIN buffer (`IMPISPBinInfo`) size / version / soc offset | 880 / 863 / 871 | 856 / 839 / 847 (no `paddr[6]` in the 2.0.2.1 header) |
| ISP / LDC / VPU clock | 219 / 588 / 500 MHz | 340 / 357 / 340 MHz |

Evidence: the ioctl numbers and payload sizes the T32 driver serves are all
issued by the T33 2.0.2.1 libimp with the same values; 279 public typedefs of
the T32 2.2.0 and T33 2.0.2.1 headers have the same size except
`IMPISPBinInfo`, `tisp_bin_attr_t` and `IMPEncoderKernEncOut`.

Not verified yet (next steps): the DRIVER_VERSION version string the T33
libimp expects, the T33 platform clock table, the IQ bin section sizes and
the register-list (RCFG) programming of the T33 ISP blocks. Nothing has run
in QEMU or on a camera.

Build (3.10.14 kernel tree):

    make -C $KDIR M=$PWD/driver/t33 ARCH=mips CROSS_COMPILE=mipsel-linux- modules

A T33 camera kernel uses `CONFIG_CPU_MIPS32_R2` (vermagic `MIPS32_R2`); build
against that tree for a device. The T32 module built from these sources is
unchanged (identical `.text` and strings).
