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
    SoC that libimp checks the bin against (`bin_version`).
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

## Not yet (S4+)

No tisp/3A, no IQ bin parsing (the bin's physical address is kept), no
image-pipeline register programming (the QEMU model does not process pixels,
so this needs a camera); the stream-on list is empty. MSCA polyphase filter
curves (vendor: shadow port 0xf0820 + ch * 0xa0, computed per scale ratio)
and the per-channel filter select are not loaded; the channel parameter words
are the vendor's cv8001/PRJ007 values until the bin parser provides them.
Day/night, WDR, LDC, OSD, IVDC direct mode, AOV/RISC-V hand-over: off.

## Build

```sh
make -C $KDIR M=$PWD/driver/t32 ARCH=mips CROSS_COMPILE=mipsel-linux- modules
```
against a thingino T32 kernel 4.4.94 tree. Host tests:
`make -C tests tx_isp_t32_mem_rcfg_test tx_isp_t32_vic_test`.

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
