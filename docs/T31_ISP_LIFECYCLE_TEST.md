# T31 ISP: life-cycle tests on the device

This guide is meant for a local Claude session that reaches a
T31 camera with thingino over SSH. It checks the branch
`claude/t31-isp-lifecycle2` on real hardware:

| Commit (title) | What becomes visible on the device |
|---|---|
| clear the MSCA address FIFO and keep buffers on dropped frames | Stream keeps running after a day/night switch; `kill -9` stops the MSCA channel |
| write the MSCA Y/UV address pair under a per-channel spinlock | no swapped chroma between frames (checkable only indirectly) |
| serialise frame-channel ioctls with the per-channel mutex | no hang, no `blocked for more than 120 seconds` |
| reject QBUF of a buffer the driver still owns | `qbuf: chN buffer N already in use` must never appear with OpenIMP |
| do not reset a frame channel that another fd is using | a second `open()` during the stream does not disturb it |
| report a tisp_channel_stop timeout to STREAMOFF and release | marker `MSCA channel still busy after 3 s` (should be absent) |
| stop a running MSCA channel on STREAMOFF in ISP bypass mode | relevant only with ISP bypass, not tested here |
| do not start the stream from the 0x400456bf frame wait | no visible effect with OpenIMP |
| drop the raw +0x74 store into struct isp_channel | no visible effect; must not make anything worse |
| keep DelSensor from freeing MDNS/WDR buffers under a running ISP | marker `Sensor release: ISP core quiet after N ms` on a clean stop |
| free the statistics DMA pages in tisp_deinit | not reachable on the device (see limits) |

## Rules

- **Do not flash anything.** No `sysupgrade`, no writing to mtd.
- **Write nothing under `/etc`**, not even indirectly. `POST /control`
  writes changed values back to `<app-config>`. Therefore the application runs in
  these tests with a copy of the configuration in `/tmp` (section 3).
  `/etc/modules.d/*` is only read.
- On the camera, write only to `/tmp`.
- **Pictures only with the human's consent.** Do not save, look at or fetch to the PC any snapshots or
  stream data as long as the human has not explicitly allowed it. Checking is done via byte counters, JPEG header bytes
  and dmesg. A snapshot is deleted right after the header check.
- **Never read `0x13309974`, `0x13309a74` or `0x13309b74`.** When read, these registers
  take an entry from the done FIFO. Use `devmem` only on the
  registers named below.
- On an oops, hang or unclear state: save dmesg
  (`dmesg > /tmp/fail.dmesg`, then fetch it to the PC via `ssh … cat`) and reboot the
  camera. A reboot loads the flashed modules again.
- `rmmod`/`insmod` of `tx_isp_t31` loses about 160 KiB of kernel
  memory per cycle (statistics pages and WDR LUT are not
  freed on unloading, already before this branch). At most two or three load cycles, then
  reboot.

## 0. Build the module (PC)

```sh
cd open-tx-isp            # checkout with branch claude/t31-isp-lifecycle2
git log --oneline -12     # the commits above must be included
TH=~/thingino-firmware ROOT=~/thingino-firmware/output/<t31-camera> ./build_local.sh
ls -l driver/t31/tx-isp-t31.ko
```

`ROOT` must be the thingino output of exactly this camera (same kernel,
same module version), otherwise the module does not load. Transfer without sftp:

```sh
CAM=192.0.2.x
ssh root@$CAM 'cat > /tmp/tx-isp-t31.ko' < driver/t31/tx-isp-t31.ko
ssh root@$CAM 'md5sum /tmp/tx-isp-t31.ko'; md5sum driver/t31/tx-isp-t31.ko
```

## 1. Inventory (camera)

```sh
uname -r; lsmod
cat /etc/modules.d/20-isp /etc/modules.d/30-sensor* 2>/dev/null
M=$(find /lib/modules -name 'tx-isp-t31.ko' | head -1); echo $M
grep -c "new subdevice management system" $M   # >0: open-tx-isp is flashed
md5sum <app-config> /etc/modules.d/* > /tmp/etc.md5
grep -E "MemFree|Slab" /proc/meminfo > /tmp/mem.before
which curl wget devmem
```

