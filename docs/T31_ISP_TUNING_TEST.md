# T31 ISP tuning: test guide for the branch `claude/t31-isp-tuning`

This guide is for a local Claude session with SSH access to
a T31 camera with thingino and open-tx-isp. It checks the corrections of the
tuning controls (bug IDs B2–B17 from the gap analysis) without flashing and
if possible without picture comparison.

## 0. Rules

- **Never flash**: no `sysupgrade`, no `flashcp`, no `fw_setenv`.
- **Write nothing to `/etc`, `/opt` or any other persistent directory.**
  Everything that goes onto the camera lives in `/tmp` (tmpfs).
  Only read `/etc/modules.d/*`.
- The installed module in flash stays unchanged. A `reboot` or
  power cycle restores the initial state completely.
- **Record pictures, snapshots or streams only with the person's explicit
  consent.** All tests below manage with registers, ioctl return values
  and `dmesg`. Some tests briefly change the live picture (noted); that
  is fine as long as nothing is recorded.
- Inform the person before the module swap. If the camera hangs afterwards, it may
  have to be powered off by hand.

Placeholders: `CAM` = host name/IP of the camera, `TH` = local checkout of
`thingino-firmware` with a **built** output for exactly this
camera profile, `ROOT` = `$TH/output/<profile>`.

## 1. Check the prerequisites

On the camera (read only):

```sh
ssh root@CAM 'uname -r; cat /proc/cpuinfo | grep -i "machine\|system type"; lsmod'
ssh root@CAM 'cat /etc/modules.d/20-isp; cat /etc/modules.d/30-sensor* 2>/dev/null'
ssh root@CAM 'ls /etc/init.d/ | grep -Ei "stream|rtsp|video"'
ssh root@CAM 'modinfo tx_isp_t31 2>/dev/null || find /lib/modules -name "tx-isp-t31.ko"'
ssh root@CAM 'cat /proc/jz/isp/isp-m0; which devmem'
```

Continue only if:

- the SoC is a T31 and `tx_isp_t31` is loaded,
- the camera already uses open-tx-isp (thingino package `open-tx-isp`;
  `/proc/jz/isp/isp-m0` shows the lines `ISP Runing Mode : ...` from
  thingino patch 0005). Do not test with the stock blob, there the
  comparison state is missing.
- `devmem` is present.

Note down: streamer init script (e.g. `/etc/init.d/S95<app>`), module parameters from `20-isp`, sensor module and parameters from
`30-sensor*`, `uname -r`.

## 2. Build the module (local)

The branch is built with the same thingino patches that the
installed module contains (0001–0005), otherwise e.g. the
`isp-m0` fields are missing.

```sh
cd /path/to/open-tx-isp            # checkout with the branch
B=/tmp/itx-test; rm -rf $B; mkdir -p $B
git archive claude/t31-isp-tuning | tar -x -C $B
for p in $TH/package/open-tx-isp/*.patch; do (cd $B && patch -p1 < $p) || echo "ERROR: $p"; done
cd $B && TH=$TH ROOT=$ROOT SOC=t31 ./build_local.sh
ls -l $B/driver/t31/tx-isp-t31.ko
```

Check that the kernel version fits:

```sh
strings $B/driver/t31/tx-isp-t31.ko | grep '^vermagic='   # must match `uname -r` of the camera
```

