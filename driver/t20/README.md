# Ingenic T20 ISP Recovery

This directory contains the T20 TX-ISP recovery baseline reconstructed from
the OEM `tx-isp-t20.ko` used by the Thingino
`wyze_cam2_t20x_jxf23_rtl8189ftv` profile. It follows the same upstream SDK,
binary-audit, shared-adapter, and hardware-validation split used for the T21
and T30 recoveries.

## Provenance

- OEM module SHA-256:
  `c41ffded823ebd51d8ef4c2227342a1dfdf1cb82e312e5bfc9fb637d01f30298`
- Binary Ninja export SHA-256:
  `28e6b8f01e57180e4bd7fb40ea18fca1b889c73fd0906add4cbda2e45c1c8a83`
- Automated recovery: 740 candidates, 731 isolated compiles, and 53
  relocation-normalized exact matches.

The generated whole-driver source initially failed its aggregate compile.
The integrated source now clean-builds, links, loads non-persistently on a
T20X device, and streams through stock `libimp.so`, OpenIMP, and direct V4L2
capture.

## Source partition

The module deliberately does not rebuild all 740 generated functions:

- 16 unmodified translation units come directly from the tracked Ingenic T20
  SDK;
- eleven repaired SDK-derived translation units (since 2026-11-06 also
  `tx-isp-csi.c`, `tx-isp-vic.c` and `apical-isp/tx-isp-load-parameters.c`:
  clock-init/NULL-attr guards, rate-limited VIC error print, single
  `filp_close`, checked IQ-bin read, vmalloc tables) and their two modified private
  headers are versioned under `sdk/source/`, so the open T20 implementation is
  owned and reviewable in this repository rather than hidden in SDK submodule
  changes;
- the sensor registry uses `driver/common/tx_isp_sinfo.c` through a T20 layout
  adapter;
- five relocation-normalized T20/T30 fixed-point matches use the shared math
  core; and
- 477 archive-backed firmware functions remain in the recovered T20 unit.

`sdk/tx-isp-core-local.h` is an ABI invariant, not a convenience include. It
ensures every mixed local/unchanged SDK translation unit sees the same repaired
`tx_isp_core_device` layout before the legacy SDK header can claim its include
guard. Mixing those definitions moves the day/night work item and later fields
by 16 bytes; device testing showed that split as DMA capture stopping after 25
frames.

The OEM `stab` object and the 12,240-byte `__fw` object are the canonical
firmware storage. Generated absolute-address aliases were collapsed into
those objects. This reduced module BSS from 529,696 bytes to 38,128 bytes;
the OEM module uses 34,016 bytes.

`extract_firmware.py` is a baseline extractor, not a source generator for the
reviewed final unit. It refuses to overwrite the checked-in repaired source
unless `--force` is given; intentional regeneration must reapply and audit the
one-off repairs recorded under `audit/`.

## Build

Build against a compatible Thingino T20 Linux 3.10.14 output:

```sh
ROOT=/path/to/thingino/output/t20-camera-3.10.14-uclibc
KDIR="$ROOT/build/linux-<revision>"
CROSS="$ROOT/per-package/ingenic-sdk/host/bin/mipsel-linux-"

make -C "$KDIR" M="$(pwd)/driver/t20" \
  ARCH=mips CROSS_COMPILE="$CROSS" modules
```

Expected artifact: `driver/t20/tx-isp-t20.ko`.

The August 29, 2026 device-tested checkpoint is an ELF32 little-endian MIPS32
R1 module with the expected Linux 3.10.14 vermagic. Rebuild locally before
comparing an artifact hash; the module is intentionally not checked in.

## Binary audit

```sh
regtrace audit-binary \
  --oem /path/to/oem/tx-isp-t20.ko \
  --recovered driver/t20/tx-isp-t20.ko \
  --objdump "${CROSS}objdump" \
  --replacement-map driver/t20/audit/replacements.json \
  --out driver/t20/audit
```

The current audit matches 721 of 742 OEM functions: 711 by name and 10 by a
documented replacement grouping. There are zero stub and zero collapsed
findings. Replacement groups account for OEM compiler inlining across SDK
dispatcher/helper boundaries; they do not hide missing symbols. The only real
first-pass collapse, `cmos_fsm_process_interrupt`, was restored from 13 to 640
instructions against the common Apical state/interrupt contract.

The shared T10 integration also made three audit-sensitive paths explicit:
the complete 140-entry API-to-firmware map, the per-FSM interrupt critical
sections, and all 16 interrupt-event register routes. Both T10 and T20
clean-build with those common repairs, and the refreshed T20 audit retains
zero stub and zero collapsed findings.

## Device validation

