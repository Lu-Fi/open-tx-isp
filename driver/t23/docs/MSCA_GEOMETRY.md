# T23 MSCA channel geometry check

T23 only. Branch `claude/release-t23-crop`. Helper
`driver/t23/tx_isp_t23_msca_geom.h`, host test
`tests/tx_isp_t23_msca_geom_test.c`, counter `msca_geom_rejects` (read only).

## Problem

The imgfx crop batch hung the T23 test camera vorne (sc2336 1920x1080,
main 1920x1080, sub 640x360) in its first case, `fcrop-mid50`:

- No channel completed another frame. Sensor and ISP interrupts kept running.
- Every later STREAMOFF reported `2 of 2 buffers still queued after 21 x 10 ms`.
- timps then served only snapshot 503. Every later session hung as well;
  only a reboot helped.

## Cause

An MSCA output channel (record `msca + ch * 56`) runs this path:

    input window (front crop, +0x10..+0x1c)
      -> scaler output (+0x20, +0x24)
      -> output crop inside the scaler output (+0x28..+0x34)
      -> frame buffer (sized by user space for the channel format)

`IMP_ISP_Tuning_SetFrontCrop` (control 0x80000e3) writes the window into
every channel's record and locks it there (`cfg[3] = 1`). It then reloads the
running channels. With a 960x540 window under the 1920x1080 main channel, the
scaler would have to upscale 2x, which the MSCA cannot do. After the reload
latch, no output completes a frame any more.

The window stays locked in the channel records. The stock module does
the same: front crop "disable" only clears the enable flag and keeps the
window. So every following session loads the same impossible geometry,
and only a reboot (module reload) clears it.

This is not the staged-register problem seen on T41: the front crop path
requests the MSCA update (0xd010 = 1) itself, independent of
`msca_flip_skip_noop`.

The same stall class can be reached through the frame channel set-format
(`IMP_FrameSource_SetChnAttr` + EnableChn). The output crop is applied in
the scaler output frame, so these two cases are impossible:

- a crop that leaves the scaler output, e.g. imgfx `fs1-crop-mid50` on
  vorne: crop 960x540 at 480,270 inside a 640x360 scaler output;
- a crop larger than the frame buffer, which is a buffer overrun.

## What the driver does (beyond stock)

The stock module programs any geometry. This driver refuses a stalling
geometry with `-EINVAL` before anything is written, logs one line naming the
reason, and counts it in `msca_geom_rejects`. Rules
(`t23_msca_geom_check()`):

1. No size may be 0, and the input window must lie inside the sensor picture.
2. Scaler output <= input window, on each axis (no upscaling).
3. The output crop lies inside the scaler output.
4. The output crop is not larger than the channel's frame buffer.

Where the rules are checked:

- **Set-format.** The driver computes the geometry that
  `tisp_channel_main_attr_set()` would build, with the same lock bytes and
  defaults, and checks it against the requested frame size.
- **Front crop (enable = 1).** The new window is checked against every
  configured channel (`cfg[0]`), with that channel's scaler output and crop.
  A stopped channel would take the window at its next start.
- **Front crop (enable = 0).** Not checked. It changes nothing, as in stock.

A valid front crop (for example a window that is still at least as large
as every channel output) behaves as before.