Prerequisite: The camera already runs open-tx-isp and the OpenIMP-based application
(`/usr/bin/appd`). If `grep -c` is 0, the vendor driver is running. Then
abort and report to the human.

## 2. Stop the streamer, load the test module (camera)

```sh
/etc/init.d/<app-init> stop; sleep 2; pidof appd && echo "appd still running"
lsmod                                 # check "Used by" of tx_isp_t31
set -- $(cat /etc/modules.d/30-sensor*); SENSOR_MOD=$1
rmmod $SENSOR_MOD
rmmod tx_isp_t31
set -- $(cat /etc/modules.d/20-isp); shift
insmod /tmp/tx-isp-t31.ko "$@" isp_day_night_switch_drop_frame_num=6
set -- $(cat /etc/modules.d/30-sensor*); modprobe "$@"
cat /sys/module/tx_isp_t31/parameters/isp_day_night_switch_drop_frame_num   # 6
dmesg | tail -30
```

`isp_day_night_switch_drop_frame_num=6` is set only at `insmod`, not in
`/etc`. The value forces the path that, before the fix, froze the stream after a
day/night switch. If a module cannot be unloaded
(`in use`, hang), reboot and report the result.

## 3. Start the application with a configuration copy (camera)

```sh
cp <app-config> /tmp/app-test.conf
start_app() { appd -c /tmp/app-test.conf >/tmp/app.log 2>&1 & sleep 8; }
stop_app()  { kill -TERM $(pidof appd) 2>/dev/null
                for i in 1 2 3 4 5 6 7 8 9 10; do pidof appd >/dev/null || return 0; sleep 1; done
                echo "appd does not end"; return 1; }
bytes() { curl -s -m ${1:-5} -o /dev/null -w '%{size_download}\n' http://127.0.0.1:8880/stream.mp4; }
post()  { curl -s -X POST http://127.0.0.1:8880/control -d "$1"; echo; }
start_app; bytes 5          # clearly > 100000 expected
```

Requests from `127.0.0.1` need no token. If `curl` is missing,
`timeout 5 wget -q -O - http://127.0.0.1:8880/stream.mp4 | wc -c` works. If
`appd` does not start this way, read `/etc/init.d/<app-init>` (do not change it) and rebuild the missing
preparations by hand in `/tmp`.

dmesg check after every test:

```sh
FAIL='BUG:|Oops|Unable to handle|scheduling while atomic|sleeping function called|blocked for more than|still busy after 3 s|TIMEOUT waiting for channel|already in use|streaming active|streamoff sensor firstly|the sensor is active|still raising interrupts'
INFO='has no ACTIVE QBUF slot|Stopping streaming on release|already open|ISP core quiet after|stopped channel'
check() { dmesg > /tmp/$1.dmesg; echo "== $1"; grep -E "$FAIL" /tmp/$1.dmesg | head; grep -cE "$INFO" /tmp/$1.dmesg; grep -E "$INFO" /tmp/$1.dmesg | sort | uniq -c | sort -rn | head; dmesg -c >/dev/null; }
dmesg -c >/dev/null
```

`dmesg -c` only empties the kernel buffer. Before every emptying, the content is saved in
`/tmp/<test>.dmesg`.

## 4. Tests

Registers (32 bit, read only): `devmem 0x13309804 32` shows in bit n whether
MSCA channel n is running (bits 16..19 are usually set and irrelevant here). `devmem 0x13309808 32`
shows in bit n whether channel n is still working.

### T1: Start/stop loop (20 rounds)

```sh
stop_app
for r in $(seq 20); do
  start_app; b=$(bytes 5); stop_app
  echo "round $r bytes=$b 9804=$(devmem 0x13309804 32) 9808=$(devmem 0x13309808 32)"
done
check T1
```

Expectation: every round clearly more than 100000 bytes. After the stop, bits 0..2 in
`0x13309804` are zero. In `T1.dmesg` there are no FAIL hits. If
the application ends the session cleanly (DisableSensor, DelSensor), there is once per round
`Sensor release: ISP core quiet after N ms` (N is typically two
frame periods, about 80 at 25 fps). If the marker is missing,
the application does not call DelSensor. That is not an error, but note it in the report.
Isolated `has no ACTIVE QBUF slot` directly after a start are legacy from
the done FIFO. Many of them or some in the middle of a run are a finding.

