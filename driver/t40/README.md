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

Open image recipe (firmware tree `claude/t40-prod-fw`): same camera config
`eufy_t8416_t40xp_sc830_aic8800d40`, the ISP choice comes from the user
overlay fragment (`BR2_PACKAGE_THINGINO_ISP_OPEN=y`, proprietary choice
unset), open-tx-isp + OpenIMP, SDK sensor modules unchanged (the stock
`sensor_sc830ai_t40.ko` resolves all 35 imports against this module,
`tests/check_ko_symbols.sh`).  Build with `OPEN_TX_ISP_SITE=<this repo>
OPEN_TX_ISP_VERSION=<sha>` on the make line.  Keep the camera name: the
user overlay (U-Boot 2026.07, timps, WiFi) is keyed by it.

### Device runs 2026-10-12 (test boots 1-3, commits 5e94df1e..68ac89d0)

| run | state | result |
|---|---|---|
| b1 | vinum fix | `sc830ai stream on`, CSI/VIC/core streamon ret=0, isp-w02/isp-m0 IRQs run, 0 frames: tisp_main_init and MSCA used the synthetic GC4653 2560x1440 attribute; framechan1 events resolved to ch=-22; rmmod oops in `ispcore_slake_module` |
| b2 | + early attr seed, late pad bind, slake call 1 | geometry 3840x2160, core frame-done IRQ (status0 0x2000) every ~106 ms, channel 0: `GetFrame` OK but 1 frame per 2 s (channel frame-done only every 1.65 s, buffer content all zero); pad 1 resolved to channel 0; rmmod oops at a second mis-resolved call |
| b3 | + slake call 2, pad ids | ch0 native 3840x2160 (no scaler): same 1 frame / 1.65 s; ch0 1920x1080 run ended in a kernel panic (`panic=2` reboot, log lost: the kmsg drain wrote to tmpfs); extra run ch0 1280x720 with `irq_frame_done_require_content=0`: unchanged, so the content gate is not the limiter |

Current state: sensor, CSI, VIC and the ISP core run; the MSCA output
completes a frame only every ~1.65 s and writes no data (the sampler reads
the buffer uncached, the zeros are real).  The deferred frame-done work
(`regtrace_irq_frame_done_workfn`) finds a done address in the MSCA address
FIFO (`Tzn_Msca_addr_fifo_read`) only at that cadence.

### Round 2, 2026-10-12 evening (boots r1-r3, commits 9fe90a6f, 64692b1d)

- r1/r2a `t40_profile_direct_vic_feed=1`, ch0 native 3840x2160: 41 frames
  per 2 s (20 fps) through the VIC MDMA qbuf ring, but the dumped frame is
  all zeros; 720p under direct feed panics (VIC writes full-width lines
  into the small buffer).
- r3a normal path, registers read with devmem while streaming and diffed
  against the vendor stack streaming the same sensor
  (`t40prod/out/oracle/vendor-regs.txt` vs `out/p4/r3a/*-regs.txt`): MIPI
  PHY and CSI identical (settle 0x1b, lanes 2, VIC 0x04 = 0x0f000870,
  0x0c = 2); the vendor programs the VIC output DMA block 0x300..0x350
  (0x304 = 3840x2160, four raw buffers at 0x318..0x350) plus 0x8c/0x90/
  0xa0/0xb0 and 0x3a4/0x3b0/0x3c0/0x3e0/0x3e4 (0x32/0x44000/0x37b/0x204);
  the open driver leaves 0x304 at the probe default 1920x1080, the bases
  at 0 and the latter group at 0x6f/0x4402b/0x499/0x81.  T40 runs 4K in
  VIC frame mode (VIC -> DDR -> ISP core); the open driver only brings up
  the direct (streaming) input that worked for GC4653 2560x1440, so the
  core and MSCA produce empty frames.
