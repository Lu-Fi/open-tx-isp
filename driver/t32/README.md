# T32 (PRJ007) open TX-ISP driver — S3 (3 channels, sensor fps)

Status: **QEMU only** (`-M ingenic-t32`, kernel 4.4.94, vendor libimp 2.2.0,
prebuilt `sensor_cv8001_t32.ko`). Not tested on a camera.

T32 is a hybrid: T41-family Tiziano ISP core, T23-style MSCA at 0xf0000,
XBurst1 CPU (MXU/MXU2, no MXU3), HERA/JPEG encoders (T23/T30 soc_vpu
family). Plan and reference traces: `audit/T32_T33_PLAN_1025.md`,
`audit/T32_S0_1025.md` in the campaign notes.

This is a new, typed driver rather than a copy of the recovered T41 module:
the T41 code is written against T41 object offsets and ioctl numbers, and
both differ on T32 (sensor registration 116 bytes, framechan ioctls nr
131-140 with a 96-byte buffer, subdevice ops at 0x108). The pieces that are
generation-shared come from the shared code instead:

| Piece | Source |
|---|---|
| sensor registry, `/proc/jz/sensor` | `driver/common/tx_isp_sinfo.c` (T32 offsets in `tx_isp_t32_sinfo.c`) |
| MSCA channel geometry rules | `driver/t23/tx_isp_t23_msca_geom.h` (T32 channel layout in `tx_isp_t32_msca.h`) |
| kernel-API wrappers for sensor modules | same set as T41 (`tx_isp_t32_funcs.c`) |

## What S1 does

- Device nodes: `/dev/tx-isp`, `/dev/isp-m0`, `/dev/misc-ivdc`,
  `/dev/framechan0-2`; `/proc/jz/isp/isp-m0` and `isp-w02` (status text).
- `REGISTER_SENSOR`: creates the I2C client, the prebuilt sensor module
  probes, the driver copies the registration into the sensor object and calls
  its `g_chip_ident` (detection), then binds it in the sensor registry.
- Answers the libimp 2.2.0 sequence of `IMP_ISP_Open`, `IMP_ISP_AddSensor`,
  `IMP_ISP_EnableSensor`, `IMP_System_Init` and `IMP_ISP_EnableTuning`
  exactly as the vendor driver does (same ioctls, same order, same returned
  values; QEMU run s1d vs vendor r22):
  - `DRIVER_VERSION` fills a 64-byte buffer: SoC name and driver version,
    which libimp compares with its own (`driver_soc`, `driver_version`).
  - `DEFAULT_BIN` fills an 880-byte buffer with the IQ bin format version and
    SoC that libimp checks the bin against (`bin_version`), and, as the vendor
    driver, the WDR (word 852) and day/night (word 856) flags of the bin last
    set with `SET_TIZIANO_PAR` (20 bytes) for the VI in byte 862; libimp's
    `GetISPRunningMode` reads word 856. The flags survive `RELEASE_SENSOR`
    like the vendor module's (#303).
  - `ENUMINPUT` returns `{index, name[32]}`; libimp finds its sensor by name.
  - `MEMORY_MALLOC`/`FREE`: page-contiguous buffers returned as `{kva, pa}`;
    libimp maps them through `/dev/mem` and loads the IQ bins into them.
  - `S_INPUT` returns the sensor size as `w << 16 | h`.
  - `GET_ISP_MEM_INFO` returns the ISP working-memory size
    (`tx_isp_t32_mem.h`, same value as the vendor: 0x9f9000 at 3840x2160,
    `isp_memopt` 1500); IPC (RISC-V) memory 0.
  - `STREAMON`/`STREAMOFF` run the sensor's own init and `s_stream`.
- Register lists (`tx_isp_t32_rcfg.h`): builder and walker for the RCFG list
  format, for S2 (the vendor driver runs one list at stream-on).

## S2: frame path (`tx_isp_t32_hw.c`, `tx_isp_t32_fs.c`)

- `S_INPUT` writes the VIC input configuration, `STREAMON` enables the ISP
  interrupt block, runs the VIC and starts the MIPI CSI host and D-PHY
  (values and order of the vendor trace; derived from the sensor attributes
  since S3).
- `/dev/framechanN`: `REQBUFS`, `QBUF`/`DQBUF` (96-byte buffer, geometry from
  QBUF word 19 and stride word 17), `STREAMON`/`STREAMOFF`, `LISTEN_BUF`
  (`listen_timeout_ms`). Queued buffers go to the MSCA channel's push FIFOs;
  the ISP interrupt (INTC 29, `isp_irq`) drains the done FIFO (UV read
  before the Y pop) and wakes the listener. VIC interrupt: INTC 30
  (`vic_irq`). Closing the channel stops it.