### T2: Day/night loop (20 switches, stream running)

```sh
start_app
curl -s -m 260 -o /dev/null -w 'permanent-client %{size_download}\n' http://127.0.0.1:8880/stream.mp4 > /tmp/t2.client &
for r in $(seq 10); do
  post '{"image":{"running_mode":1}}'; sleep 5; echo "night $r $(bytes 3)"
  post '{"image":{"running_mode":0}}'; sleep 5; echo "day   $r $(bytes 3)"
done
wait; cat /tmp/t2.client
check T2
```

Expectation: Every short fetch delivers data (roughly the bitrate times 3 s). The
permanent client does not break off and delivers data over the whole run time. Before the
fix, the stream with `drop_frame_num=6` froze at the first switch without any
dmesg message. At the end, reset `running_mode` to the value from
`/tmp/app-test.conf`. Do not judge the picture at night; without
consent, no pictures are looked at.

### T3: `kill -9` of the streamer (10 rounds)

```sh
for r in $(seq 10); do
  pidof appd >/dev/null || start_app
  curl -s -m 20 -o /dev/null http://127.0.0.1:8880/stream.mp4 & sleep 5
  kill -9 $(pidof appd); sleep 1
  echo "round $r 9804=$(devmem 0x13309804 32)"
  wait; start_app; echo "new $r $(bytes 5)"
done
stop_app; check T3
```

Expectation: After `kill -9`, bits 0..2 of `0x13309804` are zero, dmesg
shows `Stopping streaming on release` and `stopped channel`. The newly started
application delivers data every time. Known limit: After `kill -9`, sensor,
VIC and MDNS keep running, because no DisableSensor comes. This is not the subject of
this test.

### T4: Snapshot in parallel to the stream (30 fetches)

```sh
start_app
curl -s -m 120 -o /dev/null -w 'client %{size_download}\n' http://127.0.0.1:8880/stream.mp4 > /tmp/t4.client &
for r in $(seq 30); do
  curl -s -m 5 -o /tmp/s.jpg http://127.0.0.1:8880/snapshot.jpg
  echo "snap $r size=$(wc -c </tmp/s.jpg) head=$(od -An -tx1 -N2 /tmp/s.jpg)"
  rm -f /tmp/s.jpg; sleep 2
done
wait; cat /tmp/t4.client
check T4
```

Expectation: Every answer starts with `ff d8` and is a few KiB in size. The
stream client runs through. The JPEGs are not opened and not copied.

### T5: Second `open()` of a running frame channel

```sh
curl -s -m 30 -o /dev/null -w 'client %{size_download}\n' http://127.0.0.1:8880/stream.mp4 > /tmp/t5.client &
sleep 5
for r in 1 2 3 4 5; do exec 3</dev/framechan0; sleep 1; exec 3<&-; sleep 2; done
wait; cat /tmp/t5.client
check T5
```

Expectation: dmesg shows per round `Frame channel 0 already open (1), keeping its
state (streaming=1)`. On closing, **no** `Stopping streaming on
release` may appear. The client runs without a pause. Before the fix, the
second `open()` cleared the slot table of the running stream.

### T6: Idle kick (day/night without client)

The application switches chn0 on for ~500 ms on a `running_mode` POST without a client
and off again (EnableChn/DisableChn). This is a frequent cycle:

```sh
sleep 20      # chn0 must be off when idle: "disabled (idle)" in /tmp/app.log or logread
for r in $(seq 20); do post '{"image":{"running_mode":1}}'; sleep 3; post '{"image":{"running_mode":0}}'; sleep 3; done
echo "afterwards $(bytes 5)"
check T6
```

Expectation: no FAIL hits, afterwards the stream delivers data immediately.

### T7: Memory

```sh
stop_app
grep -E "MemFree|Slab" /proc/meminfo; cat /tmp/mem.before
```

Expectation: No steady growth of `Slab` over the tests. The
one-time difference from loading the test module is normal.

## 5. Clean-up

```sh
stop_app
md5sum -c /tmp/etc.md5          # all "OK", /etc is unchanged
reboot                          # loads the flashed modules, starts <app-init>
```