- Sensor swap inside one boot (sc3338 counter-check) is refused by the
  regtrace sensor capture ("sensor client exists but no tx_isp_subdev_init
  capture occurred"), so that check needs its own boot with the sc3338
  module loaded first.
- rmmod oopsed four times in the recovered tisp_deinit path; the loop is
  now skipped by default (`skip_tisp_deinit=1`), untested.
- The kernel has no netconsole (CONFIG_NETCONSOLE/NETPOLL unset); panic
  logs need the serial console.
- timpsd did not start on one vendor restore boot (no log line); manual
  `S95timps start` worked.  Keep S95timps at mode 0755 when restoring.

### Round 3, 2026-10-12 night (A/B register oracle, commits f25ce716..0855c953)

Method: the same `t40_devtest` + OpenIMP `libimp.so` run on the vendor
1.3.1 module (39 fps at 4K) and on the open module (1 empty frame per
1.65 s), registers read with `devmem` mid-stream and diffed
(`t40prod/out/pv/v2/regs.txt` vs `out/p4/r11a/*-regs.txt`, full core
0x13300000-0x13320000, VIC, CSI, PHY).

- The vendor does not use the VIC DMA bank (0x13380300 stays idle); the
  frame-output path in f25ce716 is therefore off and only kept as an
  experiment.
- The vendor keeps the MSCA address FIFO like the open driver but with
  other words: Y 0x80021100 / UV 0x80022001, addresses tagged y|6 uv|7,
  1:1 coefficients 0x200/0/0x200/0, 0x1617c = 1, 0x16194 = 0x11001,
  0x1602c = 0x7600, shadow 0x50420 = 0, top 0x88 = 0x101, 0x40 =
  0x7fd0000d, `tisp_stream_on` writes 0x1010=0, 0x1014=0x11e, 0x1060=1,
  then 0x1010=1, 0x1014=1.  All of this is now reproducible through
  parameters (`t40_msca_fifo_ch0_*`, `t40_msca_fifo_addr_tag_mode=4`,
  `t40_msca_vendor_fixups`, `t40_quality_top40_override`,
  `tisp_main_init_reg88_override`, `t40_core_fixup_shadow0`,
  `t40_tisp_stream_vendor`, `sensor_full_*_override`) and was verified in
  the dump; the result did not change: the ISP core raises its frame-done
  (status0 0x2000) per frame, the MSCA FIFO keeps its two tagged entries
  (0x1617c/0x16194 = 0x22001) and never consumes them.
- What still differs after that: the tuning blocks the open driver never
  configures (DPC 0x3000, 0x4000/0x5000, AE/AWB statistics 0x7000/0x8000,
  0xb000, 0xe000, 0x10000/0x11000 and the LUTs) and the group
  0x13301104/0x1120/0x1134/0x1138/0x1160 (vendor 0x58f/0/0x333/0x100010/
  0xc700, open 0xde70716/0x20000/0/0x10002/0xa000; 0xa000 = 2560*16 again
  looks GC4653-sized).  Their writers are the tisp parameter tables, not
  direct register writes.
- Every second stream start after a 4K run panics (five of five), the
  first run never does; one run per boot until the cause is known.
  The kernel has no netconsole, so the panic text is lost.
- The sc3338 counter-check (sensor module loaded before the first
  AddSensor, CSI1, 2304x1296) detects and streams the sensor but oopses in
  `tx_isp_send_event_to_remote+0x3c0` at `FS_EnableChn`; not analysed.

### Night of 2026-10-13: ISP overflow

The vendor core ISR names error-status bit 0x20 "ispcore: isp overflow!!!"
(string `$LC183`); the open driver logged `err=0x20` on every frame.  The
seed's clock table pins `div_isp` to 200 MHz (168 MHz effective), the
vendor runs `isp_clk=300000000` (CPM ISPCDR 0x40000003 = MPLL/4).  Fixed by
taking the rate from the `isp_clk` parameter.  The vendor ISR also uses
status bit 0x400 for an MSCA work-enable countdown handshake (clear bit,
re-set after N frames); bit 0x400 never fired on the open driver while the
core overflowed.  Vendor counters are readable in `/proc/jz/isp/isp-m0`
(`debug0`), the sensor IQ bin is loaded from `/etc/sensor/sc830ai-t40.bin`.

After the clock fix the pipeline runs at 20 fps with 24/24 devtest passes
but all-zero output; the direct VIC feed dump (r15a) now contains pixel
data, so the black came from the sensor: the AE packed its GC4653 gain
index into the EXPO event while sc830ai's `set_expo` expects the LUT
register value (0x4080 = 1x).  b0611287 converts the index through the
sensor's `alloc_again`.

### 2026-10-13 00:28 – first real frames on the Eufy T40XP / sc830ai

Run r20a: open module (clock fix a16b94f3, AE alloc_again b0611287), 4K
channel 0, 39 frames per 2 s (20 fps), 2.4 Mbit/s H.264, decoded frame is
the IR night scene.  Parameters:

    insmod tx-isp-t40.ko isp_clk=300000000 t40_quality_profile=0 t40_generic_profile=1 \
        enable_mdns=0 enable_adr_reg_writes=0 enable_ydns=0 enable_sdns=0 \
        enable_ysp=0 sensor_full_width_override=3840 \
        sensor_full_height_override=2160 skip_tisp_deinit=1
    insmod sensor_sc830ai_t40.ko

With quality profile 5 (MDNS/YDNS/SDNS/YSP/ADR active) the core never
raised the late status bits 0x100/0x200/0x400 and the output stayed zero;
with profile 0 (blocks bypassed) the pipeline completes.

Block bisect (each run one boot, metrics only, media discarded):
- r21a profile 0 + `enable_mdns=1`: 20 fps, 2.3 Mbit/s, luma mean 42 -> MDNS
  is fine at 4K.
- r23a same: 186 frames / 10 s, consecutive-frame SSIM 0.977..0.997.
- r22a profile 0 + MDNS + YDNS + SDNS + YSP: immediate kernel panic
  (self-reboot, no console); one of the Y blocks crashes at 4K.
- r24a profile 0 + MDNS + YSP: kernel panic -> YSP is the crashing block
  (recovered `tisp_ysp_*` path at 4K); keep `enable_ysp=0`.
- r25a + SDNS, r26a + SDNS + YDNS, r27a + ADR, r28a + CCM + gamma, r29a + BLC + LSC + GIB: fine (200 frames / 10 s, 0 decode errors).
- r30a/r31a quality profile 5 with YSP off (and DMSC-lit off): black
  again; the profile >= 1 extras (GC4653 literal tables, top40 bit 2) are
  not usable on this sensor.
- Recommended for the Eufy: `t40_quality_profile=0 t40_generic_profile=1`
  (607695b7) = profile 0 plus MDNS, SDNS, YDNS, ADR, CCM, gamma, BLC, LSC,
  GIB; YSP and DMSC-lit off.
- r32a generic profile, channel 0 scaled to 1280x720: fine (201 frames /
  10 s, luma mean 53).  rmmod afterwards oopsed in the `isp_core_tuning_clear`
  model stub (bad pointer); guarded in 6b70a5e5.  r33a: rmmod still takes
  the camera down (no console); reload is not usable yet.
- t1 timps (OpenIMP backend, 4K main + sub stream) on the open module:
  timpsd oopsed the kernel in the recovered `tisp_s_antiflick` (bad AE
  parameter pointer) right after `SetISPRunningMode(night)` returned -1;
  antiflicker is now a no-op unless `t40_allow_antiflick=1` (open item: no
  50 Hz antiflicker on T40 until the recovered `tisp_s_antiflick` is fixed).
- t2: the next timps call, `tx_isp_sensor_g_attr` (IMP_ISP_Tuning_GetSensorAttr),
  oopsed on the empty core sensor slot; d3544601 answers it from the bound
  sensor's attribute and video state.
- WLAN failure of the agg38-fw/hybrid image (host-side diff): the user
  overlay `thingino-user/common/overlay/etc/wpa_supplicant.conf` bakes
  ssid FB_IOT_1 while the Eufy lives on FB_IOT_0 (its runtime overlay had
  FB_IOT_0 and the flash replaced it); S38wpa_supplicant in agg38-fw adds a
  preset/portal fallback on top.  Fix for the recipe: a per-camera
  `wpa_supplicant.conf` in the eufy user-overlay dir, or keep the overlay.
- t3-t6 timps (video0 1920x1080 scaled + video1 640x360): after the
  tuning fixes timpsd streamed, then died with SIGBUS on the first 1080p
  frames; devtest 1080p runs had panicked the kernel three times and every
  second stream start after a 4K run panicked.  Cause: the seed laid NV12
  buffers out as width*ALIGN(height,16); OpenIMP and the vendor use
  width*height*3/2, so 1080p frames overran their buffers by 8 lines.
  989e7424 makes the vendor layout the default (`t40_nv12_height_align=1`).
- t7 timps with the layout fix still died with SIGBUS right after
  "framesource 0 enabled".  kmsg showed the real cause: the second frame
  channel (video1 640x360) enabled the core IRQ with irq index 1 (= the
  second ISP core instance) -> `core IRQ OEM enable idx=1 ret=-22` ->
  `core-event streamon ch=1 ret=-22`, OpenIMP then crashed timps.
  f03d0700 maps the core IRQ to the vinum (channel/3, same as VIC/CSI)
  and reference-counts the streaming channels so the shared IRQ is
  enabled once and only disabled by the last channel.
- t8 (f03d0700): no -22 chain any more, but timps died again right after
  "framesource 0 enabled", now SIGSEGV pc=libimp VBMGetFrame+0xd4.
  Resolved via the crash maps + LD_TRACE: the crashing libimp is
  `/lib/libimp.so` = the **Ingenic vendor** library of the hybrid image
  (strings "IMP-ISP", "Ingenic High Pass Filter"), not OpenIMP.  All timps
  runs t1-t8 therefore ran vendor libimp against the open driver; its
  VBMGetFrame builds the frame pointer from an index the open DQBUF does
  not fill the vendor way -> wild pointer (t7 si_addr=0, t8 0x62dbef84).
  devtest always used OpenIMP (`LD_LIBRARY_PATH=/tmp/otx/lib`), which is
  why it streams.  p5-timps.sh now uploads lib2/libimp.so (OpenIMP
  t40-prod, frame gate) and starts timpsd with LD_LIBRARY_PATH=/tmp/otx/lib.
  For the open image: libimp must be OpenIMP (thingino-isp-open), the
  vendor ingenic-lib libimp cannot sit on the open driver.
- t9: the restored master image's /usr/bin/timpsd is the vendor flavour
  (USE_OPENIMP=0) and refuses the OpenIMP libimp ("libimp mismatch", exit
  78).  Built an OpenIMP-flavour timpsd from the image's own timps source
  (buildroot timps-custom copy, same USE_* set, USE_OPENIMP=1, linked
  against lib2/libimp.so): t40prod/bin-timpsd-oimp-t40; p5-timps.sh
  uploads it to /tmp/otx/timpsd.  Nothing in timps was changed.
- t9b (f03d0700, OpenIMP libimp + OpenIMP timpsd, fs_keepalive=1):
  framesource 0 (1920x1080 scaled) and 1 (640x360) both enabled, RTSP/HTTP
  up, local snapshot.jpg HTTP 200 (~44 kB), VIC IRQ exactly 20/s, timpsd
  RSS flat at ~3.4 MB, no kernel messages, 30 min clean (see
  t40prod/out/p5-t9b.log; timpsd left running afterwards).  Open item: timps reports "sensor fps:
  requested 20, driver holds 25/1" - sensor_g_attr returns the sensor
  module's 25/1, the stream runs at 20 fps.
- rmmod: 587502b1 splits the module exit into ten steps that can be run
  one by one with `echo exit N > /proc/tx_isp_t40_recovered` while kmsg
  streams (t40prod/p6-exit.sh); the step that takes the camera down is
  the one to look at.  Pending the next boot.
- YSP hypothesis for daylight: tisp_ysp_main_init is size independent
  (vmalloc 3432/420/146), so the "4K panic" is more likely a bus hang on
  YSP register writes while top40 bit 17 still gates the block (no kmsg
  at all in r22a/r24a, which fits a hang, not an oops).  Check the vendor
  order top40 -> ysp regs before enabling again.
- Second-start test (t40prod/p7-restart.sh, OpenIMP timpsd, keepalive=1):
  timps stop/start cycles die at a random cycle (c1: 3rd, c2: 5th, c3:
  2nd); the panic notifier (7c9f03a2) never fired, the watchdog reset the
  camera -> a hang, not an oops.  The flight recorder (fd83f4b7, 32 marks
  in the rmem tail, t40prod/panicdump.sh) shows for c3: stop of ch0/ch1
  completed (streamoff steps, release, tx-isp ioctls), the new start ran
  up to the ch1 stream-on, and the last mark is `isp_irq_handle irq=39`
  (ISP core IRQ) with nothing after it - the handler hangs on a register
  access right after the restart.  f337ddda marks every
  system_reg_read/write (offset) and the ISR entries to name the register.
- Panic/hang forensics on the Eufy: `panic_dump_phys=0xe9f0000` (module
  param, rmem tail): on panic the printk ring is copied there, every
  marked step writes into a ring at +0x100; after the reboot
  `t40prod/panicdump.sh out.txt` (mmap /dev/mem via t40prod/memdump) prints
  both.  `echo dumplog > /proc/tx_isp_t40_recovered` tests the copy.
- Flight recorder c5-c8c: the restart hung on a CSI or VIC register access
  at a random point of the CSI/VIC bring-up (CSI reg04/reg28, the first
  core IRQ after it, vic_core_s_stream).  CPM clock gates are identical
  while streaming and after a stop (p8-clk.sh), so no gated clock.  Root
  cause: frame channels 0..2 share vinum 0, but the direct stream path
  ran the CSI bring-up (reset, PHY) again for channel 1 while the sensor
  was already streaming, and the sensor was never stopped on streamoff -
  so a restart re-initialised CSI/VIC against live MIPI data.
  1284cb0e: CSI/VIC are started by the first channel of a vinum and
  stopped by the last one, and the sensor video stream is stopped before
  the last VIC stop (`t40_sensor_stream_off_on_stop=1`).  Result c10:
  15/15 timps stop/start cycles and 3/3 framesource idle off/on cycles
  (fs_keepalive=0) clean, snapshot 200 every time.  Second-start panic
  closed.  (906dacd0, CSI clocks re-armed after a slake, stays in: it is
  correct, just not the cause.)
- rmmod (p6 x1 on the same boot): exit steps 1-8 fine, step 10 (platform
  device unregister -> OEM remove callbacks) hangs the camera.  Split into
  one step per child device (fs, vin, vic, csi, core) with recorder marks.
Next: p6 per-device rmmod bisect; YSP order; sc830ai tuning (daylight).

### Daylight plan (image quality)
1. Extract the sc830ai tuning from `/etc/sensor/sc830ai-t40.bin` (vendor IQ
   bin, same layout the vendor module parses) and feed BLC/LSC/CCM/gamma
   from it instead of the GC4653 literals; compare against the vendor
   stream with the A/B method (same scene, metrics only).
2. Find the profile >= 1 black (top40 bit 2 and the GC4653 literal tables).
3. Daylight colour check: ircut on, AWB/CCM, then YSP at 4K.

### Next steps (host side first)

1. Implement VIC frame mode for the normal path: allocate the raw ring
   (vendor: four buffers), program VIC 0x300..0x350 and the 0x3a4..0x3e4
   group from the vendor values, feed the core's frame-mode input (regs
   0x1a8/0x1ac/0x1b0 already exist in `regtrace_t40_vic_start_mipi`), and
   wire `ispvic_frame_channel_qbuf` (vendor export used by the core) to
   the ring.  Reference: vendor `vic_core_s_stream`, `tx_isp_vic_start`,
   `ispvic_frame_channel_qbuf`, `vic_mdma_enable`.