- QEMU (vendor libimp 2.2.0, `sensor_cv8001_t32.ko`): FrameSource 10/10
  frames at 1920x1080 and 5/5 at 1280x720 with the model's colour bars, JPEG
  and H.264 encoders bound to channel 0 deliver 10/10, full teardown
  (`DisableSensor`/`DelSensor`) returns 0, rmmod/modprobe cycles with frames
  are clean.

## S3: sensor attributes, frame rate, channels 1/2, register lists

- VIC and CSI/D-PHY values derived from the sensor attributes
  (`tx_isp_t32_vic.h`; formulas in the header): line words, `csi_fmt`,
  `mipi_sc` control word, crops, frame mode; lanes and the D-PHY settle value
  from the lane clock. Identical to the vendor's writes for the cv8001 (67/67).
  DVP sensors and CSI host 1: refused (not referenced yet).
- Sensor frame rate: isp-m0 tuning id `0x08000070` (`num << 16 | den`); GET
  answers the sensor's rate, SET calls the sensor's
  `TX_ISP_EVENT_SENSOR_FPS` (frame length over I2C), as tisp does.
- `/dev/framechan0-2`: each channel gets size, crop, scaler steps (input /
  output in Q14) and its parameter words at `STREAMON`; no upscaling.
- Register lists (`tx_isp_t32_rcfg.h`): both command kinds (consecutive
  registers, bit 31 set; one table port, bit 31 clear). The stream-on list
  (table word 49, `0x22014` bit 17) runs at `STREAMON`; it is empty until
  tisp fills it (S4).
- QEMU (vendor libimp 2.2.0, our QEMU build with the cv8001 frame timing from
  its HTS/VTS registers): ch0 1080p + ch1 640x360 + ch2 720p at 14.7 fps
  (sensor 15/1), `SetSensorFPS(10/1)` -> 9.9 fps, 5 reload cycles clean.

## S4 (in progress): MSCA polyphase curves, stream-on list, statistics, front end

- At every channel start the driver loads the polyphase filter curves of all
  three channels through the MSCA shadow port (`tx_isp_t32_msca_curve.h`,
  flow of the vendor `tisp_msca_curve_calc` / `tisp_msca_ch_curve_write`):
  ratio out/in in Q14 (limit 27306), 25 samples of a Lanczos-2 shaped kernel,
  8 phases x 6 taps normalised to 2048, 13 words per direction at
  `0xf0820 + ch * 0xa0` (vertical +0x50), the tail field 0x5a at `0xf09e0`,
  the pair count to `0xf8100`. Unused channels get the flat 6-tap box.
  The kernel table is the vendor's (data transcribed from the vendor
  module's .rodata; `tools/t32_msca_sinc_tab.py` computes its shape, which
  missed 6 of 75 traced coefficients by 1 of 2048). The vendor leaves a not yet used channel 2 out of the first
  blocks; the driver always loads all three.
- `+0x28` filter-select pulse per channel (cv8001/PRJ007 values 0x41, 0x01,
  0x00 until the IQ bin parser provides them), as the vendor.
- QEMU (run s4a, vendor libimp 2.2.0): 3 channels 1080p/640x360/720p 8/8
  frames each, 14.7 fps; MSCA writes = vendor run v1 except the curve LSBs.
- Stream-on register list = the LSC block from the IQ bin
  (`tx_isp_t32_lsc.h`, flow of the vendor `tisp_lsc_init`,
  `tisp_lsc_calc_para_refresh`, `tisp_lsc_interp_by_tgain`,
  `tisp_lsc_write_all_reg`): parameters at bin + 0x98 + 0x68c4 (block id
  0x0134da11, also written to 0x5000), grid from the sensor size and the
  cell size, the 13 control registers 0x5040..0x5070, the mesh (1152 cells,
  3 words each) through the port 0x32020/0x32024, 0x2f4 = 0. Built from the
  cv8001 PRJ007 bin at 3850 K it is identical to the vendor's list (3479 of
  3479 words, host test with the bin and the r20 dump). Stage 1: between two
  colour temperature tables the nearer one is used (the vendor blends them;
  `tisp_lsc_interp_by_ct` is stage 2), the colour temperature is the module
  parameter `lsc_ct` (vendor init 5000, no AWB yet), gain 0. The QEMU model
  does not execute T32 lists: QEMU shows the list built and run (run s4c,
  3 channels 8/8 frames), the picture effect needs a camera.