Build the test program (static, so that the camera's libc does not matter):

```sh
$ROOT/host/bin/mipsel-linux-gcc -static -O2 -Wall -o $B/t31_isp_tune_test $B/tools/t31_isp_tune_test.c
```

Transfer (thingino dropbear often has no sftp, hence via `cat`):

```sh
ssh root@CAM 'cat > /tmp/tx-isp-t31.ko' < $B/driver/t31/tx-isp-t31.ko
ssh root@CAM 'cat > /tmp/t31_isp_tune_test && chmod +x /tmp/t31_isp_tune_test' < $B/t31_isp_tune_test
```

In the following, `T=/tmp/t31_isp_tune_test` on the camera.

## 3. Baseline values with the installed module

The streamer runs normally. Note the values (they serve as comparison):

```sh
T=/tmp/t31_isp_tune_test
$T expr
$T get 0x98091b; $T get 0x8000086; $T get 0x980918; $T get 0x8000028; $T get 0x80000e0
for a in 0x1330000c 0x13304808 0x13304848 0x13304860 0x1330885c 0x13308860 0x13308864 0x13308868 \
         0x13305004 0x13305008 0x1330500c 0x13305010 0x13305014 0x13340000 0x13340004; do
  echo "$a $(devmem $a 32)"; done
dmesg | tail -20
```

## 4. Swap the module (only `/tmp`, nothing persistent)

1. Safeguard for this session (volatile, valid only until reboot):
   ```sh
   echo 1 > /proc/sys/kernel/panic_on_oops; echo 10 > /proc/sys/kernel/panic
   ```
   An oops in the test module then leads to a reboot with the module from flash.
2. Stop the streamer, e.g. `/etc/init.d/<app-init> stop` (or the script found in
   section 1). Check that no process has `/dev/isp-m0`,
   `/dev/tx-isp` or `/dev/framechan*` open any more:
   ```sh
   ls -l /proc/[0-9]*/fd 2>/dev/null | grep -E 'isp|framechan|video' || echo free
   ```
3. Unload the sensor and ISP modules (names from section 1):
   ```sh
   rmmod sensor_<model>_t31 && rmmod tx_isp_t31
   ```
   If `rmmod` fails: **do not force it**, `reboot` and stop.
4. Load the test module and sensor, with the same parameters as in
   `/etc/modules.d/20-isp` or the sensor line noted in section 1:
   ```sh
   insmod /tmp/tx-isp-t31.ko $(sed 's/^[^ ]* *//' /etc/modules.d/20-isp)
   modprobe <sensor line from 30-sensor*, i.e. module name plus parameters>
   ```
   `modprobe` resolves the dependency of the sensor against the already loaded
   `tx_isp_t31` from `/tmp`.
5. Start the streamer, wait 10–20 s, check `dmesg | tail -50` for oops/errors,
   `cat /proc/jz/isp/isp-m0` must show plausible values. The streamer
   must run: AE, statistics and register refresh work only with a running
   pipeline. The test program opens `/dev/isp-m0` as a second handle; this
   is uncritical with this driver (closing only frees its own
   ioctl buffer).

**Way back at any time:** `reboot`.

For stable register comparisons (tests 5.8–5.10), freeze AE and release it again at the end:

```sh
$T set 0x8000034 1    # AE holds the current tuple
...
$T set 0x8000034 0
```

## 5. Tests

The program shows failures as `ioctl ... ret=-1 errno=N (...)`. After
every block check `dmesg | tail` for `Oops`, `BUG` or `Unable to handle`.

### 5.1 Logging (pr_info → pr_debug)

`dmesg -c >/dev/null; $T set 0x98091b 128; dmesg` – no line
`Set control: cmd=...` may appear (only "ISP M0 device open/release" from
opening the device).

### 5.2 B7/B15: no more kernel stack in the zone getters

```sh
$T getptr 0x8000046 900 | sort | uniq -c   # AF zones: only zeros
$T getptr 0x8000030 900 | head             # AE zones: real values, no a5 pattern
$T getptr 0x8000030 900 | md5sum; sleep 2; $T getptr 0x8000030 900 | md5sum   # changes with the scene
$T getptr 0x8000045 20                     # 20 zero bytes, ret 0
```

Expected: 0x8000046 and 0x8000045 only `00`; 0x8000030 contains neither
random kernel pointers (`8xxxxxxx` pattern) nor `a5 a5 ...`.

### 5.3 B2/B3: SetAe_IT_MAX, GetExpr, SetAeMin

```sh
$T expr                    # it, it_min, it_max, line_us; it_max must be > 0
$T get 0x8000032           # equal to it_max
```

Set a cap below the current integration time (example: `it` = 1200 → 600):

```sh
$T set 0x8000032 600
$T get 0x8000032           # 600
$T expr                    # it_max=600
sleep 3; $T expr           # it <= 600
$T getptr 0x8000026 24     # EV attr: again/dgain (log2, 32 = 2x) rise to compensate
$T set 0x8000032 0         # must fail with errno 22 (EINVAL)
$T set 0x8000032 <old it_max>
```

If the scene is so bright that `it` is already very small, set the cap to about
half of the current `it`; in a dark scene the effect is clearer.
Note: As in stock, an FPS change or a day/night switch resets the
cap to the sensor limit. With patch 0005, `/proc/jz/isp/isp-m0` shows
the sensor limit (`sensor->attr`), not the AE cap; `expr` is authoritative.

If the application already has `image.ae_it_max_us` configured:
in the application log, "capped AE at … lines" and later "cap in effect" must now appear instead of "GetExpr gave no line/max reference". Do
**not** change the configuration for this if it would be written to `/etc`.

SetAeMin (16 bytes: it_min, ag_min, it_short_min, ag_short_min):

```sh
$T getptr 0x800002f 16                    # note the initial values
$T setptr 0x800002f 4,0x400,0,0
sleep 1; $T getptr 0x800002f 16           # 04 00 00 00 00 04 00 00 ...
$T expr                                   # it_min=4
$T setptr 0x800002f 0,0x400,0,0           # it_min 0 is rejected (dmesg warning), ret 0
$T setptr 0x800002f <old values>
```

### 5.4 B14: range check SetMaxAgain

```sh
$T get 0x8000028                 # note the value (log2, 32 per f-stop)
$T set 0x8000028 0xffffffff      # must fail (errno 1, EPERM, like stock -1)
$T set 0x8000028 <old value>     # ret 0
```

### 5.5 B12: anti-flicker with a negative value

```sh
$T get 0x980918                  # note (0 off, 1 = 50 Hz, 2 = 60 Hz)
$T set 0x980918 0xffffffff       # errno 22
$T set 0x980918 3                # errno 22
$T set 0x980918 <old value>
```

### 5.6 Gap 8: anti-flicker after SetSensorFPS

Briefly changes the frame rate of the stream.

```sh
$T get 0x80000e0                 # packed: (num << 16) | den, e.g. 0x190001 = 25/1
$T set 0x980918 1                # 50 Hz
dmesg -c > /dev/null
$T set 0x80000e0 0x140001        # 20 fps
dmesg | grep T31_DEFLICK         # new line directly after the set, total/last_idx for the new frame length
$T set 0x80000e0 <old value>; $T set 0x980918 <old value>
```

Before (without the fix), no `T31_DEFLICK` line appeared after the FPS change.
Whether `step` changes depends on whether the sensor regulates the frame rate via the
line count (then `step` stays the same, `total` changes) or via the
pixel clock.

### 5.7 B9: SetModuleControl via pointer

```sh
devmem 0x1330000c 32
$T getptr 0x80000e2 4            # word W (little endian); W & 0x7ffff == register & 0x7ffff,
                                 # bit 31 set if MDNS is off
$T setptr 0x80000e2 <W>          # write the same word back
devmem 0x1330000c 32             # unchanged, picture unchanged
```

Do not set any other bits – that switches off ISP blocks.

### 5.8 B4: sharpness idempotent (freeze AE, keep the scene quiet)

```sh
$T set 0x8000034 1
r() { for a in 0x13304808 0x13304848 0x13304860; do printf '%s ' $(devmem $a 32); done; echo; }
$T set 0x98091b 128; r           # base A
$T set 0x98091b 200; r           # B
$T set 0x98091b 200; r           # must be equal to B (before: kept rising)
$T set 0x98091b 128; r           # must be equal to A
$T set 0x98091b 60;  r; $T set 0x98091b 60; r   # equal twice
$T set 0x98091b <old value>
```

### 5.9 B5: sinter (2D NR) idempotent and effective immediately

```sh
s() { for a in 0x1330885c 0x13308860 0x13308864 0x13308868; do printf '%s ' $(devmem $a 32); done; echo; }
$T set 0x8000086 128; s          # A
$T set 0x8000086 200; s          # B – must differ from A IMMEDIATELY (AE is frozen)
$T set 0x8000086 200; s          # equal to B
$T set 0x8000086 128; s          # equal to A
$T set 0x8000086 <old value>
```

### 5.10 B6: defog strength idempotent

SetDefog_Strength expects a pointer to **one byte**:

```sh
$T get 0x8000039                 # current value
$T setb 0x8000039 200; $T get 0x8000039    # 200
$T setb 0x8000039 200; $T setb 0x8000039 128; $T get 0x8000039
```

The transmission lists exist only in the driver and have no register of their own;
the defog block RAM (`0x13358000…`) depends on the scene. Therefore
only a functional check here (ret 0, getter matches, no oops). A visual
check (200, 200, 128 must look like the initial state) only with
consent and only if defog is active (do not switch it on yourself).

### 5.11 B8/B17: AE weights, histogram, user pointer

```sh
$T getptr 0x800002d 225 > /tmp/w.txt; cat /tmp/w.txt    # 225 bytes, all 00..08
W=$(awk '{for(i=1;i<=NF;i++) printf "%s0x%s", (n++?",":""), $i}' /tmp/w.txt)
$T setb 0x800002d "$W"                                   # ret 0, table unchanged
$T setb 0x800002d "9,$(echo $W | cut -d, -f2-)"           # errno 1 (weight >= 9)
$T getptr 0x800002d 225 | diff - /tmp/w.txt && echo equal
$T getptr 0x8000031 1024 | head                          # 256 x u32 raw histogram, not all 0
$T getptr 0x800002e 16                                   # 4 thresholds, 5 x u16 (sum ~0xffff), 2 bytes
$T setb 0x800002e 16,64,128,192,0,0,0,0,0,0,0,0,0,0,0,0  # ret 0
$T set 0x800002e 0x10            # invalid user pointer: errno 14 (EFAULT), NO oops
dmesg | tail
```

### 5.12 B11: gamma effective immediately

Briefly changes the picture slightly.

```sh
$T getptr 0x800002b 258 > /tmp/g.txt
G=$(awk '{for(i=1;i<=NF;i++) printf "%s0x%s", (n++?",":""), $i}' /tmp/g.txt)
devmem 0x13340000 32; devmem 0x13340004 32
$T setb 0x800002b "$G"; devmem 0x13340000 32     # unchanged
```

Build `G2`: the same list, but the third element (byte 2 = lower byte
of `lut[1]`) raised by 0x10, without overflow beyond 0xff (otherwise take another
small element). It is best to do the conversion in the local session,
busybox awk does not compute reliably with hex.

```sh
$T setb 0x800002b "$G2"; devmem 0x13340000 32    # bits 12..23 changed immediately, without a day/night switch
$T setb 0x800002b "$G"; devmem 0x13340000 32     # initial value again
```

The register contains `lut[1] << 12 | lut[0]`.

### 5.13 B10: manual CCM

Briefly changes the colour saturation (stock behaviour: in the combined path the
saturation flag goes to BCSH, the CCM saturation is set to 1.0).

```sh
$T getptr 0x8000100 40                          # initial attribute
for a in 0x13305004 0x13305008 0x1330500c 0x13305010 0x13305014; do devmem $a 32; done
```

Form the current matrix from the registers: `0x5004 = m1<<16|m0`,
`0x5008 = m3<<16|m2`, `0x500c = m5<<16|m4`, `0x5010 = m7<<16|m6`,
`0x5014 = m8` (16 bits each). Then set manually (word 0 = byte 0 manual,
byte 1 saturation flag):

```sh
$T setptr 0x8000100 0x0101,m0,m1,m2,m3,m4,m5,m6,m7,m8
for a in 0x13305004 0x13305008 0x1330500c 0x13305010 0x13305014; do devmem $a 32; done
```

Expected: the registers correspond to m0..m8 (at most rounding) and stay
constant even with a light change (before: tuning D matrix, user values ignored).
`$T getptr 0x8000100 40` shows byte 0 = 01. Back to automatic:

```sh
$T setptr 0x8000100 0
```

### 5.14 B13: SetAeAttr (manual exposure)

Changes the picture brightness as long as it is active.

```sh
$T expr                                          # note the current it
$T getptr 0x8000035 152 | head -2                # control object, word 0 = 0 (auto)
$T setptr 0x8000035 1,0x800,0x400,<it>,0x400     # manual: IT=<it>, again 2x, dgains 1x
sleep 2; $T expr                                 # it == <it>, stays put
$T getptr 0x8000026 24                           # again field ≈ 32 (log2, 2x)
$T setptr 0x8000035 0                            # back to automatic (all flags 0)
sleep 3; $T expr                                 # AE regulates again
```

Before, AE only froze without taking over the values.

## 6. Finish

```sh
$T set 0x8000034 0      # if AE is still frozen
reboot                  # loads the module from flash again
```

After the reboot, check that `/tmp` is empty and the streamer runs normally.

## 7. Result log (template)

| Test | Bug | Expectation | Result | Note |
|---|---|---|---|---|
| 5.1 | logging | no `Set control:` lines | | |
| 5.2 | B7/B15 | only zeros or real zone values | | |
| 5.3 | B2/B3 | it_max = cap, it ≤ cap, EINVAL at 0, AeMin works | | |
| 5.4 | B14 | EPERM at 0xffffffff | | |
| 5.5 | B12 | EINVAL at -1 and 3 | | |
| 5.6 | gap 8 | new T31_DEFLICK line after FPS change | | |
| 5.7 | B9 | registers unchanged on write-back | | |
| 5.8 | B4 | X, X equal; 128 = initial value | | |
| 5.9 | B5 | effective immediately; X, X equal; 128 = initial | | |
| 5.10 | B6 | ret 0, getter matches | | |
| 5.11 | B8/B17 | 225 weights, round trip, EFAULT without oops | | |
| 5.12 | B11 | LUT register changed immediately and back | | |
| 5.13 | B10 | register = user matrix, automatic back | | |
| 5.14 | B13 | IT/gain fixed, automatic back | | |

Also record: camera model, sensor, streamer, `uname -r`,
commit hash of the branch and all `dmesg` anomalies.