2. Explain the direct-feed zeros with the same register group (0x3e0/0x3e4
   = 0x204, 0x3b0 bit pattern) before trusting the VIC MDMA path.
3. Then one boot: normal path 4K + 720p, register diff again, frame dump,
   rmmod with `skip_tisp_deinit=1`.

### Previous test plan (superseded)

1. Baseline ch0 1280x720 with `t40_profile_direct_vic_feed=1` (VIC frame
   MDMA to DDR, bypassing core/MSCA): proves CSI/VIC/sensor data flow and
   gives a raw picture to check the MIPI setup (2 lanes, 1080 Mbps, boot 4).
2. Same without the profile, read the bounded diag (`proc_live_reads=1`,
   `head -c` on the frame-done/MSCA sections) right after `StartRecvPic`:
   MSCA status/FIFO counters (0x16008, 0x1616c/0x16184 banks), core
   status0/err bits (0x1000 + err 0x20 appear once per frame), tisp event
   thread counters, `regtrace_irq_frame_done_*` counters.
3. Vendor reference on the same boot image is not possible (vendor rmmod
   oopses); take the vendor MSCA/core register values from a separate
   vendor boot (`/proc/jz/isp` or the ISP register dump tool) and diff them
   against the open driver's `regtrace_core_write` trace for 3840x2160.
4. Reproduce the 1920x1080 panic with the console log captured (serial or
   `netconsole`), it is the only kernel-fatal signature seen on this device.
5. rmmod after a run: the two mis-resolved calls in `ispcore_slake_module`
   are fixed on paper (68ac89d0) but untested; check with
   `dmesg | grep -c "Unable to handle"`.
