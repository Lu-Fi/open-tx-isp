# Open T33 (PRJ008) TX-ISP driver

Planned, not a release gate. Step 1 of the T33 plan (issue Lu-Fi/openimp#383).

The T33 shares the T32 ioctl ABI, register map and interrupt layout, so this
directory builds `tx-isp-t33.ko` from the `driver/t32` sources with
`-DTX_ISP_T33`. Each `tx_isp_t33_*.c` only includes its `../t32` counterpart;
the per-SoC constants are in `../t32/tx_isp_t32_soc.h`:

| Item | T32 (PRJ007) | T33 (PRJ008) |
|---|---|---|
| DRIVER_VERSION / DEFAULT_BIN SoC string | PRJ007 | PRJ008 |
| DEFAULT_BIN buffer (`IMPISPBinInfo`) size / version / soc offset | 880 / 863 / 871 | 880 / 863 / 871 (what the camera libimp 2.0.2.1 reads; the header suggests 856) |
| Clocks | div_isp, div_isps, div_ldc, div_ispa, div_vpu + mux_* parents | no div_isps, no mux_*; parents on the dividers, gate_ldc / gate_vpu |
| Sensor subdev offsets (kernel) | 4.4.94: notify 0x98 | 3.10.14: notify 0x94 (miscdevice 4 bytes shorter) |
| ISP / LDC / VPU clock | 219 / 588 / 500 MHz | 340 / 357 / 340 MHz |
| Block programming | direct writes, one stream-on list (LSC) | one register list per block, started at stream-on |
| Interrupts | masked at probe | no ISP access before the first open; list-done word 0x300c0 used |

Both modules accept the vendor glue parameters (`clk_name`, `isp_clk`, ...,
`direct_mode`, `ivdc_*`). The T33 module implements IVDC direct mode
(`direct_mode=1`, as the T33L vendor glue loads it: `ivdc_mem_line=1080
ivdc_threshold_line=800`): `../t32/tx_isp_t32_ivdcdrv.c` with the ported
IVDC code (openimp #349), built with `TX_ISP_IVDC_DIRECT`; the T32 module
keeps the buffered path until #349 is merged.

Evidence: the ioctl numbers and payload sizes the T32 driver serves are all
issued by the T33 2.0.2.1 libimp with the same values; 279 public typedefs of
the T32 2.2.0 and T33 2.0.2.1 headers have the same size except
`IMPISPBinInfo`, `tisp_bin_attr_t` and `IMPEncoderKernEncOut`.

QEMU (`-M ingenic-t33`, t33l, cv2005, vendor T33 kernel; openimp #384): the
vendor libimp 2.0.2.1 runs ISP open, sensor, FrameSource frames and the full
teardown on this module; DRIVER_VERSION `PRJ008` / `20251107a` and the clocks
match the vendor module. Register lists (openimp #388): the T33 programs its
blocks through one register list per block (`../t32/tx_isp_t32_rcfg33.h`);
done so far: top (list 33), CSC (13), CDNS (21), CSCCR (22), and in the
fixed module layouts of `../t32/tx_isp_t33_blk.h` BLC (4), YDNS (17) and SDNS
(20), each equal to the vendor module's list word for word in QEMU. The PRJ008 IQ bin walk uses
the T33 payload rules; T32 block code runs only on sections of the T32 size.
Later (openimp #388/#389, QEMU with `-icount`): all block lists, AE/AWB and
the per-frame list refresh equal the vendor's per frame, including the rerun
counts. IVDC direct mode: the vendor libimp 2.0.2.1 encodes H.264 from the
IVDC (DQBUF frame type 3), IVDC register sequence equal to the vendor
module's except the DDR ring address. FrameSource input crop (vendor
format words 13..17) reaches the MSCA list 23 as the vendor's. rmmod writes no ISP register while the
ISP clock is off (as the vendor module). Not tested on a camera.

Build (3.10.14 kernel tree):

    make -C $KDIR M=$PWD/driver/t33 ARCH=mips CROSS_COMPILE=mipsel-linux- modules

A T33 camera kernel uses `CONFIG_CPU_MIPS32_R2` (vermagic `MIPS32_R2`); build
against a 3.10.14 tree configured like the camera kernel (MIPS32_R2,
preempt, `__isvp_goat_1.0__`), which gives the camera's vermagic. The T32 module built from these sources is
unchanged (identical `.text` and strings).
