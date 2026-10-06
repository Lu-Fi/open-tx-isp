# T23 MSCA scratch area for stopped channels

Module parameter `msca_scratch` (default 1, 0644). T23 only. Branch
`claude/release-t23-af-scratch`.

## Problem

Since the release defaults (`STREAMOFF_DRAIN_WAIT.md`), STREAMOFF of a
frame channel works like this:

- It leaves the MSCA output enabled (`msca_keep_enabled=2`).
- It keeps the input running while tx-isp streams (`chan_stop_keep_input=1`).
- It waits until the stream's buffers have left the address FIFO
  (`chan_stop_drain`).

The MSCA pops one Y/UV address pair per frame. When the FIFO is empty, it
writes the next frame to the last address it popped. That address belongs
to a buffer of the stopped stream. User space gets that buffer back with
STREAMOFF, and openimp may free it or hand it out again (pool destroyed
after the close). So, until the next STREAMON, every frame goes into
memory that the channel no longer owns. The ISR drains these completions,
and they show up as `unmatched MSCA completion`.

## What the driver does

1. **Scratch area.** `GET_BUF` asks for one more block at the end of the ISP
   buffer that libimp allocates in rmem. The block is one Y plane at sensor
   size, with the height aligned to 16 and rounded up to whole pages. For
   sc1a4t at 1280x720 that is 900 KiB. `SET_BUF` places the scratch area at
   the end of the buffer, behind the MDNS area and the crumb page
   (`crumbs=1`). `msca_scratch_phys` and `msca_scratch_bytes` show where it is.
   - All channels share this one area. Y and UV point at the same address,
     so one Y plane at sensor size covers every channel. What lands there is
     never read.
   - The area lives from AddSensor to DelSensor. That is longer than any
     running input: DelSensor refuses while the sensor is enabled, and a
     process that dies stops the input on its last close of `/dev/tx-isp`.
   - It is not kernel memory. That has two benefits. First, there is no
     contiguous kernel block to allocate. On the Jooan A6M the largest free
     block is order 6 (256 KiB), even after `compact_memory`, and a 720p
     plane needs order 8. Second, after a module unload nothing can point
     at freed kernel pages.
2. **Park at STREAMOFF.** Parking happens when all of these hold:
   - the output stays enabled,
   - the input keeps running after this STREAMOFF,
   - the output bit is in the channel mask.

   STREAMOFF (and the close of the owning file) then writes the scratch
   address into the channel's address FIFO. It does this before the drain
   wait, so the scratch address sits behind the stream's buffers. The MSCA
   completes those buffers normally. After that it writes every further
   frame into the scratch area. The FIFO never runs empty on a user
   buffer.
3. **Held QBUFs.** A parked channel does not write a QBUF into the FIFO. The
   buffer is recorded and marked deferred. If its address went in now, the
   MSCA would consume it before STREAMON and stay on it. Stock behaves the
   same way: buffers queued before STREAMON reach the hardware only at
   STREAMON (`__enqueue_in_driver`). The drain wait does not count deferred
   buffers.
4. **Unpark at STREAMON.** STREAMON marks the channel as streaming, then
   writes the deferred buffers into the FIFO. The MSCA pops them after the
   scratch address. The first one completes at least one frame later, when
   the channel already streams.
5. **Completions** of the scratch address are counted
   (`msca_scratch_frames`). They no longer show up as unmatched.
6. **Settle before an input stop.** Before the input stops (tx-isp
   STREAMOFF, last close, or the last channel with `chan_stop_keep_input=0`),
   the driver waits until the MSCA has consumed the scratch address of every
   parked channel. That is one or two frames, at most `chan_stop_drain`
   x 10 ms, and only while frames arrive.
   - Without this wait, a raptor exit stops the input a few ms after the
     last channel close. The scratch address stayed in the FIFO, and the
     next session start found a non-empty address FIFO, which
     `msca_session_release` clears.
   - The first device build had no wait. The Jooan A6M hung (watchdog
     reset, the camera went dead about 20 s after the stop, as the next
     start began) at 3 of about 35 cold starts.
   - With the wait: 0 of 40 cold starts and 0 of 20 restarts hung. The
     longest wait was 3 x 10 ms.
   - The previous defaults never left an entry in the FIFO either: the
     drain wait empties it.
7. **Session release.** `msca_session_release` switches off kept outputs at
   a new tx-isp session and clears their FIFOs. It also clears the parked
   mark.

## Parameters and counters

| name | mode | meaning |
|---|---|---|
| `msca_scratch` | 0644, default 1 | 0: no scratch area at the next GET_BUF, no parking (behaviour before) |
| `msca_scratch_phys` | 0444 | physical address of the area (0 = none) |
| `msca_scratch_bytes` | 0444 | its size |
| `msca_scratch_parks` | 0444 | STREAMOFFs that parked the output |
| `msca_scratch_frames` | 0444 | completions into the scratch area |
| `msca_scratch_deferred` | 0444 | QBUFs held back until STREAMON |
| `msca_scratch_settle_timeouts` | 0444 | input stops that still found a scratch address in a FIFO after the wait (kmsg `scratch address of channels ... not consumed`) |
| `msca_scratch_settle_ticks_max` | 0444 | longest settle wait, 10 ms steps |
| `msca_scratch_skips` | 0444 | STREAMOFFs that wanted to park but had no area, or one too small (kmsg `no MSCA scratch area`) |

kmsg: `MSCA scratch area 0x..+0x..` at SET_BUF, `output parked on scratch` at
STREAMOFF, `unparked, N held buffers queued` at STREAMON.

## Not changed

- `msca_keep_enabled` other than 2: there is no parking.
  - With 0, the output is switched off.
  - With 1, it is kept only when the input stops.
- A STREAMOFF that stops the input: no frame follows, so there is no
  parking. The next tx-isp session releases the output.
- `msca_fifo_rearm=1` (debug) clears the FIFO at STREAMON and writes every
  queued buffer again, the deferred ones included.

## Host test

`tests/tx_isp_t23_scratch_test.c` covers the helpers in
`tx_isp_t23_scratch.h`:

- the size,
- the placement in the ISP buffer (with and without the crumb page, too
  small, misaligned),
- the frame-fit check,
- the park decision for each `msca_keep_enabled` / input state,
- the completion match.

## Device test (2026-10-06)

Jooan A6M .30: T23N, sc1a4t 1280x720 at 15 fps, raptor, flashed openimp.
The module was loaded into RAM with the parameters of
`/etc/modules.d/20-isp`, with `source_af=1` (default).

- **20 cold starts** (raptor stop, 20 s idle, start): all 40 snapshots OK,
  no hang. 40 parks, 41 scratch completions, 0 settle timeouts, longest
  settle 30 ms.
- **20 `rvd` restarts, then an ISP day/night switch** (night, then day):
  all snapshots OK. After that run, 80 parks and 81 scratch completions
  in total; 0 settle timeouts, 0 drain timeouts, longest drain 70 ms.
- **apitest sys,fs,isp:** the FrameSource disable/enable cycle passed. It
  parks, holds back 3 QBUFs, then unparks. The remaining FAILs are the
  same as with the release module (FS delay/pool/GetFrame, sensor
  register).
- **`unmatched MSCA completion`:** none at any STREAMOFF. The only ones
  seen came right after an insmod: the previous module instance had left
  completions in the FIFO.
- 0 oopses.
