# Ingenic T40 ISP Recovery

This directory is the T40 bring-up workspace for matching the OEM
`tx-isp-t40.ko` behavior on Linux 4.4.94 targets.

The initial `tx_isp_t40_recovered.c` file is a recovered whole-driver seed plus
the local parity fixes from the Wyze Cam 3 Pro T40XP/GC4653 smoke-test loop.
Hardware sequencing and tuning remain local, while reviewed object-layout
adapters now let T40 consume the same small shared-library contracts as the
other open drivers.

Build from a compatible 4.4.94 kernel tree with:

```sh
make -C <kernel-src> M=$(pwd)/driver/t40 modules
```

Expected artifact:

- `driver/t40/tx-isp-t40.ko`

## Current organization

- `tx_isp_t40_recovered.c` owns the recovered pipeline, device lifecycle,
  hardware programming, diagnostics, and T40 tuning policy.
- `tx_isp_t40_sinfo.c` adapts the T40 sensor object layout to the common typed
  registry.
- `tx_isp_t40_subdev.c` adapts the extended T40 graph and subdevice layout to
  the common graph resolver, remote-event resolver, readiness policy, and
  pad-link operations.

The T40 adapter retains generation-local pointer validation and the recovered
queue/state offsets (`0x1fc` and `0x218`). Remote-event filtering, diagnostics,
and the local frame-done fallback also remain in the recovered driver.

The first shared-subdevice device cycle ran on a T40XP/GC4653 camera on
2026-07-31.  A later 2026-08-31 cycle restored sensor-owned geometry and
rational frame rate reporting, producing the GC4653's native 2560x1440 stream
through both OpenIMP and direct V4L2 without a Raptor resolution override.
The open module registered one sensor, accepted forced day/night transitions,
kept IRQ 38/39 active, and supplied valid H.264 frames without a kernel-fatal
signature during the sustained checks.

Do not use an unbounded full read of `/proc/tx_isp_t40_recovered` as a health
probe. That legacy diagnostic path remains unsafe and caused the one-shot
test boot to fall back to stock. Use the bounded T40 probe tooling and the
specific `/proc/jz/sensor` nodes instead. The fail-safe boot loader now keeps
its early dmesg and kmsg captures in its persistent state directory so this
class of watchdog fallback retains evidence.

## Validated parity repairs

The 2026-07-18 parity batch replaced three unconditional kernel-build stubs
with behavior checked against the T40 OEM disassembly:

- `fix_point_div_32`: unsigned integer and fractional Q-format division
- `tisp_round_int64`: signed 64-bit right-shift with half-up rounding
- `defog_wei_interpcot`: 32-bin accumulation, rounded averaging, clamping,
  and monotonic output
- `defog_count_weight35abc`: preserve the OEM reducer count across the final
  call's MIPS delay slot and restore cleanup of all 13 temporary buffers

The batch builds against the Thingino T40XP/GC4653 Linux 4.4.94 tree. A smoke
run on the `.242` test camera completed block initialization, produced a valid
1920x1080 RTSP frame, kept Raptor and both ISP interrupt lines active, and
returned the camera to a clean boot. Evidence is in
`logs/20260718-t40-parity-batch1-final2-smoke/`.

The final OEM/recovered assembly audit reports 32 hard stubs, down from 35
before this batch, with no increase in collapsed functions (18).

## Forensic image-quality checkpoint

The 2026-07-18 GC4653 comparison loop reached the retained "good enough"
checkpoint on the `.242` test camera.  The stock oracle is
`logs/20260718-t40-forensic-stock-2105/`; the most useful recovered-driver
evidence sets are:

- `logs/20260718-t40-v42-lsc-active-exposure-trim-settled/`: best stored
  full-frame score and the retained stable visual baseline
- `logs/20260718-t40-v52-ysp-lsc-gain-settled/`: independent YSP/DNS gain
  control with the fixed YSP register groups matching stock at gain 229376
- `logs/20260718-t40-v53-dmsc-literal-gain212541/`: complete literal DMSC
  smoke, state/core capture, frame comparison, and live gain sweep

The retained implementation includes the exact LSC mesh, fixed CLM topology,
live CCM/BCSH controls, independent `ysp_gain_ev`, and a self-contained
source-derived DMSC chain.  The DMSC literal path remains opt-in through
`enable_dmsc_lit=1`: its complete consumer/evidence cycle was run at the stock
event gain of 212541.  A disposable live sweep found that gain 229376 was the
closest match for the primary stock DMSC register groups, but it was not
promoted to the default because it did not receive a separate full consumer
cycle.  The sweep was discarded with a clean reboot.