After the reboot, check `pidof appd` and `bytes 5`.

## 6. Report to the human

- Camera, sensor, `uname -r`, commit state of the module.
- For T1 to T7 each passed or not passed, with byte counts and
  `devmem` values.
- All FAIL hits verbatim, plus the INFO count per test.
- Whether `Sensor release: ISP core quiet after N ms` appeared and which N values.
- Anything unusual when loading or unloading the module.
- Do not attach pictures unless the human has agreed.

## Appendix: dmesg markers

| Marker | Meaning | Expected |
|---|---|---|
| `qbuf: chN buffer N already in use (state S)` | QBUF of a buffer that the driver still holds | never (OpenIMP) |
| `reqbufs: channel N streaming active` | REQBUFS during a running stream, now `-EBUSY` | never |
| `Frame channel N already open (K), keeping its state` | further `open()`, the state is kept | only in T5 |
| `Channel N: STREAMOFF: MSCA channel still busy after 3 s` | `tisp_channel_stop` ran into the timeout | never |
| `Channel N: release: MSCA channel still busy after 3 s` | the same on closing (crash, `kill -9`) | never |
| `tisp_channel_stop: TIMEOUT waiting for channel N` | detail line for the timeout | never |
| `ispcore_frame_channel_streamoff: stopped channel N (0)` | MSCA channel stopped, result 0 | at every stop |
| `Stopping streaming on release` | channel closed without STREAMOFF | only T3 |
| `Please, streamoff sensor firstly!` | DelSensor rejected while the sensor is running | never |
| `the sensor is active, please stop it firstly.` | sensor release rejected while the sensor is running | never |
| `Sensor release: ISP core quiet after N ms` | DelSensor: ISP without interrupt, buffers released | on a clean stop |
| `Sensor release: ISP core still raising interrupts after 1 s` | ISP keeps reporting interrupts although the sensor is stopped | never; otherwise a finding |
| `MSCA chN completion 0x… has no ACTIVE QBUF slot` | done entry without a matching slot | at most isolated after a start |
| `blocked for more than 120 seconds` | hang, e.g. deadlock on the frame channel mutex | never |
| `scheduling while atomic` / `sleeping function called from invalid context` | sleeping under a spinlock | never |

## Limits of these tests

- `tisp_deinit` (release of the statistics pages) runs only via
  `ispcore_slake_module`, which currently has no caller. This cannot be
  checked on the device.
- Whether the MDNS/WDR DMA really rests after DelSensor is shown only by a poison test in
  OpenIMP: `DMA_FreePhys` fills the block with `0xA5`, holds it back for one cycle
  and checks the pattern before it is handed out again. This needs a
  separate OpenIMP build; it is not part of these tests.
- The Y/UV atomicity shows itself only as a rare colour swap between two
  frames. Without a picture check it cannot be proven here.
- The bypass path (`ISP_CTRL_BYPASS`) is not used by the application.

## Latency measurement (branch `claude/t31-isp-latency`)

This section measures whether the latency branch removes the long phases with
interrupts disabled. It builds on sections 0 to 3
(rules, load module, `start_app`, `stop_app`, `bytes`, `post`, `check`).
Two modules are compared with the same camera, scene and configuration:

- **old**: state `claude/t31-isp-all` (commit `999a62b`)
- **new**: state `claude/t31-isp-latency`

| Commit (title) | What can be measured |
|---|---|
| do not zero the active parameter block before overwriting it | shorter day/night switch |
| refresh each day/night module with local IRQs off | no measurement of its own |
| run the day/night parameter switch from a work item | no ms peak per day/night switch (L2, L3) |
| serialize the day/night and custom bank switches | no hang (`blocked for more than`) |
| cancel the day/night work on module exit before tisp_deinit_free | no oops on `rmmod` (L1) |
| stop logging every poll of the VIC error-recovery loop | only on VIC errors: no `addr ctl is` flood |
| bound the release-ack poll of the WDR exception reset | only in WDR mode on errors, otherwise not reachable |
| rate-limit the VIC interrupt error lines | only on VIC errors: at most 20 `Err [VIC_INT]` per 5 s |
| let the AE0 solver read the unpacked statistics planes directly | a few µs per frame, not measurable individually |
| clear only the unused tail of the AE0 statistics planes | a few µs per frame, not measurable individually |
| skip the AE1 statistics work in linear mode | `ctxt`/s in `/proc/stat` drops by about the frame rate (L4) |