The Ingenic T20 and T30 OEM modules share 618 named functions; 133 of those
have identical relocation-normalized instruction streams in the current
reference set. Shared code is accepted only where SDK source, normalized
object code, or both prove the ABI and behavior. T20-only V4L2 topology,
register programming, calibration, and firmware policy remain local.

The same reconstructed driver passed three surgical, RAM-only device gates on
August 29, 2026:

1. stock `libimp.so` produced a clean processed frame;
2. OpenIMP produced clean 640x360 H.264 at 25 fps; and
3. The application captured the ISP V4L2 nodes directly and produced clean 640x360
   H.264 at 25 fps.

The direct-V4L2 and OpenIMP reference frames measured SSIM 0.985870 in the
tested scene. Every cycle preserved the stock module and sensor module, staged
the open components under `/tmp`, captured bounded logs and frames, and
rebooted to restore the stock stack.

The subsequent full-resolution application gate used the repository-owned T20
driver and the generic two-buffer V4L2 handoff. It captured 120/120 H.264 High
frames at 1920x1080 and 25.2 fps over IPv6 RTSP/TCP, passed strict decode with
zero source or publish drops and zero ISP IRQ errors, and sustained 3.06 Mbps
against a 3 Mbps request. The driver reserves an 8 MiB sensor-neutral MMAP
pool so two page-aligned 1080p NV12 buffers can overlap capture and encoding;
the vendor 4 MiB default fits only one such frame.

This is evidence from one Wyze Cam V2 for the T20X/JXF23 linear-daylight path,
not a universal sensor or mode claim. Night/IR, WDR, additional sensors, and
long-duration streaming remain open validation work. Runtime image policy is
sensor-neutral: active IQ calibration tables own AE targets, AWB/denoise
response, lens shading, and Iridix behavior; sensor-specific tuning does not
belong in this driver.

Several early-initialization trace messages remain intentionally present. A
build differing from the three-gate artifact only by removal of those messages
rebooted before the userspace-ready marker, so their timing effect must be
isolated with IRQ/state instrumentation before they can be removed safely.

## AE / AWB owner and the firmware thread (T10 / T20)

- `t20_simple_ae` (default 0 since 2026-11-17, openimp #609; before 1): `t20_simple_ae=0` runs the recovered OEM AE graph (AE statistics event 1 -> `ae_calculate_exposure` -> exposure event 8 -> CMOS allocators -> exposure history -> sensor writes at frame start), as the vendor firmware does. `t20_simple_ae=1` selects the compact histogram AE instead.
- `t20_simple_awb` (default 1): compact AWB; `=0` runs the OEM AWB graph. The compact AWB waits for 4 stable frames of the compact AE before its first statistics update (`t20_awb_feedback_ready`); with the OEM AE (`t20_simple_ae=0`) that gate does not apply, as the vendor AWB has none (before openimp #609 the gate stayed closed there: static WB, colour temperature / CCM / LSC frozen). The compact AWB runs `color_matrix_update` (CCM, saturation, LSC banks / blend) from its interrupt only while the firmware thread is parked; otherwise the colour-matrix FSM in the thread does it on the frame-end event 11, as in the vendor firmware (openimp #657: the irq preempted the thread's update and tore the CCM / LSC state for a frame).
- `t20_park_fw_after_first_pass` (default 1; no effect with the default `t20_simple_ae=0`): the firmware thread (`isp_fw_process`) sleeps after its first pass only while both compact controllers are active (`tx_isp_t20_fw_may_park()`); the OEM AE or AWB need the thread running, as in the vendor SDK (it wakes about every 10 ms). Before openimp #609 the gate ignored `t20_simple_ae`, so `t20_simple_ae=0` alone left the sensor at the init exposure (integration time 4, gain 0, black picture).
- `t20_simple_ae`, `t20_simple_awb` and `t20_park_fw_after_first_pass` are load-time parameters (read-only in sysfs, `0444`): set them at `insmod` / modprobe. A parked firmware thread leaves its loop, so a runtime switch to the OEM AE or AWB found no thread to consume the firmware events (stale exposure, openimp #627). The vendor has no such switch.
- With `t20_simple_ae=0` the compact NR / sharpening / CCM / Iridix calls of the compact AE frame path do not run; the OEM FSMs do that work from the firmware events (`t20_simple_nr` then has no effect).
- Host check: `make -C tests/t20_fw check` runs the scenarios `oem-ae` (OEM AE + compact AWB / NR) and `oem-ae-park` (the same with the park gate) and fails when the OEM AE never moves the integration time, or when the compact AWB under the OEM AE stays at the init colour temperature / never drives the CCM (openimp #609).
