# agg-39 device and soak plan (draft)

Status: plan only. Nothing has been flashed; the user decides when (and whether) the images go on the
cameras. Device runs follow the usual rules: one camera at a time per lock file
(`locks/<camera>.lock`, the T41 uses `locks/t41.lock`), no pictures leave the machine, no audio tests,
no credentials in logs, kill processes only by PID, and every run ends with the restore check below.

## 1. What is tested

| repo | branch | commit | contents |
|---|---|---|---|
| open-tx-isp | `claude/agg39` | head of the branch (release notes a95deef5) | agg-38 + T41 ch1, tone worker, AWB/EV, WaitFrameDone, audit (+ follow-up 567763b4) + T40 sc830ai |
| openimp | `claude/agg39` | e5fff32 | agg-38 + T41 WaitFrameDone outside the lock, T40 encoder poll deadline (+ test stub) |
| streamer, web UI, firmware tree | as agg-38 | unchanged | (a streamer rebuild is a separate decision, see section 7) |

What actually changes against agg-38:

- **T41** (driver and libimp): MSCA defaults `t41_msca_cfg_update=2`, `t41_msca_stop_disable=1`, ch1 gate
  1280x720; tone worker available but off (`t41_tone_trigger=0`); WaitFrameDone -ETIMEDOUT; audit
  (counters `unresolved_jalr_count`, `irq_func_cb_rejects`).
- **T40** (driver and libimp): new defaults (best line), Bayer phase from the sensor, fast AWB seed,
  sliding 3A window, gamma from the bin, recovery fixes, opt-in `isr_oem_hw_refresh` (off).
- **T10, T20, T21, T23, T30, T31**: driver sources identical to agg-38; the openimp changes are in the
  T40/T41 library only. These cameras run the soak as a regression check (same code, new build).

Module md5 of the host build (unstripped, against each camera's agg-38 kernel tree): T10 5c0afb89,
T20 e6b5c22a, T21 78a89e69, T23 7b93af16, T31 67a89b80, T40 2321e4af, T41 1b5116b0. Image builds
produce their own (stripped) modules; record those md5s in the build summary.

## 2. Cameras

The camera set is the agg-38 production set plus the test cameras (CHANGELOG agg-38, TODO.md).
"Image" means a full agg-39 image (`run39.sh`-style build with `AGG_ISP`/`AGG_OIMP` pointing at the
agg-39 commits); "RAM" means the module and/or libimp are loaded at runtime on the running agg-38 image,
without flashing.

| SoC | cameras | sensor | step 1 (no flash) | step 2 (after user OK) | focus |
|---|---|---|---|---|---|
| T41 | .32 | gc5603 2880x1620 | RAM: agg-39 module + libimp, 30 min | image, soak | MSCA defaults, ch1, counters, WaitFrameDone |
| T40 | .184 | sc830ai 3840x2160 | only if reachable; no rmmod on this board (reboot instead) | image, soak | picture metrics, devtest, kernel partition size |
| T31 | .25 .145 .29 .26 .33 .151 | sc2336 1080p | — | image, soak | regression |
| T31 | .21 .224 | sc4336p | — | image, soak | regression |
| T23 | .23 .28 .124 | sc2336(p) | — | image, soak | regression |
| T23 | .30 | sc1a4t 720p | — | image (its own streamer configuration as before), soak | regression |
| T21 | .24 (open), .27 (vendor reference) | jxf23 | — | image on .24 only; .27 stays vendor | regression, CPU/bitrate against .27 |
| T20 | .107, .163 (pan) | jxf23 / jxf22 | — | image, soak | regression, RSS trend (known slow growth) |
| T10 | .31 | jxh42 | — | image, soak | regression, day/night flapping (known) |

Before any T40 image: the board was lost once to a kernel partition overflow (uImage larger than the
1600k partition, reported by the build without stopping it) and needs the per-IP WLAN configuration.
Check the kernel size and the WLAN overlay of the image before the flash; the board recovers only with
the USB cloner and the backup of 2026-10-12.

## 3. Checks per camera (every soak sample, and before/after each RAM run)

Common to all SoCs:

- `/snapshot.jpg` returns 200 and a plausible size (night and day differ; compare with the camera's
  agg-38 numbers);
- RTSP `OPTIONS` returns `200 OK`; one 20 s stream pull counts samples per track (fps as configured,
  e.g. 25 fps -> about 500 samples on track 1);
- `dmesg` since the start: no `Oops`, `BadVA`, `Unhandled kernel unaligned access`, `BUG`,
  `Call Trace`, `end trace`, `Unknown symbol`, `Unknown parameter`;
- uptime keeps rising (no reboot; on the T41 respect the isp-guard rule: never two reboots within
  300 s of uptime);
- the streamer keeps its PID; `free` and the streamer's VmRSS sampled hourly (trend over 6 h, flag
  growth above 1 MB);