The exact source-derived BLC/GIB paths now follow the active sensor IQ bank's
bypass flags on every day/night transition.  The GC4653 day and night banks
enable BLC and supply its gain curves, so BLC is initialized and updated from
the sensor's live analog gain.  Both banks explicitly bypass GIB; the driver
honors that calibration instead of force-enabling the block.  Force-enabling
GIB against those banks produced a nearly black/green frame and is retained
only as a forensic override.

The 2026-09-01 image-quality cycle also removed an uncalibrated coupling
between white-balance gains and colour temperature.  WBG gains alone are not a
portable CT measurement, and the old placeholder ratio mapped a neutral
GC4653 scene to the 2300 K endpoint, causing a yellow/green cast.  LSC and CCM
remain active on the tuning bank's calibrated 5000 K reference until the AWB
path supplies a real CT result.  The same cycle widened the frame-sampled AE
deadband to its configured value and limited highlight trimming to a scene
whose mean has actually reached target, eliminating the observed gain-index
ping-pong.

The date/time OSD in the upper-left corner is outside the ISP comparison.  It
is rendered by the userspace overlay using the camera's system clock, so any
remaining offset or formatting issue should be fixed in the Thingino
time/NTP/timezone and OSD configuration without changing this driver profile.

## Eufy T40XP / sc830ai bring-up (2026-10-12, branch `claude/t40-sc830ai`)

Second T40 device: Eufy T8416 (T40XP, sc830ai 3840x2160@20 on CSI0/i2c1
0x30, boot mode 4; the second sensor sc3338 is unused).  Linux 4.4.94
(`thingino-out/eufy_t8416_master/build/linux-3143636237cf5…`), vendor SDK
d30c0af with ISP firmware 1.3.1.

First open-driver boot (2026-10-12 19:20, commit 3bd3087b): insmod, sensor
detect, `IMP_ISP_Open/AddSensor/EnableSensor`, `FS_EnableChn` and
`StartRecvPic` all returned 0, but 0 frames, ISP IRQs 36-39 stayed at 0 and
the sensor never logged "stream on".  dmesg showed
`send_event … event=0x3000002 ret=-22` (SET_FMT), `0x3000005 ret=-22`
(QBUF) and `0x3000008 ret=-515` (SET_BANKS, expected) on framechan1.

Findings:

- `t40_devtest` streams frame channel 1 (1280x720 scaled).  The regtrace
  stream path handed the frame-channel index to `vic_core_s_stream`, the CSI
  state/stream helpers, `tx_isp_vic_index_cal` and the cached sensor
  attribute lookup.  On T40 those take the sensor number (vinum): frame
  channels 0..2 belong to vinum 0, 3..5 to vinum 1 (the OEM `tx_isp_core_probe`
  stores `index / 3`; the Eufy 1.3.1 module writes `index % 3` to +0x74 and
  `index / 3` to +0x78 of each core channel).  Channel 1 therefore addressed
  the unbound second sensor.  Fixed by `regtrace_channel_vinum()`.
- `AeIntegrationTimeUnit : -1064949056` plus a stack dump is not a sensor
  attribute problem: `frame_channel_unlocked_ioctl` prints a mis-resolved
  rodata literal (`LC79`) with `&__param_str_isp_dual_buf` for an
  unsupported ioctl; the value is that pointer.
- The rmmod oops recorded on the Eufy (`Kernel unaligned instruction access`,
  epc 0x6ef, module size 763904) was the vendor `tx_isp_t40`, not the open
  module.  The open module's unload path (`tx_isp_t40_core_exit`) frees IRQs
  with the same dev_id it requested them with and pins itself through the
  misc fops owners; it has not been rmmod-tested on the Eufy yet.
- `KERN_INFO` notes are suppressed unless `t40_runtime_trace=1` (only lines
  with `ret=-…`/`fail=…` pass), so the first boot's dmesg hid the whole
  core-event / VIC / CSI trace.  Test boots use `t40_runtime_trace=1` with a
  `dmesg -c` drain loop.
- `regtrace_core_event_tisp_bootstrap` only runs for frame channel 0, so a
  stream must start on channel 0 before channel 1 can be used.
- The driver still carries GC4653 fallbacks (synthetic attribute 2560x1440,
  literal LSC/CLM/DMSC tables, IQ bank names).  They are only used when the
  sensor attribute is missing, but image quality on sc830ai is GC4653-tuned
  until an sc830ai bank exists.

Open image recipe (firmware tree `claude/t40-prod-fw`): camera config
`eufy_t8416_t40xp_sc830_aic8800d40_openisp` =
`BR2_PACKAGE_THINGINO_ISP_OPEN=y` (open-tx-isp + OpenIMP), SDK sensor
modules unchanged (the stock `sensor_sc830ai_t40.ko` resolves all 35 imports
against this module, `tests/check_ko_symbols.sh`).  Build with
`OPEN_TX_ISP_SITE=<this repo> OPEN_TX_ISP_VERSION=<sha>` on the make line.