### L0: Build modules and measurement program (PC)

Build both modules as in section 0 and copy them as `/tmp/tx-isp-alt.ko` and
`/tmp/tx-isp-neu.ko` to the camera (alt = old, neu = new). In addition a small measurement program in the
style of `cyclictest`: It sleeps 1 ms in a loop with SCHED_FIFO and
measures how late it wakes up. Every phase with interrupts disabled delays
the timer interrupt and thus the wakeup by exactly this time.

```sh
cat > /tmp/latprobe.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 60, s = 0;
	struct sched_param sp = { .sched_priority = 90 };
	struct timespec next, now;
	long lat, max = 0, smax = 0, n = 0, b[4] = { 0 };

	if (sched_setscheduler(0, SCHED_FIFO, &sp))
		perror("sched_setscheduler");
	mlockall(MCL_CURRENT | MCL_FUTURE);
	clock_gettime(CLOCK_MONOTONIC, &next);
	while (s < secs) {
		next.tv_nsec += 1000000;
		if (next.tv_nsec >= 1000000000) {
			next.tv_nsec -= 1000000000;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		clock_gettime(CLOCK_MONOTONIC, &now);
		lat = (now.tv_sec - next.tv_sec) * 1000000L +
		      (now.tv_nsec - next.tv_nsec) / 1000;
		if (lat > max)
			max = lat;
		if (lat > smax)
			smax = lat;
		b[lat >= 2000 ? 3 : lat >= 1000 ? 2 : lat >= 500 ? 1 : 0]++;
		next = now; /* count a peak only once */
		if (++n % 1000 == 0) {
			printf("t=%d max_us=%ld\n", ++s, smax);
			fflush(stdout);
			smax = 0;
		}
	}
	printf("total n=%ld max_us=%ld <500:%ld 500-999:%ld 1000-1999:%ld >=2000:%ld\n",
	       n, max, b[0], b[1], b[2], b[3]);
	return 0;
}
EOF
$ROOT/host/bin/mipsel-linux-gcc -static -O2 -Wall -o /tmp/latprobe /tmp/latprobe.c
ssh root@$CAM 'cat > /tmp/latprobe && chmod +x /tmp/latprobe' < /tmp/latprobe
```

The camera's kernel has `CONFIG_HIGH_RES_TIMERS=y`, so a 1 ms period works.
The line `t=N max_us=M` appears about once per second. It shows the largest
delay of that second.

### L1: Switch module (camera)

As section 2, but with the respective module and **without**
`isp_day_night_switch_drop_frame_num` (default 0), so that only the latency is
measured:

```sh
KO=/tmp/tx-isp-alt.ko      # second pass: /tmp/tx-isp-neu.ko
/etc/init.d/<app-init> stop; sleep 2
set -- $(cat /etc/modules.d/30-sensor*); rmmod $1; rmmod tx_isp_t31
set -- $(cat /etc/modules.d/20-isp); shift; insmod $KO "$@"
set -- $(cat /etc/modules.d/30-sensor*); modprobe "$@"
dmesg -c >/dev/null; start_app; bytes 5
```

At most two or three module switches per reboot (memory loss on unloading,
see rules). Safer is: measure old, reboot, measure new.

### L2: Base load and day/night loop with latency probe

```sh
# 60 s base load: stream runs, no switches
curl -s -m 70 -o /dev/null http://127.0.0.1:8880/stream.mp4 &
/tmp/latprobe 60 > /tmp/lat-ruhe.txt; wait
tail -1 /tmp/lat-ruhe.txt

# 120 s with day/night switch every 5 s
curl -s -m 130 -o /dev/null http://127.0.0.1:8880/stream.mp4 &
/tmp/latprobe 120 > /tmp/lat-dn.txt &
for r in $(seq 12); do
  post '{"image":{"running_mode":1}}'; sleep 5
  post '{"image":{"running_mode":0}}'; sleep 5
done
wait
tail -1 /tmp/lat-dn.txt
sort -t= -k3 -n /tmp/lat-dn.txt | tail -5      # the five largest seconds
check L2
```