- `lsmod` shows the ISP and sensor modules; the module md5 matches the build summary.

T41 (.32) in addition:

- `/sys/module/tx_isp_t41/parameters/unresolved_jalr_count` and `irq_func_cb_rejects` stay 0
  (if `unresolved_jalr_count` rises, keep the `unresolved jalr from ...` lines from dmesg);
- `t41_msca_cfg_update` reads 2, `t41_msca_stop_disable` 1, `t41_tone_trigger` 0;
- ch1 pulls (`/snapshot.jpg?chn=1`, 20 s stream of channel 1) at the configured size;
- WaitFrameDone check with the devtest `isptest wait` while a client pulls channel 0 (ret 0, about
  40 ms per wait at 25 fps) and once without a client (-ETIMEDOUT / -145, counter unchanged); the
  devtest still marks the second case as FAIL until it learns the new return code;
- 10 streamer restarts and 5 client-driven idle stops/starts (RTSP client connects and leaves) without
  a hang: the RTSP idle path was not covered by the ch1 runs.

T40 (.184) in addition:

- `t40_devtest` mask 5: 24 or 35 results, 0 FAIL (the known BcshHue cell may fail as on agg-38);
- picture numbers from one frame (numbers only): frame Y (best line gave 184.5 by day), neighbour
  difference (3.15), full-frame U/V (near 128/128), TOP word 0x7FFF08D8, register 0x88 = 0x101;
- `unresolved_jalr_count` 0, no `system_irq_func_set ... out of range` line;
- encoder poll with no frames returns within its deadline (openimp T40 poll change).

T21 (.24 vs .27): CPU of the streamer process and the substream bitrate over 10 min with the same
configuration, as in the agg-38 comparison.

## 4. Sequence

1. **Host (done):** builds of all SoCs with `check_ko_symbols.sh`, `make -C tests check` (91/0),
   openimp `tests/t31`, `tests/t40`, `tests/t41`, T40/T41 library builds.
2. **T41 RAM run (needs the user's OK):** agg-39 module (and libimp per swap) on .32 at runtime,
   30 min with periodic pulls, the T41 checks of section 3, then restore (section 6).
3. **Images (needs the user's OK):** build per camera, verify each with the `verify39.sh` checks
   (image size, rootfs free space, kernel partition, libimp/ko md5, overlay files, keys present —
   the script prints no credential contents).
4. **Flash (user decision):** production cameras in two groups so that half of each SoC stays on
   agg-38 until the first group has run 1 h clean; .27 stays on the vendor stack.
5. **Overnight soak, 6 hours (our rule):** start in the evening, all flashed cameras together under
   the soak monitor used for agg-38; samples every 5 min for snapshot/RTSP/uptime, hourly for memory
   and the SoC-specific checks; the T41/T40 checks of section 3 at the start, after 3 h and at the end.
   A soak counts as CLEAN when every camera passes every sample, no oops, no reboot, no streamer
   restart by the watchdog, memory trend within the limit.
6. **Daylight follow-up:** colour items that need light (T41 white balance and colour verdict,
   OSD/IVS; T40 daylight colours).

## 5. Pass criteria for the aggregate

- all cameras CLEAN over the 6 h soak;
- T41 counters 0, MSCA defaults in effect, no hang on restarts and idle stops;
- T40 devtest without new failures, picture numbers within 10 % of the best-line values by day;
- no regression on T10..T31 against their agg-38 soak numbers.

## 6. Restore

- **RAM runs:** load the flashed module again (`lm.sh flashed`), start the streamer, check the
  module md5 against the camera's agg-38 value (T41: 2cb4b758), snapshot 200, RTSP 200, `lsmod`;
  restore a swapped libimp from its backup and compare md5 (T41 agg-38 libimp c33fb0b2); remove all
  files under `/tmp` that the run copied.
- **Flashed cameras:** the agg-38 images and the backups of 2026-10-12 (`scratch-builds/backup-*`)
  are the fallback; flash back only on the user's decision.
- **T40 board:** no rmmod (the core remove hangs it); reboot instead. If it does not come back,
  recovery only with the USB cloner and the 2026-10-12 backup.

## 7. Open decisions (user)

- `t41_tone_trigger` default: stays 0 in this aggregate unless decided otherwise (day results look
  plausible, no vendor comparison yet).
- Whether `claude/log-quiet` (9d0d7ad0, quieter logs for T31/T41) joins agg-39; it is listed in the
  TODO but not merged into `claude/agg39`.
- Whether the streamer is rebuilt for agg-39 (pending fixes in its own branch) or stays at agg-38.
- The T40 image (kernel size, WLAN overlay) before the board gets agg-39.