- AE and AWB statistics engines initialised from the IQ bin at stream-on
  (`tx_isp_t32_stats.h`, flow of the vendor `tisp_ae_malloc_cfg`,
  `tisp_ae_set_hardware_param`, `tisp_ae_roi_point`, `tisp_awb_malloc_cfg`,
  `tisp_awb_set_hardware_param`, `tisp_awb_set_regional_threshold`,
  `tisp_awb_set_lum_th_freq`): zone grid, ROI, thresholds (EV 1024 as
  `tisp_awb_init`), buffers (AE 4 x 4 KiB zones + 4 x 2 KiB histograms, AWB
  4 x 0x7000). Against the vendor's AddSensor writes (cv8001 bin, 3840x2160):
  50 of 50 identical apart from the buffer addresses (host test with the bin;
  QEMU run s4g). The blocks are IQ bin sections 1 (AE, bin + 0x104) and 2
  (AWB, bin + 0xddc) of the section table the vendor's `tisp_bin_data_parse`
  walks (`tx_isp_t32_iq.h`: 28 x { enable, length } at 0x14, sections packed
  from 0xa0); the vendor copies each to its own tparamsP offset (AE + 0x68,
  AWB + 0xd40, LSC + 0x68c4), which explains the former 4-byte AE question.
  Stream-on checks that the bin's table puts them there.
  The engines are switched off before their buffers are freed (#132 lesson).
  Statistics interrupts (0x30028 bits 3/4/5) are counted (proc).

- **Bring-up controller, not the vendor AE/AWB** (`bringup_ae=1`, default off):
  scaffolding only, until the vendor AE flow is reimplemented. Every AE
  histogram interrupt schedules a work item that reads the histogram buffer
  written last (0x1708c bits 1:0; 256 bins of 24 bits, two per 64-bit word),
  takes the mean bin and scales the integration time by `bringup_target` /
  mean (at most x4 / x0.25 per step, 1 .. VTS - 4 lines, gain unchanged)
  through the sensor's EXPO event (0x02000017). No AWB, no tuning. QEMU run
  s4h: 13 histogram interrupts, 5 steps, the sensor ioctl accepted each; proc
  shows steps, last mean and integration time.
- **Vendor AE chain, in progress (openimp #234)**: the AE context init
  (`tx_isp_t32_aeinit.h`, `t32_ae_ctx_init` = state part of `tisp_ae_init`,
  oracle `t32_aeinit_oracle`) and the fixed point maths
  (`tx_isp_t32_aemath.h`, oracle `t32_aemath_oracle`) are ported and proven on
  the host. The driver builds the context at stream-on (`t32_pf_attach_ae`,
  `t32_ported_paths` bit 10, sensor block via `lib_tisp_sync_sensor_attr`)
  and writes the init tail registers. Since 2026-11-11 (part 3) the vendor AE
  runs per frame (`tx_isp_t32_ae.c` on `tx_isp_t32_aechain.h`, oracle
  `t32_aechain_oracle`, all 67 bins, 23k frames bit-exact against the whole
  vendor chain): every AE zone interrupt runs `tisp_ae_interrupt_static` (+
  `tisp_ae_long_ev_alloc` once the tune ran) and `tisp_ae_calc_process`
  (par_update_trig, algorithm = weight_mean + flicker + tune, long_process);
  sensor exposure as packed it | again << 16 (TX_ISP_EVENT_SENSOR_EXPO),
  event 4 (long total gain) feeds `t32_long_tgain_update` instead of
  `tisp_tgain`. The dd2d4 sensor block is `t32_ae_sensor_blk`
  (`tx_isp_t32_aesns.h`, oracle `t32_aesns_oracle`). Stats in
  /proc/jz/isp/isp-m0 ("vendor AE"). The exposure-list first-frame
  mismatch is gone (openimp #333: the vendor reads past the 2184 B state into
  the zero next-channel block; ours reads 0 there). Not QEMU / device tested
  (the #288 QEMU rootfs has no EnableTuning, so no IQ bin and no AE
  statistics; the QEMU cv8001 model has no exposure registers).
- **Vendor AWB per frame (openimp #234, `t32_ported_paths` bit 14)**: the
  state part of `tisp_awb_init` (`tx_isp_t32_awbinit.h`, oracle
  `t32_awbinit_oracle`) builds the AWB context at stream-on after the AWB
  statistics init (`tx_isp_t32_awbc.c`), and every AWB statistics interrupt
  (status bit 3) runs `tisp_awb_interrupt_static` + `tisp_awb_process`
  (`t32_awb_frame`; oracle `t32_awbchain_oracle`: init + 7990 frames through
  the whole vendor chain, bit-exact). The AE feeds the AWB as in the vendor's
  event thread: AE event 6 (long EV) -> `tisp_awb_ev_update` on an EV change,
  AE event 4 (long total gain) -> `tisp_awb_tgain_update` on a gain change,
  which re-interpolates and writes the regional / luma thresholds
  (`tx_isp_t32_awbtg.h`, oracle `t32_awbtg_oracle`). Stats "vendor AWB" in
  /proc/jz/isp/isp-m0. Host only.
- **Colour temperature fan-out (openimp #234, `t32_ported_paths` bit 15)**:
  after each AWB frame its event 11 colour temperature runs `tisp_ct_update`
  (`t32_ct_update`, oracle `t32_dispatch_oracle`, force / keep 0) on the CCM
  and BCSH states kept from stream-on (`t32_ccm_start` / `t32_bcsh_start`;
  `t32_ccm_ct_update`, `t32_bcsh_ct_update`, oracles `t32_ccmf_oracle` /
  `t32_bcshf_oracle`) and the CLM block state as `tisp_clm_init` leaves it
  (`tx_isp_t32_clmct.h`: `tisp_clm_ct_update` / `tisp_clm_ct_interp` on the
  kept state, oracle `t32_clmct_oracle`) in `tx_isp_t32_ctup.c`. LSC: see
  the next entry. Stats "ct fan-out" in /proc/jz/isp/isp-m0. Host only.
- **LSC colour temperature path (openimp #234, `t32_ported_paths` bit 19,
  with bits 11 and 15)**: `tx_isp_t32_lscct.h` ports the whole vendor
  `tisp_lsc_write_all_reg` (all modes: the completion wait for a running
  register list at 0x22014, the mesh through the LUT port: one sensor into
  the LSC register list image, else 0x32020 / 0x32024 or the shadow indices
  2767 / 2769; the registers as shadow indices or list words; the list
  trigger flag), `tisp_lsc_interp_by_ct` with its lock pattern,
  `tisp_lsc_ct_update`, `tisp_lsc_tgain_update` and
  `tisp_lsc_irq_frame_done`; oracle `t32_lscct_oracle` (d03a0 0 / 1 / 2,
  1 or 2 sensors, busy / idle, the image `tisp_lsc_init` leaves, 50/50
  mutations). `tx_isp_t32_lscct.c` keeps the LSC state and a vendor-layout
  LSC list (16 KiB, uncached) from stream-on; the ct fan-out runs
  `tisp_lsc_ct_update` on it, the long tgain update runs
  `tisp_lsc_tgain_update` on the same list (strength 0x5050 + list word 8),
  and the LSC frame done interrupt (0x300a8 bit 4 = bit 36) runs the armed
  list through table word 49 / bit 17 (`t32_hw_rcfg_lsc_trig`). Stats "LSC
  ct path" in /proc/jz/isp/isp-m0. Host + QEMU build only.
- **EV fan-out (openimp #234, `t32_ported_paths` bit 16)**: the AE event 6
  runs `tisp_long_ev_update` (`t32_long_ev_update`, oracle
  `t32_longev_oracle`): the AWB part (bit 14), `t32_ccm_ev_update` and
  `t32_bcsh_ev_update` (x = the AE brightness word) on the CCM / BCSH states
  of bit 15. Gamma, defog, TMO and WDR EV updates are not wired. Host only.

- **Front end: CSC, CSCCR, ISP top** (`tx_isp_t32_front.h`, flow of the vendor
  `tisp_csc_init`, `tisp_csccr_init`, `tisp_top_init`, `tisp_top_reg_cfg`),
  written at stream-on before the statistics engines: the colour conversion
  matrix of the bin's mode (BT.601/709/2020, full/limited; the bin supplies
  the mode, the vendor loads the matrix of the mode over its working copy),
  the chroma range (mode 0..2), the ISP top size/bayer/enable registers
  (sensor size, `isp_mbus_to_bayer` for 8/10/12-bit Bayer codes, control
  bits from IQ section 0). 25 of 25 writes identical to the vendor's
  AddSensor writes (cv8001 bin, 3840x2160, SRGGB10; host test with the bin).
  The vendor writes them at AddSensor, the driver at stream-on (the bin and the
  sensor mode are known there). IQ section offsets now include the look-up
  table payloads the vendor's parse copies after sections 7, 12, 13, 19, 20
  (not counted in the table lengths): sections 8 and up were at wrong
  offsets before (none was used).

- **tisp init blocks from the IQ bin** (S4 step 5; flow of the vendor
  `tisp_*_init` at AddSensor, one header per block, written at stream-on after
  the statistics engines, `t32_tisp_init`): DMSC (`tx_isp_t32_dmsc.h`, section
  11, 175 writes), gamma (`_gamma.h`, section 13 and its curve payload, 390),
  CDNS (`_cdns.h`, section 18, 16), YDNS (`_ydns.h`, section 17, 17), SDNS
  (`_sdns.h`, section 22, 27), YSP (`_ysp.h`, section 21, 147) and CLM
  (`_clm.h`, section 20 and its colour-temperature payload, 868; the
  YCbCr -> RGB matrix of the colour space is computed from the primaries, not
  copied). The interpolated blocks run the vendor's table interpolation at the
  gain index 1.0 (as `tisp_*_refresh(0x10000)` at init); the DMSC and YSP
  register functions are a mechanical transcription of the vendor flow (the
  section is edited by them, so the driver passes a copy). Against the vendor's
  AddSensor writes (cv8001 bin, 3840x2160, SRGGB10) every block is identical
  write for write: 1640 of the 5555 writes of the vendor's AddSensor core
  phase (host test `tx_isp_t32_tisp_test` with `T32_IQ_BIN` and the local
  trace `T32_ADD_TRACE`; with the earlier front end, AE/AWB and MSCA blocks 1726).
  A block whose section is missing or has another size is skipped. Not built
  (vendor flow needs inputs the bin does not carry or vendor-internal
  tables): tmo, MDNS (its compression setting comes from the init parameters),
  RDNS (vendor smooth table), DPC, GIB, defog, CCM, BCSH, LCE, AF and the
  remaining small blocks (about 3800 writes). QEMU (debug kernel,
  `slub_debug=FZPU`, `maxcpus=1`, vendor libimp 2.2.0): the blocks' writes
  appear in the stream-on phase with the vendor's values; picture effect
  needs a camera.

- **MDNS, DPC and GIB** (S4 step 6, same scheme as above): GIB (`tx_isp_t32_gib.h`, section 8, 89 writes; written by hand from the vendor functions, RGGB-first pointer table only), MDNS (`tx_isp_t32_mdns.h`,
  section 16, 1281 writes: frame compression setup, `func_en`, the 620-write
  register refresh twice with `start`, trigger and enable) and DPC
  (`_dpc.h`, section 5, 145 writes; the long and short exposure blocks and
  the other words, the extra short-exposure words stay off with this front
  end). The MDNS compression setting is the vendor's init parameter: the ratio
  in tenths (11..20 on, 10 off), i.e. `isp_memopt` / 100; the frame
  compression setup follows from it and the frame size. All three identical
  write for write to the vendor's AddSensor writes (cv8001 bin, 3840x2160): with
  step 5 and the front end, AE/AWB and MSCA blocks 3241 of 5555 (58 %). Not built:
  tmo, RDNS and AF need vendor-internal state tables the bin does not
  carry (AF: its filter coefficient tables; BCSH/RDNS/tmo: smooth and
  default curves); defog and AF also write the physical address of a driver
  buffer.

- **CCM and LCE** (S4 step 7, same scheme): CCM (`tx_isp_t32_ccm.h`, section 12 and
  its knot payload, 24 writes: the matrix at 5000 K from the knots with the
  hysteresis, the exposure-0 table words, the 1/range factors) and LCE
  (`tx_isp_t32_lce.h`, section 15, 33 writes: block grid from the frame size,
  strength words at gain 0, the 5x5 Gaussian smoothing words, default curve).
  Identical write for write to the vendor's AddSensor writes, except LCE's five
  DMA address words, which hold the physical addresses of the two buffers the
  driver allocates for it (work 0x26400 bytes, curve 0x5a00 bytes). Refused
  (vendor compiled-in defaults): CCM sections with byte `+0x35f` = 0 or
  `+0x38` = 1, LCE at 1920x1080 or with `+0x1fb` set. 3298 of 5555 (59 %).

- **BCSH** (S4 step 8, same scheme): `tx_isp_t32_bcsh.h`, section 19 with its
  colour temperature knots, plus the colour space section (9) that the
  vendor's `tisp_csc_api_get` hands the block; 49 writes, identical to the
  vendor's AddSensor writes (cv8001 bin, 3840x2160). Flow: parameter set from the
  exposure-0 interpolation, the 5000 K matrix, saturation/contrast/brightness
  curves and the hue rotation (sine table, matrix products), register list,
  block id `0x0134dae2`. Brightness, contrast, saturation and hue start at 128
  (neutral), nothing else is copied from the vendor's compiled-in defaults.
  Refused: section byte `+0x4b1` set (other exposure keying), `+0x492` > 2,
  a colour space section with the other `+0x62`/`+0x63` path. 3347 of 5555 (60 %).

- **Clocks** (S4 step 8): the driver gets the vendor module's clocks (`div_isp`,
  `div_isps`, `div_ldc`, `gate_isp`, `div_ispa`, `div_vpu`, `gate_ivdc`,
  `gate_csi`) at load, sets the platform rates, and at the first open of
  `/dev/tx-isp` sets the parents, the working rates and enables them (the
  last release disables them). Without the `gate_isp` enable the newer QEMU
  (which gates the ISP clock) delivers no frames.

- **Per-frame ported paths** (`tx_isp_t32_tisp.c`, openimp #234): the
  oracle-proven ports of the vendor's per-frame block refreshes run in the
  driver. Every AE histogram interrupt schedules one run of the ported
  `tisp_long_tgain_update` (`t32_long_tgain_update`); its refresh hooks call
  the ported `tisp_<blk>_refresh` functions unchanged and their writes go out
  in order (direct register mode, `t32_data_d03a0` = 0). Wired: DMSC, YSP, MDNS, DPC (long-frame gain),
  CDNS, YDNS, SDNS (contexts as the vendor's `tisp_<blk>_init` leaves them:
  the init-edited section copy, zeroed value block, last gain -1, YDNS
  hysteresis -1, DMSC sharpness 0x80; MDNS keeps the value block and frame size of its init), GIB, RDNS (openimp #234: context as
  `tisp_rdns_init` leaves it, vendor row table `t32_data_c0ac0` in `tx_isp_t32_rdnstab.h`; programmed at
  stream-on only on its ported path), LSC strength (`tisp_lsc_tgain_update`: register 0x5050, context
  from the stream-on list, current values as `tisp_lsc_init` leaves them, `t32_lscx_oracle`), LCE state and
  strengths (the 0x22850-byte state `tisp_lce_init` leaves, vmalloc'd, freed at stream-off;
  `tisp_lce_tgain_update` with `tisp_lce_tgain_interp_strength`, `tx_isp_t32_lcetg.h`, oracles
  `t32_lcetg_oracle` / `t32_lceinit_oracle`). The LCE curve (`tisp_lce_par`) runs from the vendor's
  block interrupts (`tx_isp_t32_blkirq.h` / `.c`, oracle `t32_blkirq_oracle`): at stream-on the three DMA
  buffers of `tisp_lce_malloc_cfg` (statistics banks 0x26400, curve 0x5a00, histogram 0x9000 B), the band
  table and the size shift bytes d6c64 .. d6c67 of `tisp_lce_init` / `tisp_lce_size_para_calc`, ISP status
  bit 8 = `tisp_lce_interrupt_static` and bit 30 = `tisp_lsf_interrupt_static` in the hard interrupt through
  the ported `lib_tisp_intp_process`, event 14 through the ported event queue to a work item that runs
  `tisp_lce_par` (`Tisp_lce_soft_t41_simd` or `_simd_v4`, `tisp_lce_write_curve`); the next bit 8 hands the
  curve to the hardware (0xc0a4). The driver's proc info counts attaches, bits 8 / 30 and events.
  TMO / ADR (`t32_ported_paths` bit 13, `tx_isp_t32_tmoblk.h` / `tx_isp_t32_tmo.c`, oracle
  `t32_tmoblk_oracle`): at stream-on `tisp_tmo_init` runs on a copy of IQ bin section 4 (11000 B, the vendor's
  tuning base + 11976) with the first gamma record: statistics DMA buffer (kmalloc 0x10000, registers
  0x7100 ..), channel record, tone buffer, adr block set, the default register refreshes and the ramY / ramV
  tables; bit 9 = `tisp_tmo_interrupt_static` (statistics, then the TMO path `tmo2fbf_input_prep` /
  `tmo2fbf_process`, or with stat byte 385 = 0 the ADR soft process up to `adr_fpga` and
  `adr_reg_write_every`), bit 31 = `tisp_tmo_fbf_interrupt_calc` (`tmo_soft_process`: stat_limit, ramY /
  ramV), both in the same block table and dispatch as LCE; `tisp_tmo_tgain_update` from the long tgain
  update; `tisp_tmo_deinit` at stream-off. `tisp_tmo_init` sets context byte 10042 (stat byte 385) to 1, so
  the TMO path runs; the ADR path needs `tisp_tmo_param_array_set` (not wired). `tisp_tmo_ev_update` is ported
  in the header but not called (the driver does not run `tisp_long_ev_update`). Proc: "tmo:" line. MSCA has no per-frame gain hook in the vendor (`tisp_long_tgain_update`
  does not call it; `tisp_msca_params_update` runs only from day/night, `param_array_set` and
  `cfg_shd`). AF and GSM (`t32_ported_paths` bits 17 / 18, `tx_isp_t32_afgsm.h` / `tx_isp_t32_afgsm.c`,
  oracle `t32_afgsm_oracle`): at stream-on `tisp_af_init` on IQ bin section 24 (800 B, the vendor's tuning
  base + 0xec30; statistics DMA buffer kmalloc 16 KiB, slots 0x18108 .., register image 0x18010 ..) and
  `tisp_gsm_init` (0x1904c), both from the vendor's module .data (AF attribute records d5c60, gain tables
  d5c90 / d6014, GSM context d6e00 .. d7e28); bit 6 = `tisp_af_interrupt_static` (event 15:
  `tisp_af_calc_process`, the focus metrics), bit 7 = `tisp_gsm_interrupt_hist` (event 16: `tisp_gsm_process`,
  zone weights), bit 63 (0x300a8 bit 31) = `tisp_gsm_irq_frmae_done`; `tisp_af_tgain_update` from the long
  tgain update. Proc: "af / gsm:" line. The total gain is `tisp_tgain`
  (default 0x10000, the vendor init gain) until the vendor AE runs in the
  driver, so with the default the first run rewrites the stream-on values and
  later runs write nothing. `t32_ported_paths` switches each wired block back
  to the old path (stream-on programming only) for device A/B tests.

## ISP OSD (openimp #336)

`tx_isp_t32_isposd.c`: libimp's ISP-drawn OSD reaches the MSCA. The isp-m0 tuning
controls 0x08000074 (mask block, 24 B), 0x080000a0 (draw block, 36 B), 0x080000a1
(OSD attribute, 32 B) and 0x080000a2 (OSD block, 20 B), set and get, call the ported
vendor setters / getters (`tx_isp_t32_msca_api.h`, `_set.h`, `_arr.h`) on the MSCA
parameter block of VI 0; the MSCA frame-done interrupt (status 0x300a8 bit 1, vendor
system irq 33) runs `tisp_msca_irq_frame_done` (`tx_isp_t32_msca_irq.h`, oracle
`t32_mscafd_oracle`) = `tisp_msca_cfg_shd(0, 0)`, which sends the pending windows,
masks and lines through the shadow-RAM port. The channel-start curve load and this
commit share the port under one lock. QEMU (openimp `tools/t32` isposd run, vendor
libimp 2.2.0 and OpenIMP): the OSD / mask / line pairs equal the vendor module's;
the QEMU model draws the pictures and masks. Not the vendor's: indexes the vendor
uses unchecked (window >= 8, line >= 20, mask set >= 3 / block >= 4, VI != 0) are
refused with -EINVAL, getters start from a zeroed block (vendor: stack garbage).
`t32_isposd_trace=1` logs requests and commits.

## ISP tuning controls (openimp #296)

`tx_isp_t32_tunctl.c` answers the libimp 2.2.0 `IMP_ISP_Tuning_*` requests on
`/dev/isp-m0` (`0xc0105467`, `{vi, dir, id, value}`) through the ported vendor
dispatchers (`tx_isp_t32_txsctrl.h` / `tx_isp_t32_txgctrl.h`): BCSH bytes,
anti-flicker, mirror / flip, module control, module ratio, CSC, CCM, gamma
(presets), statistics config, AE / AWB / AF attributes and weights, face AE /
AWB, WDR output mode, scaler level, CT trend offset, CSC-CR, HB line space,
sensor attributes, autozoom, frame-done wait; AE start EV on `/dev/tx-isp`
`0xc0045436/7`. The AE / AWB / statistics ids are bound to the per-frame AE
and AWB blocks (`tx_isp_t32_tunblk.c`: the ported `.isra` wrappers, `tisp_[sg]_*`
API and `t32_aea_*` / `t32_awb_*` block API, so a Set is read by the next AE /
AWB frame and a Get answers the live statistics and exposure state). AE and AWB
run live from stream-on (openimp #350: the AE context built by the statistics
init is no longer freed by the tisp init). Vendor UB kept: the face AE weight
id (0x2a, `IMP_ISP_Tuning_SetFaceAeWeiget`; third word = AE smoothing window length) is not
bounded by the vendor; a value above 221 makes the next AE frame write past the
AE state block (kernel memory corruption, the vendor module hangs as well). The other
attributes are still kept per channel and returned (not applied yet). The AF part
of the statistics config, AF weight, AF statistics and AF metrics run on the live
AF block, the statistics config's GSM zone bounds go to the live GSM block
(`tx_isp_t32_afgsm.c`). Sensor fps (`0x08000070`) stays in
`tx_isp_t32_core.c`, OSD / mask / draw in `tx_isp_t32_isposd.c`. QEMU vs the
vendor module: see CHANGELOG.

## IVDC direct mode (openimp #349)

`direct_mode=1` sends the sensor's frame channel 0 to the encoder over the
IVDC (ISP -> encoder line buffer, 0x13100000, INTC 57) instead of memory, as
the vendor module. `tx_isp_t32_ivdcdrv.c` runs the oracle-proven ports of the
vendor IVDC code on vendor-layout subdev / private objects:
`ivdc_core_ops_init` at the ISP stream-on (reset, mode word 0x5c),
`ivdc_misc_unlocked_ioctl` on `/dev/misc-ivdc` (GET/SET_DIRECT, SET_PADDR,
MEM_LINE, MAX_STRIDE, SET_MEM_LINE), `ivdc_pad_event_handle` for frame
channel 0 (SET_FRAME_FORMAT: geometry; REQBUFS: DDR ring 0x30 / 0x34 / 0x38;
FRAME_STREAMON / OFF: irq mask, enable) and
`ivdc_core_interrupt_service_routine` (frame start 0x44 bit 17 completes
channel 0's next buffer). The MSCA writes channel 0 to the IVDC addresses
(Y 0x80000000, UV 0x81000000, 4096-byte lines), pushed for each queued buffer
and again at each channel-0 done; DQBUF keeps the buffer queued (libimp
queues them once). The ISP vertical flip is refused in direct mode (ported
`tx_isp_core_ops_s_ctrl`, d9014). GET / SET_FRAME_FORMAT copy the vendor's
116-byte format on every channel (ported `frame_channel_vidioc_get_fmt` /
`set_fmt`). Direct modes 2..4 (several sensors) are refused at load (mode 0).

## Not yet (S4+)

No tisp/3A, no IQ bin parsing (the bin's physical address is kept), no
image-pipeline register programming (the QEMU model does not process pixels,
so this needs a camera); the stream-on list carries the LSC block only. The channel parameter words
are the vendor's cv8001/PRJ007 values until the bin parser provides them.
Day/night, WDR, LDC, AOV/RISC-V hand-over: off (ISP OSD, IVDC direct mode: above).

## Build

```sh
make -C $KDIR M=$PWD/driver/t32 ARCH=mips CROSS_COMPILE=mipsel-linux- modules
```
against a thingino T32 kernel 4.4.94 tree. Host tests:
`make -C tests tx_isp_t32_mem_rcfg_test tx_isp_t32_vic_test tx_isp_t32_msca_curve_test tx_isp_t32_lsc_test tx_isp_t32_stats_test tx_isp_t32_front_test tx_isp_t32_tisp_test`
(`T32_IQ_BIN` / `T32_RCFG_REF` / `T32_ADD_TRACE` for the comparison with the vendor lists).

## Module parameters

| Name | Default | Meaning |
|---|---|---|
| `print_level` | 1 | `isp_printf` threshold (0 info, 1 warning, 2 error) |
| `isp_memopt` | 1500 | MDNS memory ratio in 1/1000 (<= 1000 uncompressed, max 2000) |
| `driver_soc`, `driver_version` | `PRJ007`, `20251107a` | answered to `DRIVER_VERSION`; must match libimp |
| `bin_version` | `1.00` | IQ bin format version answered to `DEFAULT_BIN` |
| `t32_trace` | 1 | log every ioctl (bring-up) |
| `isp_irq`, `vic_irq` | 37, 38 | Linux IRQs of INTC 29/30 (XBURST_SOC_IRQ_BASE 8) |
| `listen_timeout_ms` | 2000 | `LISTEN_BUF` wait limit |
| `direct_mode` | 0 | 1 = frame channel 0 over the IVDC to the encoder (2..4 refused) |
| `ivdc_mem_line`, `ivdc_mem_stride`, `ivdc_threshold_line` | 0, 3840, 0 | IVDC DDR ring lines (0: half the picture), ring stride (modes >= 2), threshold line (0: half) |
| `ivdc_irq` | 65 | Linux IRQ of the IVDC (INTC 57) |
| `lsc_ct` | 5000 | colour temperature for the LSC mesh at stream-on |
| `bringup_ae` | 0 | 1 = bring-up exposure controller (not the vendor AE) |
| `bringup_target` | 64 | its target mean histogram bin (0..255) |
| `t32_ported_paths` | 1 | 1 every wired per-frame block on its ported path, 0 all on the old path, else mask: bit 1+n = block n (0 DMSC, 1 YSP, 2 CDNS, 3 YDNS, 4 SDNS, 5 MDNS, 6 DPC, 7 GIB, 8 RDNS, 9 AE, 10 LSC, 11 LCE (with the block interrupts 8 / 30), 12 TMO (with the block interrupts 9 / 31), 13 AWB, 16 AF (bit 17, interrupt 6), 17 GSM (bit 18, interrupts 7 / 63), 18 LSC ct mesh (bit 19, interrupt 36, with 10 and 14); RDNS off = not programmed at all, as before) |
| `tisp_tgain` | 0x10000 | total gain for the per-frame refresh until the vendor AE runs (test aid) |
| `ldc_irq` | 46 | Linux IRQ of the LDC / isp-m1 (INTC 38), read-only (`tx_isp_t32_snapldc.c:89`) |
| `t32_isposd_trace` | 0 (`0644`) | 1 = log every ISP OSD request and every MSCA OSD commit (`tx_isp_t32_isposd.c:64`) |

All parameters except `print_level`, `listen_timeout_ms`, `lsc_ct`, `bringup_*`, `t32_trace`, `t32_ported_paths`, `tisp_tgain` and `t32_isposd_trace` (`0644`) are read-only (`0444`); the vendor glue names (`clk_name`, `isp_clk*`, `isp_cache_line`, `isp_ch0_pre_dequeue_time`, `atlas_open`, `zeratul_open`, ...) are accepted so that the vendor glue module line loads (read-only, no effect on the driver behaviour). The T33 module (`tx-isp-t33.ko`) reports `PRJ008` as `driver_soc`; see `driver/t33/README.md`.