Evaluation: Compare the number of seconds with `max_us` above 1000 in `lat-dn.txt` with the
number of switches (24), each for old and new. Expectation: With
**old**, almost every second with a switch has a peak in the
order of 1 to 5 ms. With **new**, the peaks during the switches are
close to the base load from `lat-ruhe.txt`, at most a few 100 µs above
(longest single module). The base load itself hardly changes, because the
AE/AWB algorithms still run with interrupts disabled.

In `L2.dmesg`, one line per switch `Day/night mode updated: 0` or `: 1`
(about 24). FAIL hits in addition to section 3:
`T31 day/night switch failed`, `T31 day/night apply failed`.

Afterwards reset `running_mode` to the value from `/tmp/app-test.conf`.

### L3: Frame intervals (only with consent)

The frame timestamps are created in the ISP interrupt. With the old module, the
day/night switch ran in the same interrupt **before** the frame pick-up, so the
timestamp of the frame was shifted by the duration of the switch. This
shows up as an outlier in the frame interval. The measurement fetches stream data to the
PC. Therefore only if the human has agreed. Only timestamps are
output, no pictures are saved.

```sh
# PC, while the loop from L2 runs on the camera (without latprobe)
ffprobe -v error -rtsp_transport tcp -select_streams v:0 \
  -show_entries packet=pts_time -of csv=p=0 -read_intervals '%+120' \
  "rtsp://<user>:<pass>@$CAM:554/ch0" > /tmp/pts.txt
awk 'NR>1 { d = ($1 - p) * 1000; if (d > m) m = d; s += d; n++;
            if (d > 1.5 * 1000 / 25) big++ } { p = $1 }
     END { printf "n=%d mean=%.2f ms max=%.2f ms long=%d\n", n, s/n, m, big }' /tmp/pts.txt
```

Take the path (`video0.rtsp_path`) and credentials from the application configuration. Replace `25` by the
frame rate of `video0.fps`. Expectation: With old, the scatter around the
switches is visibly larger than with new. Mean and frame count stay the same.

### L4: Interrupts and context switches (linear mode)

```sh
snap() { grep -E 'isp-m0|isp-w02' /proc/interrupts; grep -E '^ctxt' /proc/stat; }
curl -s -m 70 -o /dev/null http://127.0.0.1:8880/stream.mp4 &
sleep 5; snap > /tmp/s0; sleep 60; snap > /tmp/s1; wait
paste /tmp/s0 /tmp/s1
```

Divide the differences by 60. Expectation: The interrupt rates of `isp-m0` (37)
and `isp-w02` (38) are the same for old and new. `ctxt`/s is smaller with new by about
the frame rate (the AE1 event is omitted without WDR). If the camera runs in
WDR mode, `ctxt`/s stays the same.

### L5: Drop frames after the switch

Repeat T2 from section 4 with the new module and
`isp_day_night_switch_drop_frame_num=6`. Expectation as there: The
stream keeps running after every switch. Now the count value applies from the moment
the new parameters are active. So per switch at least 6 frames are missing,
occasionally one more.

### L6: Error paths (observe only)

VIC errors and the WDR exception cannot be triggered deliberately. If they
occur, the following applies:

| Marker | Meaning | Expected |
|---|---|---|
| `Err [VIC_INT] : …` | VIC error bit | at most 20 lines per 5 s, afterwards `… callbacks suppressed` |
| `VIC error handler: status 0x…, restarting VIC` | VIC restart after an error | isolated; `addr ctl is` is no longer logged per loop pass |
| `VIC error handler: VIC did not stop, addr ctl is 0x…` | VIC stayed active for 1 ms, restart anyway | never; otherwise a finding |
| `ispcore: WDR exception reset: no release ack` | ISP did not acknowledge the release for 1 ms | never; otherwise a finding (before: hang) |
| `scheduling while atomic` / `sleeping function called from invalid context` | among others WDR refresh in the interrupt (old) | never with new |

In the report on L2 to L5, give the numbers for old and new side by side
(`total` line of `latprobe`, number of seconds above 1 ms, `ctxt`/s,
interrupt rates, with consent the `ffprobe` line).
