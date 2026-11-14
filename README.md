<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/banner-dark.svg">
    <img src="docs/assets/banner.svg" alt="open-tx-isp - open source vision stack for Ingenic-based SoCs" width="560">
  </picture>
</p>

<h1 align="center">open-tx-isp</h1>

<p align="center">

[![license](https://img.shields.io/badge/license-GPLv3-blue)](#license)
[![SoCs](https://img.shields.io/badge/SoC-T10%20%C2%B7%20T20%20%C2%B7%20T21%20%C2%B7%20T23%20%C2%B7%20T30%20%C2%B7%20T31%20%C2%B7%20T40%20%C2%B7%20T41-3e63dd)](#status)
[![status](https://img.shields.io/badge/open%20stack-device%20tested-30a46c)](#status)
[![branch aperto](https://img.shields.io/badge/branch-aperto-e5484d)](https://github.com/opensensor/open-tx-isp/tree/aperto)
[![thingino](https://img.shields.io/badge/thingino-integrated-orange)](https://github.com/themactep/thingino-firmware)
[![platform](https://img.shields.io/badge/platform-MIPS%20%C2%B7%20Linux%203.10%20%26%204.4-lightgrey)](#build)
[![last commit](https://img.shields.io/github/last-commit/opensensor/open-tx-isp/aperto)](https://github.com/opensensor/open-tx-isp/commits/aperto)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen)](https://github.com/opensensor/open-tx-isp/pulls)

</p>

Open reimplementation of Ingenic's closed TX-ISP kernel driver (`tx-isp-<soc>.ko`) for
T10, T20, T21, T23, T30, T31, T40 and T41 (T32 and T33 are planned and exist as QEMU-tested ports only, see [Status](#status)). The goal is behavioural equivalence with the
OEM driver: same ioctl and `libimp` ABI, same register sequencing, same image behaviour,
with cleaner unload/reload, checked inputs and less memory.

It works with Ingenic's unmodified `libimp.so` and with the open replacement
[OpenIMP](https://github.com/opensensor/openimp). Open stack = open-tx-isp + OpenIMP + a camera application. The `aperto` branch carries the released open stack on top of the original `main` line; see
[Branches and releases](#branches-and-releases).

This is a reverse-engineering and compatibility effort, not a greenfield pipeline: OEM binary
analysis, `libimp` ABI work and recovery of tuning data.

## Status

**Newest release candidate: RC3 `v2026.11.07-rc3`** (same tag in open-tx-isp and OpenIMP, released 2026-11-07; RC3 gate green: every vendor function that a streamer calls works on every gate SoC, 2017 of 2017 cells, no open critical/high-severity issue, RC3 milestone empty). Release note: [docs/releases/RC3.md](https://github.com/Lu-Fi/openimp/blob/next/docs/releases/RC3.md) (in OpenIMP), [GitHub release](https://github.com/Lu-Fi/open-tx-isp/releases/tag/v2026.11.07-rc3). Gate SoCs: T10, T20, T21, T23, T30, T31, T40, T41; T32 and T33 are planned (QEMU only; the drivers `driver/t32` and `driver/t33` have been in `next` since 2026-11-13, see [driver/t32/README.md](driver/t32/README.md) and [driver/t33/README.md](driver/t33/README.md); not on a camera, not part of RC3). Next: Release 1 = the image built from the RC3 tag runs 14 days without an unplanned reset on at least one camera per gate SoC. Previous tags: RC2 `v2026.10.25-rc2`, RC1 `v2026.10.11-rc1` ([docs/RELEASE-v2026.10.11-rc1.md](docs/RELEASE-v2026.10.11-rc1.md)). Release notes and known issues: [CHANGELOG.md](CHANGELOG.md) (earlier states such as the agg-28 candidate and agg-37 are recorded there). The per-SoC table below is a short summary as of RC3 (2026-11-07); the authoritative per-feature state is the matrix. "Fully open" = open driver, OpenIMP and the camera application run from a flashed image.

| SoC | Status |
|---|---|
| T10 | Fully open; RC3 gate (all 228 application-used function cells work); day/night, reload (5 cycles) and boot guard verified; front-crop window kept while no downscaled channel is open and released with the tuning session. Driver review (#220-#222): fewer high-order allocations, about 20 KB less module RAM (bring-up trace parameters only with `TX_ISP_TRACE=y`). Module sizes: [docs/SIZE.md](docs/SIZE.md). |
| T20 | Fully open; RC3 gate (228/228); 10x stop/start and reload without an oops, `rmmod` during streaming refused; `isp-m0` reports the reachable analog gain cap (smaller of the sensor maximum and the `SetMaxAgain` ceiling) for day/night; same driver review as T10 (#220-#222). |
| T21 | Fully open; RC3 gate (252/252); AE/ADR/defog/AWB lifted from the vendor module; `SetBrightness` acts and sepia works (beyond vendor); VIC error interrupt no longer writes the NULL page, netlink/deinit serialised, `warn_stack` (default 0: no vendor stack dump after warnings) (#220-#222, #231). |
| T23 | Fully open (native encoder in OpenIMP); RC3 gate (290/290); vendor AE default; frequent Helix frame drops fixed (residual interrupt, kernel patch merged upstream); stock-like release defaults, MSCA scratch buffer, AF statistics on by default (see "T23 module parameters"); the FrameSource crop-change hang is fixed (agg-34: a window that does not fit is dropped instead of hanging the MSCA); the rare single Helix encode error (errno 5) has not recurred since the kernel fix (0 errors in 6 days, provisionally closed); AWB luminance threshold from the AE mean on every AWB interrupt like stock (`source_awb_lum_th`, #231); optional ISP bypass graph `isp_bypass_graph=1` (default off, #129). The AWB runs on every AWB statistics interrupt like the vendor since 2026-11-11 (`source_awb_hlil_interval` = 1, #233). Open: no real WDR. |
| T30 | RC3 gate SoC since 2026-11-03 on the T30X lab camera (SC4236; runtime module swap, device batches b1-b51; 230/230 cells). Driver work device-proven there: tuning controls served, tuning-bin customer init, system table, day/night bank reload, CCM light-source bank, sharpen coring, analog gain path, AWB_CWF_SHIFT, Iridix strength fixes, direct raw writer for the ISP bypass graph (#177, #202, #218); H.264 and H.265 streams decode clean, 0 oopses. Built at `-Os` by default since 2026-11-13 (#274; registers and picture equal to the `-O0` build and the vendor, 35-minute stream soak with day/night switch, 0 oopses); debug proc nodes `isp-w00`/`w01`/`w02`/`m3`/`fs` like the vendor (#464); the sensor module is pinned while the ISP holds it (#497, QEMU only). Not yet run: a multi-day soak. |
| T31 | Reference SoC; SC2336, GC2053, SC301IOT; RC3 gate (319/319); `isp_memopt=1` default; `SetFrameDrop` with the stock semantics; front crop refuses a window the MSCA would have to upscale beyond `fcrop_upscale_pct` (-EINVAL; default 100 = 2.0x since 2026-10-24); driver review (#223, #231): module RAM -16 KB, no idle IRQ threads, "ch0 done" counted once, CSI shared-line handler fixed; optional ISP bypass graph `isp_bypass_graph=1` (default off, #129); the T31L reset #94 was closed on 2026-11-07 after a 3 h debug-kernel stress without a reset. |
| T40 | Runs from a flashed image on the Eufy T40XP test camera (sc830ai) with the open ISP driver; RC3 gate (217/217). Vendor-equal picture in the fair light-box pass (#84): AE target, metering and exposure split as the OEM (#225), frame AE meters the hardware AE statistics (#232), AWB thresholds follow the total gain, chroma noise vendor-equal (#213), CDNS day/night like the vendor (#203); the lifted colour chains are plain C, oracle-checked against the vendor module (#85, #189, #193); release-gate oopses and the AWB close oops fixed (#173); application CPU 0.34 % against 2.26 % on the vendor stack. Only the sc830ai board is device-tested. |
| T41 | Fully open from a flashed image (H.264, H.265); RC3 gate (253/253); reload verified; the use-after-free of the I2C sensor client when DelSensor races a running frame work is fixed (#134); AF statistics only with `t41_af=1` (default off, #179); optional ISP bypass RAW16 channel `isp_bypass_graph=1` (#125). Open: the fair light-box comparison against the vendor (#230, Release 1). |

**Completeness** of the vendor API on 2026-11-11 (vendor IMP/SU functions that work, from the "All areas" coverage summary of the OpenIMP feature matrix): T10 323/329 (98.2 %), T20 323/329 (98.2 %), T21 328/333 (98.5 %), T23 439/457 (96.1 %), T30 294/350 (84.0 %), T31 377/401 (94.0 %), T40 372/389 (95.6 %), T41 407/427 (95.3 %). Table: [OpenIMP README, Completeness](https://github.com/Lu-Fi/openimp/blob/next/README.md#completeness).

Per feature and SoC:
the **[website](https://lu-fi.github.io/openimp/)** (status per SoC, light-box protocols open vs vendor, trend per camera), the rendered **[feature matrix](https://lu-fi.github.io/openimp/feature-matrix.html)** (source: [FEATURE_MATRIX](https://github.com/Lu-Fi/openimp/blob/next/docs/FEATURE_MATRIX.md)).
History: [CHANGELOG.md](CHANGELOG.md).

## T23 module parameters (release candidate defaults)

Defaults of `tx-isp-t23.ko` in the release candidate. The switches are `0644` module parameters (changeable at run time through `/sys/module/tx_isp_t23/parameters/`) except where noted; the stock-like set is the one that ran 7 h overnight plus many stream restarts. Parameters marked "debug" exist for bisecting and are not needed in normal operation.

| Parameter | Default | Meaning |
|---|---|---|
| `chan_stop_keep_input` | 1 | Keep the input (CSI, sensor, VIC, TISP core) running after the last frame channel STREAMOFF while the ISP is streaming, as stock does; an on-demand wake then restarts no hardware. 0 stops the input with the last channel (debug; hangs together with `msca_fifo_rearm=1`) |
| `msca_keep_enabled` | 2 | What STREAMOFF does with the MSCA output enable bit: 0 clear, 1 keep when the input stops too, 2 always keep (stock style) |
| `msca_fifo_rearm` | 0 | 1 clears and refills a stopped channel's MSCA address FIFO at STREAMON (debug; stock never does) |
| `msca_flip_skip_noop` | 1 | No MSCA update request for unchanged mirror/flip bits |
| `msca_restart_skip` | 1 | No MSCA reload on STREAMON of a still-enabled output with unchanged geometry |
| `msca_session_release` | 1 | An ISP STREAMON with the input stopped switches off MSCA outputs left enabled and clears their FIFOs |
| `msca_scratch` | 1 | STREAMOFF parks a kept MSCA output on a scratch area at the tail of the ISP buffer (900 KiB at 720p), QBUFs of a parked channel are held until STREAMON, the input waits until the scratch address was consumed. Read-only counters `msca_scratch_parks`, `_frames`, `_deferred`, `_skips`, `_settle_timeouts`, `_settle_ticks_max`. 0 = off (first thing to try if a stop/start hangs) |
| `chan_stop_drain` | 21 | STREAMOFF waits up to N x 10 ms until the channel's queued buffers have left the hardware (stock: 21); 0 = no wait. Counters `chan_drain_waits`, `chan_drain_timeouts`, `chan_drain_ticks_max` |
| `qbuf_cache_inv` | 1 | QBUF invalidates the buffer's cache lines before its addresses reach the FIFO (as stock); 0 = off (debug) |
| `source_af` | 1 (read-only) | AF statistics chain of the stock module: AF block, AF interrupt and the AF getters/setters. 0 = AF off and the controls unrouted. No measurable CPU or interrupt cost |
| `source_ae_oem` | 1 (read-only) | Lifted vendor AE (default since 2026-10-03) |
| `mdns_ref_hold` | 0 | Bit 0 holds the temporal-NR (MDNS) reference weights and gates at their maximum and writes `b_min` as 0xffffffff, so a picture an application wrote into the reference stays visible (OpenIMP test pattern mode 5 sets it while it runs). The same parameter exists on T41 (bit 0, same meaning) |
| `crumbs` | 0 | Hang step markers in a reserved page (1 = rmem page after the MDNS buffer, 2 = page at `crumb_addr`); only for hang analysis |
| `t23_runtime_trace` | 0 | 1 enables informational driver logging |
| `fcrop_upscale_pct` | 0 | Percentage by which the front-crop geometry check tolerates an MSCA upscale (0 = no upscaling allowed). Measured on T23: upscaling up to 2.0 works, at 2.2 the MSCA stalled briefly and the crop was dropped without a reboot. The same parameter exists in `tx-isp-t31.ko` (writable under `/sys/module/tx_isp_t31/parameters/`), where the default is 100 (2.0x) since 2026-10-24 |
| `msca_flip_state` | read-only | Who flips each axis, as a bit mask: 1 sensor_h, 2 sensor_v, 4 isp_h, 8 isp_v. The sensor bits are set when the sensor accepted the flip event (sc2336p: register 0x3221), the ISP bits follow the MSCA mirror/flip word (0xd050). With image hflip and vflip both stages flip on T23 (`shvflip=1` only flips the lens-shading mesh), so an application that mirrors a crop window must apply both. Same parameter in `tx-isp-t31.ko` (bits for the sensor flip request of `apical_isp_hvflip_update` and the MSCA mask) |
| `isp_clk`, `isp_clka` | 153000000, 416000000 (load time only) | ISP core and AXI clocks in Hz; since the candidate they really reach the hardware (before, the activation path read the wrong clock-table slot) |

## T10/T20 module parameters

`tx-isp-t10.ko` and `tx-isp-t20.ko` share one firmware unit. Parameters added with the safe anti-fog preset and the lens-shading scale (host-tested; device test pending):

| Parameter | Default | Meaning |
|---|---|---|
| `t20_antifog_variance` | 0 (`0644`) | 1: the anti-fog presets also write the Iridix variance nibbles (register 0x3c8) at the frame-start commit of Iridix and restore the original value on "off"; 0: strength only. Leave it at 0 until the strength-only path has run on the camera |
| `t20_ae_flip_guard` | 1 (`0644`) | Beyond vendor (default on since 2026-11-11, #267): a one-step change of the IQ AE-correction curve is held until the exposure moved 1/16 stop; 0 restores the vendor behaviour. See [docs/BEYOND_VENDOR.md](docs/BEYOND_VENDOR.md) |
| `t20_antifog_level` | read-only | Last accepted anti-fog level: 0 off, 1 strong, 2 medium, 3 weak |
| `t20_antifog_ignored` | read-only | Anti-fog requests that found the Iridix block disabled (nothing is written then) |
| `t20_mesh_scale_cur` | read-only | Lens-shading scale last written by the `SetMeshShadingScale` control (255 = none yet) |

## Better than the vendor driver

- Reload and stop: 10 stop/start and rmmod/insmod cycles without an oops on T20/T21/T23/T31/T41 (T10: 5 cycles).
- T40: rmmod resets the VICs and switches the AE/AWB statistics off before their buffers are freed, so a sensor that keeps sending frames cannot make the ISP write into freed memory (the vendor frees them with the statistics running).
- T20/T10 give back about 8 MB RAM (unused 8 MiB V4L2 frame pool off by default); on a T20 the open stack measured 10.5 % application CPU against 25.2 % with the vendor stack, snapshots 0.20 s against 0.45 s.
- Smaller modules than the vendor on T21, T23 and T31.
- Checked inputs (all user copies, QBUF window, bounded waits), sensor module pinned while streaming.
- T21 controls and noise reduction act (the vendor ignores them), T10/T20 noise-reduction strength acts.
- Shortfall logging with a concrete parameter value when reserved memory is too small.

Details and application integration notes:
[OPENIMP_BEYOND_VENDOR](https://github.com/opensensor/openimp/blob/aperto/docs/OPENIMP_BEYOND_VENDOR.md).

## Build

Out-of-tree kernel modules, built against a Thingino buildroot output (toolchain and vendor
kernel tree, Linux 3.10.14 for T10-T31, 4.4.94 for T40/T41). The helper picks the SoC with `SOC`:

```sh
SOC=t31 ./build_local.sh          # t10 t20 t21 t23 t30 t31 t40 t41
TH=/path/to/thingino-firmware SOC=t23 ./build_local.sh
```

`TH`, `ROOT`, `KDIR`, `CROSS` and `ARCH` can be overridden (see the comments in `build_local.sh`).
It builds `driver/<soc>/` into `tx-isp-<soc>.ko` plus the diagnostic `driver/tx_isp_trace.ko`; when no
module for the SoC was built locally the result is a compile baseline only. The T20 driver expects the
`external/ingenic-sdk` submodule (`git submodule update --init`). Host tests for kernel-independent
primitives: `make -C tests check`.

## Integration in Thingino

The packages `open-tx-isp` (this driver) and `openimp` (userspace) are in the upstream
[thingino-firmware](https://github.com/themactep/thingino-firmware) branch `aperto`
([#1756](https://github.com/themactep/thingino-firmware/pull/1756)), selected with
`BR2_PACKAGE_THINGINO_ISP_OPEN` (menu "ISP stack"). The Lu-Fi `aperto` branch of thingino-firmware pins both
packages to the tag `v2026.11.07-rc3` (sources `Lu-Fi/open-tx-isp` and `Lu-Fi/openimp`); the streamer package is pinned separately; the upstream
pull request for RC3 is [#1945](https://github.com/themactep/thingino-firmware/pull/1945) (until it is merged, the
upstream `aperto` branch pins `v2026.10.25-rc2`). The module is installed as `tx-isp-<soc>.ko` and replaces the proprietary one;
SDK sensor, audio and AVPU modules stay. The kernel VPU/rmem stability patches
([#1748](https://github.com/themactep/thingino-firmware/pull/1748),
[#1752](https://github.com/themactep/thingino-firmware/pull/1752)) are merged there. The optional
boot guard `BR2_PACKAGE_THINGINO_ISP_GUARD` ([#1749](https://github.com/themactep/thingino-firmware/pull/1749),
default off; `isp_open=auto|manual|off`) skips the ISP/sensor modules after an unstable load so a bad
driver cannot boot-loop the camera. thingino pins a release tag, not a commit SHA.

## Branches and releases

- `main`: the original author's line (opensensor). It is left untouched and is a strict ancestor of `aperto`.
- `aperto`: the open stack release line: OpenIMP + open-tx-isp, as used by the thingino `aperto` branch. Fast-forward only; every commit was flashed and checked on cameras. Releases are tagged `vYYYY.MM.DD` on this branch, and thingino pins a tag instead of a SHA.
- Development and the device-test campaign happen in the [Lu-Fi forks](https://github.com/Lu-Fi/open-tx-isp) (branch `next` = integration, `claude/<topic>` = topic branches); tested work reaches `aperto` from `next` after a clean soak.
- Companion repository: [opensensor/openimp](https://github.com/opensensor/openimp) (branch `aperto`). Both repositories are released together; use matching tags.

## Documentation

- [OpenIMP wiki](https://github.com/Lu-Fi/openimp/wiki) (the whole open stack): module parameters, memory (rmem, ispmem, MMAP pool), troubleshooting, install and boot guard, release scheme; kernel-driver view: [open-tx-isp wiki](https://github.com/Lu-Fi/open-tx-isp/wiki).
- [`docs/T31_ISP_ARCHITECTURE.md`](docs/T31_ISP_ARCHITECTURE.md): hardware and driver architecture
- [`docs/ISP_SOC_ALGORITHM_VARIANCE.md`](docs/ISP_SOC_ALGORITHM_VARIANCE.md): algorithm differences between SoCs
- [`docs/DRIVER_REUSE_PLAN.md`](docs/DRIVER_REUSE_PLAN.md), [`docs/SHARED_DRIVER_LIBRARY.md`](docs/SHARED_DRIVER_LIBRARY.md): shared code
- [`docs/IMAGE_TUNING_PRD.md`](docs/IMAGE_TUNING_PRD.md): image tuning plan
- [`docs/ISP_PERFORMANCE_BENCHMARK.md`](docs/ISP_PERFORMANCE_BENCHMARK.md): on-device CPU/memory baseline
- [`docs/V4L2_CAPTURE_PATH.md`](docs/V4L2_CAPTURE_PATH.md): V4L2 capture architecture
- [`docs/INTERRUPT_DEBUG_GUIDE.md`](docs/INTERRUPT_DEBUG_GUIDE.md): interrupt debugging
- [`docs/re/`](docs/re): reverse-engineering dumps (T31 vendor module HLIL)
- Per SoC: `driver/t32/README.md` and `driver/t33/README.md` (planned, QEMU only), `driver/t10/README.md`, `driver/t20/README.md`, `driver/t21/README.md` (+ `COMPARATIVE_ANALYSIS.md`), `driver/t23/README.md`, `driver/t30/README.md`, `driver/t31/README.md`, `driver/t40/README.md`, `driver/t41/README.md`; T31 IPU (OSD blend, kernel 7): `driver/ipu/README.md`

Layout: `driver/<soc>/` per-SoC driver, `driver/common/` and `driver/include/tx_isp/` shared code and
interfaces, `docs/` notes, `tests/` host tests and oracle checks, `tools/` on-device probes and generators,
`sensor-src/` sensor sources used by the T10/T20 builds.

## Reporting problems

Open an issue at [opensensor/open-tx-isp](https://github.com/opensensor/open-tx-isp/issues) (kernel driver, ISP, memory) or [opensensor/openimp](https://github.com/opensensor/openimp/issues) when unsure. Please include the SoC and sensor, the revisions of open-tx-isp, OpenIMP and the camera application, `dmesg` (including any shortfall line such as `set ispmem >= N KB`), the application log, the stream set and the `rmem`/`ispmem` values. Do not post addresses, credentials or location names. Details: [Troubleshooting](https://github.com/Lu-Fi/openimp/wiki/Troubleshooting#reporting-a-problem).

## Contributing

Contributions are welcome when grounded in OEM binary analysis, `libimp` ABI validation, hardware
logs or captures, tuning/calibration recovery or documentation. For behavioural changes state the OEM
evidence, the files changed, how it was validated and what remains uncertain.

## Acknowledgments

Prior art from the Ingenic / Thingino reverse-engineering community, especially
[thingino-firmware](https://github.com/themactep/thingino-firmware),
[ingenic-sdk](https://github.com/themactep/ingenic-sdk) and
[OpenIMP](https://github.com/opensensor/openimp), and the upstream
[opensensor/open-tx-isp](https://github.com/opensensor/open-tx-isp).

## License

This project is licensed under the GNU General Public License (GPLv3).

---

<sub>Not affiliated with or endorsed by Ingenic Semiconductor. "Ingenic" is used only to name the SoCs this project supports.</sub>
