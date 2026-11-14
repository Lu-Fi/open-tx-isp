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
IVDC code (openimp #349), shared with the T32 module.

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

RAW snapshot and LDC (openimp #491, `../t32/tx_isp_t32_snapldc.c`): `0xc004548e` / `0xc004548f`
(raw WDMA, MSCA channel stop / restart), the online LDC requests `0xc00454b9..bb` and the
`/dev/offline_ldc` misc device (LUT memory, one job on the ISP LDC at 0x27000, IRQ `isp-m1`) answer as
the vendor module in QEMU (apitest t33f1, t33l1, t33drv). Online LDC at ISP init (`../t32/tx_isp_t33_ldc.h`): after LDC mode online the module programs list 25, the IPC LDC memory and the LDC/WDMA/RDMA addresses as the vendor (QEMU: the 50 vendor writes equal).

Tuning controls (`/dev/isp-m0` 0xc0105467, openimp #389): the T32 dispatchers
(`../t32/tx_isp_t32_txsctrl.h:297`, `txgctrl.h:177`, glue
`tx_isp_t32_tunctl.c` / `tx_isp_t32_tunblk.c`) are built into this module.
The vendor T33 dispatcher (`isp_core_tunning_default_ioctl` 0x1aea8,
`tx_isp_core_ops_s_ctrl` 0x18170, `tx_isp_core_ops_g_ctrl` 0x19dfc of the
vendor module) serves the T32 id set minus 0x9b (BCSH RGB offset: no
`tisp_s/g_coefft_wb` call). The AE / AWB / statistics ids run on the live T33
AE / AWB blocks (`tx_isp_t33_aec.c`, `tx_isp_t33_awbc.c`: the same T32 block
layout; an AWB start gain goes to list 7). The AE 5-bin histogram bounds of a
new AE context come from the statistics config (vendor
`tisp_ae_hist_last + 0x414`). Mask / draw / OSD ids (0x74, 0xa0..0xa2) are
stored and read back as the vendor's, not drawn (no MSCA OSD path yet); AF ids
answer the stored weight / zeros (no AF block). QEMU apitest with the OpenIMP
libimp: 255 PASS / 16 FAIL on this module, 254 / 17 on the vendor module, no
FAIL of this module that the vendor module passes. With the vendor libimp
2.0.2.1 four /dev/tx-isp commands are still missing (G_REGISTER, frame drop
set / get, internal channel attribute; openimp #429).

LCE / LSF per frame (openimp #444, `../t32/tx_isp_t33_lcepf.h`, glue in
`../t32/tx_isp_t32_hw.c`): the vendor's system irq 8
(`tisp_lce_interrupt_static`: statistics part from 0xc088, list 37 slots 4 /
5, curve half copy, list 16 slots 0x19 / 1 and start, list 37 start word and
start when 0x25040 reads 0), irq 30 (`tisp_lsf_interrupt_static`: histograms
or curves, then event 0xe) and event 0xe (`tisp_lce_soft_process` with
`Tisp_lce_soft`: per zone threshold search, histogram clip / ramps, cdf,
adjust, converge; the new curve into curve buffer half 2), with the lceInfo
state of `tisp_lce_init` in the vendor's layout and `tisp_lsf_write_curve`.
Host oracle `tests/t33_lcepf_host.sh` (Unicorn, vendor instructions vs ours):
the whole lceInfo block, section, curve buffer, lists, register reads, list
starts and events equal. QEMU (`-global ingenic-isp.x-int-extra=0x40000100`
raises irq 8 / 30 with each frame end): list 16 runs once per frame and list
37 once, as with the vendor module. Not ported: `tisp_lce_ev_update` (EV
changes), the tuning API set / day-night switch of the LCE section.

Build (3.10.14 kernel tree):

    make -C $KDIR M=$PWD/driver/t33 ARCH=mips CROSS_COMPILE=mipsel-linux- modules

A T33 camera kernel uses `CONFIG_CPU_MIPS32_R2` (vermagic `MIPS32_R2`); build
against a 3.10.14 tree configured like the camera kernel (MIPS32_R2,
preempt, `__isvp_goat_1.0__`), which gives the camera's vermagic. The T32 module built from these sources is
unchanged (identical `.text` and strings).
