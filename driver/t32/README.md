# T32 (PRJ007) open TX-ISP driver — S1 skeleton

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

## Not yet (S2/S3)

No ISP register is written: no VIC/CSI/MSCA programming, no frames
(`FrameSource_EnableChn` fails at `REQBUFS`), no tisp/3A, no IQ bin parsing
(the bin's physical address is kept). Day/night, WDR, LDC, OSD, IVDC direct
mode, AOV/RISC-V hand-over: off.

## Build

```sh
make -C $KDIR M=$PWD/driver/t32 ARCH=mips CROSS_COMPILE=mipsel-linux- modules
```
against a thingino T32 kernel 4.4.94 tree. Host test:
`make -C tests tx_isp_t32_mem_rcfg_test`.

## Module parameters

| Name | Default | Meaning |
|---|---|---|
| `print_level` | 1 | `isp_printf` threshold (0 info, 1 warning, 2 error) |
| `isp_memopt` | 1500 | MDNS memory ratio in 1/1000 (<= 1000 uncompressed, max 2000) |
| `driver_soc`, `driver_version` | `PRJ007`, `20251107a` | answered to `DRIVER_VERSION`; must match libimp |
| `bin_version` | `1.00` | IQ bin format version answered to `DEFAULT_BIN` |
| `t32_trace` | 1 | log every ioctl (bring-up) |
