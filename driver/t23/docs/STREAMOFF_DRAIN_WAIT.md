# T23 frame-channel STREAMOFF drain wait and QBUF cache invalidate

Release defaults since branch `claude/release-t23-driver` (2026-10-06).
Both behaviours come from the stock `tx-isp-t23.ko` (unstripped galayou
build). The sequence audit is `START_STOP_SEQUENCE_STOCK_VS_OPEN.md` on
branch `claude/t23-seq-audit`, sections 1.4, 1.5 and 4 (rank 2 and rank 8).

## Why

Stock T23 never clears an MSCA address FIFO: `tisp_channel_main_fifo_clear()`
has no caller. Instead, `__frame_channel_vb2_streamoff()` waits before it
cancels the queue:

    while (q->done_count < q->num_buffers && n < 21) { msleep(10); n++; }

While this loop runs, the input, the output (`0xd040` bit) and the core ISR
keep running. The MSCA consumes the addresses still in its FIFO, and they
complete as normal frames. When STREAMOFF returns, no address of this
stream is left in the hardware FIFO, unless the input produces no frames.

The open driver had no such wait. Instead, `msca_fifo_rearm=1` (1911bb83)
cleared the FIFO and refilled it at the next STREAMON. That fixed the
cold-start snapshot 503: stale FIFO entries from the previous stream were
consumed first, and the buffers of the new stream were never reached. But
the clear runs while the input may be live. Together with
`chan_stop_keep_input=0`, it is the cam-B hang combination. The overnight
run (7 h plus many streamer restarts) used the stock-like set, with
`msca_fifo_rearm=0`.

Without a FIFO clear and without a drain, two things can go wrong:

- A stopped stream can leave addresses in the FIFO. If the input stops
  before the MSCA consumes them, the next stream gets them first. That is
  the 503 path.
- openimp destroys the frame pool right after the close
  (`IMP_FrameSource_DisableChn`). If the MSCA output stays enabled
  (`msca_keep_enabled=2`), the MSCA writes frames into freed rmem.

## What the driver does now

Code: `driver/t23/tx_isp_t23_core.c`, in `regtrace_framechan_drain_wait()`
and `regtrace_framechan_record_qbuf()`.

1. **Drain wait.** STREAMOFF and close both pass through
   `regtrace_framechan_stream_off_locked()`. Before the channel stops
   streaming, the driver waits until none of the channel's buffers is
   still queued in the hardware. A buffer counts as queued when it was
   written to the FIFO and its completion has not been matched yet. The
   wait is at most `chan_stop_drain` steps of 10 ms (default 21, the same
   as stock).
   - The wait runs only when frames can arrive: the channel streams, the
     VIC/input streams, and the output bit is in `msca_ch_en`.
   - While it waits, completions are matched as normal frames, because the
     channel still counts as streaming.
   - The stream mutex stays held, as in the other stop paths. QBUF, DQBUF
     and the ISR do not take it.
   - The last channel close before a tx-isp STREAMOFF drains too, with the
     input still running. The next session therefore starts with empty
     FIFOs.
2. **QBUF cache invalidate.** Before a buffer's addresses reach the FIFO,
   the driver calls `dma_cache_sync(NULL, kseg0(phys), len,
   DMA_FROM_DEVICE)`. It covers the whole buffer and needs no struct page
   for rmem. Stock does the same: `dma_sync_single_for_device(NULL, phys,
   len, DMA_FROM_DEVICE)` on every QBUF in non-direct mode. The T30 C
   source does it in `__buf_prepare`. Without it, a dirty line evicted
   later could overwrite part of a frame the MSCA wrote. That would be
   image corruption, not a hang. Switch: `qbuf_cache_inv` (default 1).

Release defaults (all 0644, debug escape hatches):

| parameter | default | note |
|---|---|---|
| `chan_stop_keep_input` | 1 | input runs from tx-isp STREAMON to STREAMOFF (stock) |
| `msca_keep_enabled` | 2 | STREAMOFF never clears the `0xd040` bit (stock) |
| `msca_fifo_rearm` | 0 | no FIFO clear at STREAMON (stock); 1 + `chan_stop_keep_input=0` = hang |
| `msca_flip_skip_noop` | 1 | no `0xd010` for an unchanged flip word |
| `msca_restart_skip` | 1 | no reload of a still-enabled output with unchanged geometry |
| `msca_session_release` | 1 | outputs left enabled are released at a new tx-isp session (input stopped) |
| `crumbs` | 0 | off |
| `chan_stop_drain` | 21 | STREAMOFF drain wait, N x 10 ms; 0 = no wait |
| `qbuf_cache_inv` | 1 | cache invalidate at QBUF |
| `msca_scratch` | 1 | park a kept output on a scratch area at STREAMOFF (`MSCA_SCRATCH.md`) |

Read-only counters:

| counter | meaning |
|---|---|
| `chan_drain_waits` | number of STREAMOFFs that found buffers in the hardware |
| `chan_drain_timeouts` | number of drains that ended with buffers still queued; kmsg `stream off: N of M buffers still queued` |
| `chan_drain_ticks_max` | longest drain, in 10 ms steps |

## Remaining exposure

After the drain the FIFO is empty, but the output stays enabled while the
input runs (`msca_keep_enabled=2`). The open driver's notes say that the
MSCA then writes into the last address it used. That address belongs to
the stopped channel's pool.

openimp therefore parks the pool's rmem block on T23 as on T21
(`vbm_parked`, branch `claude/release-t23-vbm`). The block stays owned by
the channel until one of two things happens:

- the channel's next pool of the same size takes it back, or
- `DestroyChn` frees it.

With raptor, prudynt and timps, `DestroyChn` comes only at process exit,
after the last close. The input stops with the tx-isp STREAMOFF of that
exit.

Since branch `claude/release-t23-af-scratch`, the driver closes this gap
itself. STREAMOFF queues a scratch address behind the stream's buffers, so
the kept output writes into a scratch area at the end of the ISP buffer
instead of the stream's last buffer (`msca_scratch`, see `MSCA_SCRATCH.md`).

## Device test (Jooan A6M .30, T23N + sc1a4t, raptor)

See the commit message of the drain-wait commit and the CHANGELOG entry for
the numbers. Method: the module was loaded into RAM (rmmod/insmod, same
parameters as `/etc/modules.d/20-isp`), and raptor was started with the
openimp `claude/release-t23-vbm` libimp from `/tmp` (LD_LIBRARY_PATH).
Then three tests ran:

- Cold starts: stop raptor, wait 20 s, start it, and time the first
  snapshot of ch0 and ch1 (`raptorctl rvd save jpeg`, 15 s limit).
- `rvd` restarts without idle time.
- An ISP day/night switch.
