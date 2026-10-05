# T23 start/stop sequences: stock `tx-isp-t23.ko` vs. the open driver

Purpose: find the timing/ordering deviation behind the cam-B hard hang
(Galayou Y4, T23N + sc2336, timps): with the default module the SoC dies
~0.5 s after the second `EnsureLinkStreamOn` (second frame-channel enable
while the tx-isp stream is already on); with the stock-like knob set
(`chan_stop_keep_input=1 msca_keep_enabled=2 msca_fifo_rearm=0
msca_restart_skip=0 msca_flip_skip_noop=0`) cam-B survived a cold start,
5 min and 14 wakes (`t23reboot-repro/2354-stocklike2`).

No driver behaviour is changed by this document. Inputs:

- Stock: `tx-isp-t23.ko` (unstripped, galayou build, md5
  `8237acb18a548d8ad8c2d3fcf1517724`), `objdump -dr` annotated with
  `audit/dis_annotate.py` + `audit/dis_strings.py`; MMIO traces with
  `audit/seq_emu.py` (on top of `audit/memu.py`).
- Open driver: branch `claude/fable-chan-restart` @ `4ce66147`,
  `driver/t23/tx_isp_t23_core.c` (line numbers below refer to that file),
  built module `fable-chan-restart-tx-isp-t23.ko` (md5 `d5f622cf…`) for the
  emulator.
- openimp `d810050` (`src/framesource/framesource_tseries.c`,
  `src/isp/isp_tseries.c`, `src/kernel_interface.c`).

Evidence tags: **[P]** proven from the stock disassembly, **[E]** proven by
emulator trace, **[S]** read from our source, **[I]** inferred (not proven;
needs the device or more RE).

Register names: core base = ISP core (`sd+0xb8`), `0xd0xx` = MSCA global,
`0xdN..` = MSCA channel N (`(N + 0xd0) << 8`), VIC base = VIC subdev
`sd+0xb8`.

---

## 0. Stock architecture facts that drive everything else

| # | fact | tag |
|---|---|---|
| A1 | tx-isp `STREAMON` (`0x80045612`) / `STREAMOFF` (`0x80045613`) call `tx_isp_video_s_stream(dev, 1/0)`, which walks `dev->subdevs[0..15]` **forward on both edges** and calls `video_ops->s_stream`. Slots (from `tx_isp_create_graph_and_nodes`, byte 3 of the platform data): core = 0, ivdc = 1, framesource = 4 (fs has no video ops, ivdc's is `return 0`). | [P] 0x103c0, 0x12a20, `.data` 0x330/0x428/0x1f8 |
| A2 | The core's `ispcore_video_s_stream` walks its own children forward on both edges: **CSI (0) → VIN/sensor (1) → VIC (2)** (widget byte 3: csi 0x20, vin 0x21, vic 0x22). | [P] 0x6a818, `.data` 0x570/0x690/0x798 |
| A3 | `TX_ISP LINK_STREAM_ON/OFF` (`0x800456d2/d3`) → `tx_isp_video_link_stream`, which calls `video_ops[1]`; that slot is NULL for every T23 subdev, so it is a no-op. | [P] 0x104c0, ops tables |
| A4 | The TISP core (`0x800 = 1`, all block init) is started by `tisp_init()` from `ispcore_core_ops_init(on)` (sensor enable/init path), **not** by any stream ioctl. No stream path writes `0x800`. The only runtime `0x800` writer is `exception_handle()`, gated by `core+0x180`, which nothing in the module sets to non-zero (only `ispcore_frame_channel_streamoff` stores 0 there), i.e. dead by default. | [P] 0x6aa18, 0x163ac, 0x6a6c0, xrefs |
| A5 | `tisp_channel_main_fifo_clear()` (writes `0xdN44/64/48/68 = 1`) and `tisp_channel_main_stop()` have **no caller** (no relocation against them). Stock never clears an MSCA address FIFO and never clears a `0xd040` bit at run time. | [P] xref search |
| A6 | `csi_video_s_stream` and `vic_core_s_stream(0)` write no register (state 3/4 only). CSI is programmed only in `csi_core_ops_init`. | [P] 0x582c, 0x1894 |
| A7 | No frame-buffer cache maintenance happens anywhere except **`dma_sync_single_for_device(NULL, phys, length, DMA_FROM_DEVICE)` on every frame-channel QBUF** (non-direct mode). The other `dma_cache_sync` users are statistics ISRs (AE/AWB/AF/ADR/defog) and the VIC proc command. | [P] 0xf1e0, callee lists |

---

## 1. Stock sequences

### 1.1 tx-isp STREAMON (`0x80045612`, `tx_isp_video_s_stream(1)`) [P]

1. `ispcore_video_s_stream(core, 1)` (0x6a818):
   1. `spin_lock_irqsave(core+0xdc)`; require core state ≥ 3 else `-1`;
      unlock. Clear counters `core+0x164/168/16c/174`.
   2. state 3 → 4.
   3. Children forward:
      - **CSI** `csi_video_s_stream(1)`: if MIPI, `csi+0x12c = 4`. No MMIO.
      - **VIN** `vin_s_stream(1)`: if state ≠ 4, sensor subdev
        `video->s_stream(1)` (sensor I2C stream-on), state 4. Data starts
        flowing into the (already configured) CSI/VIC here.
      - **VIC** `vic_core_s_stream(1)` (0x1894): if VIC state ≠ 4:
        `tx_vic_disable_irq()`; `tx_isp_vic_start()`; state = 4;
        `tx_vic_enable_irq()`. `tx_isp_vic_start` (MIPI, OTHER_MIPI for
        sc2336), VIC base writes in this order:
        `0x1a4 = 0x000a000a` (SONY: `0x10 = 0x20000`, `0x1a4 = 0x100010`),
        `0x100 = ceil(bpp*width/32)`, `0x0c = 2`, `0x14 = fmt`,
        `0x04 = w<<16|h`, `0x10c = mipi cfg`, `0x110`, `0x114`, `0x118`,
        `0x11c` (attr), `0x1ac = 0x1a8 = 0x4440/0x4140/0x4240` (frame mode),
        `0x1b0 = 0x10`, **`0x0 = 2`, `0x0 = 4`**, `0x1a0 = mode`,
        **poll `0x0 != 0` — unbounded busy loop, no delay, no timeout**,
        `0x104 = attr(0x56)<<16|attr(0x52)`, `0x108 = attr(0x5e)<<16|attr(0x5a)`,
        **`0x0 = 1`**, `vic_start_ok = 1`. Runs with the sensor already
        streaming (A2) and the VIC IRQ line disabled.
   4. If not the bypass sensor type: **core `0xb0 = 0xffffffff`** (ISP IRQ
      mask open), `tx_isp_enable_irq()` (`enable_irq`, under
      `spin_lock_irqsave(sd+0x80)`). Order: IRQ unmask **after** CSI/sensor/VIC.
2. ivdc: `return 0`. fs: no op.

No MSCA, FIFO, `0x800`, GIB or CSI register is touched.

### 1.2 tx-isp STREAMOFF (`0x80045613`, `tx_isp_video_s_stream(0)`) [P]

1. `ispcore_video_s_stream(core, 0)`:
   1. lock/check as above, counters cleared.
   2. If core state 4: for each of the 3 channels with channel state 4,
      `ispcore_frame_channel_streamoff(ch)` (state 4 → 3, **no MMIO**, see
      1.4). Core state 4 → 3.
   3. Children forward: CSI state 3 (no MMIO) → **sensor stream off**
      (I2C) → VIC state 4 → 3 (**no MMIO**; VIC keeps `0x0 = 1`).
   4. **core `0xb0 = 0`**, `tx_isp_disable_irq()` (`disable_irq`, i.e.
      synchronize with a running handler).
2. Nothing else: TISP core keeps running (`0x800 = 1`), VIC keeps its run
   bit, every MSCA output keeps its `0xd040` bit and its address FIFO.

### 1.3 Frame-channel STREAMON (`VIDIOC_STREAMON` on `/dev/framechanN`) [P]

fs side (`frame_channel_unlocked_ioctl`, 0xee90):
1. fs channel state must be 3, buffer type must match, not already
   streaming (`"streamon: already streaming"`).
2. `spin_lock_irqsave(fs+0x2c4)`; for every buffer on the queued list
   (QBUF'd while not streaming): `__enqueue_in_driver()` → event
   `0x3000005` (core QBUF, 1.5) → **the Y/UV addresses go into the MSCA
   FIFO now, before the output is enabled**. Unlock.
3. streaming flag (`fs+0x230 |= 1`).
4. event `0x3000003` → `ispcore_pad_event_handle` STREAMON (0x69cf8):
   - pad state must be 3 (else return 0);
   - **`spin_lock_irqsave(chan+0x9c)`** (local IRQs off, no preemption);
   - if channel state ≠ 4: **`tisp_channel_start(ch)` inside the
     lock**; state = 4, pad state = 4;
   - unlock.
5. fs state = 4; `"Streamon successful"`.

`tisp_channel_start(ch)` (0x167a4) = `mscaler[ch].state(+0xc) = 1`,
`msca[ch].en = 1`, `tisp_msca_chx_cfg_load(0, ch, &msca[ch])` (0x19e48):

| step | MMIO | tag |
|---|---|---|
| `tisp_msca_para_calc`, `mscaHardPar[ch] = cfg` | — | [P] |
| `tisp_msca_curve_calc`, `tisp_msca_ch_curve_write_ctrl` | LUT port `0xe000 = 0x101`, `0xe004` addr/data pairs **for every enabled channel, including the live ones**, `0xe000 = 0x01010102`, `0xe100` bank word (e.g. `0x111 → 0x231` with ch0 live) | [E] |
| `tisp_msca_init_chx_cfg` | channel block `0xdN00..0xdN28` (`0xdN28 = 0x41` then `0`) | [E] |
| `tisp_msca_write_reg` | `0xd090` (global crop origin), `0xd094` (global size), then `tisp_msca_scaling_algorithm`: `0xd050 = rd(0xd050) | uv bits | 7 (| 0x11)`, `0xd04c = ctl_mode bits`, **`0xd040 = hard[0].en | hard[1].en<<1 | hard[2].en<<2` (absolute)**, then **`0xd010 = 1`** (update request) | [P][E] |
| strides | `0xdN40 = 0xdN60 = cfg+0x30` (direct_mode ch0: `0x1000`) | [P][E] |
| enable | `0xd040 = rd(0xd040) | en<<ch` | [P][E] |

Emulator (`seq_emu.py stock.ko ours.ko 1 live`, ch0 started first in the
same instance), stock tail: `0xd090, 0xd094, R 0xd050, 0xd050, 0xd04c,
0xd040 = 3, 0xd010 = 1, 0xd240, 0xd260, R 0xd040, 0xd040 = 3`.

No wait, poll or frame-boundary sync anywhere in this path [P]; the
whole sequence runs with local IRQs off [P], which keeps it within
microseconds (no ISR, no preemption between `0xd010` and the strides/enable).

### 1.4 Frame-channel STREAMOFF [P]

fs side, `__frame_channel_vb2_streamoff` (0xd7b8):
1. type check, must be streaming.
2. **Drain wait:** `while (q->done_count < q->num_buffers && n < 21)
   { msleep(10); print once "wait stop q->num_buffers=%d
   q->done_count=%d"; n++; }` — up to 21 × `msleep(10)` (210 ms nominal,
   ~420 ms at HZ=100 [I]). `done_count` (`fs+0x224`) counts buffers the
   hardware completed and user space has not dequeued; it is incremented
   by the frame-done event (`frame_chan_event`, 0xe664) and decremented by
   DQBUF. While the drain runs the input, the output (`0xd040`) and the
   core ISR all keep running, so the MSCA consumes the addresses still in
   its FIFO and their completions are delivered as normal frames.
   *Effect* [I]: when STREAMOFF returns, no address of this queue is left
   in the hardware FIFO unless the input is not producing frames.
3. `printk("streamoff stop!!")`, `__vb2_queue_cancel()`: event
   `0x3000004` → `ispcore_frame_channel_streamoff` (0x687fc): under the
   channel spinlock check state 4 → 3, pad state 3, memset channel queue,
   counters `+0xa0/+0xb0/core+0x180 = 0`. **No MMIO.** Then streaming flag
   cleared, lists re-initialised, event `0x3000008` (core: lock/unlock
   only), `wake_up_all`, all buffers state 0.
4. fs state = 3.

The output stays enabled (`0xd040` bit set), with whatever is left in its
FIFO, and the input stays up.

### 1.5 QBUF [P]

fs (`frame_channel_unlocked_ioctl` 0xf0a0 → 0xec9c):
1. checks (type, index < num_buffers, memory, length == sizeimage, not in
   use).
2. **`dma_sync_single_for_device(NULL, phys, length, DMA_FROM_DEVICE)`**
   (cache invalidate of the whole buffer) — always in non-direct mode.
3. `mutex_lock(fs+0x28)`, `spin_lock_irqsave(fs+0x2c4)`: add to queued
   list, unlock.
4. **Only if streaming**: `__enqueue_in_driver()` → event `0x3000005` →
   core QBUF (0x69df0): `spin_lock_irqsave(chan+0x9c)`, check NV12, buffer
   state 5, **`0xdN30 = Y`, `0xdN50 = Y + align16(h)*w`** (direct MMIO
   stores, no readback, no barrier), unlock.
5. Not streaming: the buffer waits on the queued list until 1.3 step 2.

The FIFO is therefore only ever written for a streaming channel, and
always after the cache invalidate.

### 1.6 REQBUFS / close [P]

- REQBUFS: refused while streaming (`"reqbufs: streaming active"`);
  count 0 / re-alloc → `__vb2_queue_free` (kfree descriptors, event
  `0x3000008` = no-op). No MMIO.
- close (`frame_channel_release`): if fs state 4 → 1.4 (with the drain
  wait), then `__vb2_queue_free`; fs state 2. No MMIO.

### 1.7 Frame done (core hard IRQ, `ispcore_interrupt_service_routine` 0x6b7e4) [P]

`st = rd(0xb4); wr(0xb8, st)`; bit 12 (frame start) → `schedule_work(fs_work)`;
bits 8/9 (break/overflow) counted (exception reset dead, A4); bit 0/1/2
(channel N done): `while (!(rd(0xdN3c) & 1)) { y = rd(0xdN38) & ~7;
uv = rd(0xdN58) & ~7; send 0x3000006 to framechanN; }` — drains the done
FIFO of channel N **whenever its status bit is set, independent of the
channel state**, unbounded loop. Then `irq_func_cb[bit]` for every set bit.

### 1.8 HV flip [P]

`HFLIP`/`VFLIP` (s_ctrl 0x980914/15): `tisp_s_mscaler_hvflip_mask()` +
`tisp_hv_flip_enable()` → `vmalloc` scratch → `tisp_msca_api_set_mirr_flip`:
`0xd050 = (rd(0xd050) & 0xc00001ff) | flip bits (| 0x11)`, **`0xd010 = 1`**,
always (no no-op check), process context, no lock, no frame sync. The
combined HV control while streaming marks it pending (`core+0x1ac/0x178`)
and `ispcore_irq_thread_handle` applies it (same two writes) from the IRQ
thread.

### 1.9 Day/night bank switch [P]

`tisp_day_or_night_s_ctrl` (from `isp_core_tuning_event`): memcpy of the
day/night tparams bank, `0xc` top bypass RMW, then every block's
`*_dn_params_refresh` (register writes only), `tispPollValue`,
`__wake_up`. No MSCA/FIFO register, no wait, no lock against the ISR.

---

## 2. Open driver sequences

Knobs: **default** = agg-26 behaviour (`chan_stop_keep_input=0
msca_keep_enabled=1 msca_fifo_rearm=1 msca_restart_skip=1
msca_flip_skip_noop=1 msca_session_release=1`); **stock-like** =
`chan_stop_keep_input=1 msca_keep_enabled=2 msca_fifo_rearm=0
msca_restart_skip=0 msca_flip_skip_noop=0`. `direct_vic_start=0`
(VIC MDMA paths are no-ops). All [S] unless tagged.

### 2.1 tx-isp STREAMON (`regtrace_t23_txisp_stream_locked(1)`, l. 17123)

Under `regtrace_framechan_stream_lock` (mutex):
1. `enable_stream_clks()`.
2. `tisp_prestart()` → `source_core_set_stream(1)` (l. 14965): if the core
   is not running: resolve sensor, **program core DMA rings**, `0x800 = 0`,
   `0x4, 0x8, 0x1c, 0x2c, 0xc, 0x10, 0x30`, all block init (AE/GIB/gamma/
   LSC/DPC/YDNS/defog/CCM/DMSC/ADR/AF/sharpen/HLDC/BCSH/CLM/CSC/AWB/
   MDNS/SDNS, MDNS DMA restore), `0x33c`, `0x804`, `0x1c`, **`0x800 = 1`**,
   `0xb000 = 1`.
3. `msca_release_stale()` (only with the input stopped): for each output
   with `0xd040` bit set and no streaming channel: `0xd040 &= ~bit`, pop the
   done FIFO, **`tisp_channel_main_fifo_clear()`** (`0xdN44/64/48/68 = 1`).
4. `source_input_stream(1)` (l. 12353): `csi_core_ops_init(1)` (CSI PHY
   reset/config, 3 × `msleep(1)`), CSI state 4, then
   `vic_core_s_stream(1)` (l. 12318) → `vic_start_repaired` (l. 12170):
   **sensor core init (full I2C register list)**, CSI init again (no-op once
   initialised), VIC config `0x1a4, 0x100, 0x0c, 0x14, 0x04, 0x10c, 0x110,
   0x114 = 0x118 = 0x11c = 0, 0x1ac, 0x1a8, 0x1b0`, `wmb`, **sensor
   stream on**, `0x0 = 2, 0x0 = 4, 0x1a0`, `wmb`, poll `0x0 == 0` (100 ×
   `usleep_range(1000, 2000)`, `-ETIMEDOUT`), **`0x104 = 0, 0x108 = 0,
   0x10 = 1`**, `0x0 = 1`, VIC IRQ regs `0x1f0/0x1f4 = ~0`, `0x1e8/0x1ec`.
5. `tisp_stream_regs(1, -1)` (l. 15184): `source_core_set_stream(1)`
   (no-op now), `0x1010 = 0x04000400, 0x1014 = 0, 0x1008 = 0, 0x1060 = 1`,
   GIB black levels rewritten.
6. `stream_irq_gate(1)`: core `0xb0 = ~0` (written every call), enable
   core + VIC IRQ lines.

### 2.2 tx-isp STREAMOFF (`txisp_stream_locked(0)`)

`source_input_stream(0)`: VIC state 3 + **sensor stream off**, CSI state 3,
**`csi_core_ops_init(0)`: `0x08/0x0c/0x10 &= ~1` (CSI/PHY shut down)**;
`tisp_stream_regs(0)`: **`0x1060 = 0`, `0x800 = 0`** (core stopped, AE/ADR/
AF lifts halted, block init flags cleared); IRQ gate off (`0xb0 = 0`,
`disable_irq` core/VIC/IVDC). Output bits/FIFOs untouched.

### 2.3 Frame-channel STREAMON (`regtrace_framechan_stream_on`, l. 16999)

Under the stream mutex, **local IRQs on throughout**:
1. `enable_stream_clks()`, `tisp_prestart()` (core start if stopped, 2.1
   step 2).
2. If the channel is not streaming:
   - `drop_stale_done(ch)` (l. 16894): if `ch` not in `msca_ch_en`, pop
     the done FIFO (`0xdN3c/38/58`) under `local_irq_save`.
   - `rearm_fifo(ch)` (l. 16939), **default only** (`msca_fifo_rearm=1`):
     if `ch` not in `msca_ch_en` and it has queued buffers: if the output
     is kept and the VIC streams, `0xd040 &= ~bit` (**output disabled
     mid-stream**); **`tisp_channel_main_fifo_clear(ch)`**; re-write every
     queued buffer (`0xdN30/0xdN50`). Under `framechan_done_lock`
     (spin, IRQs off).
3. `set_streaming(ch, true)` (done ring reset).
4. If the VIC is not streaming: `source_input_stream(1)` (2.1 step 4:
   CSI init, sensor init + on, VIC start).
5. `tisp_stream_regs(1, ch)`: **`0x1010/0x1014/0x1008/0x1060` + GIB black
   levels, on every channel start, with the input live**.
6. `set_msca_stream(ch, 1)` (l. 16410):
   - default + `msca_restart_skip`: bit still set and same geometry → no
     reload (state bytes only);
   - else `tisp_channel_start(ch)` → `tisp_msca_chx_cfg_load` (l. 45289):
     para calc, curves (`0xe000/0xe004/0xe100`, same as stock [E]),
     `init_chx_cfg`, `commit_global_window()` (l. 45237): `0xd090, 0xd094,
     0xd050 |= 7, 0xd04c = 0, 0xd010 = 1`, strides `0xdN40/60`,
     **`0xd040 = rd | en<<ch`** (first and only enable), `printk`;
   - then again `0xd040 = before | msca_ch_en` (RMW), readback, `printk`.
7. `stream_irq_gate(1)` (`0xb0 = ~0`, IRQs already enabled).

### 2.4 Frame-channel STREAMOFF / close (`stream_off_locked`, l. 17176)

1. clear the channel from `stream_mask`; `last` = no channel left
   (stock-like: `last = false` while tx-isp streams).
2. `set_streaming(ch, false)`: done ring cleared, **queued buffers
   forgotten (software only)**, waiters woken. **No drain wait.**
3. `set_msca_stream(ch, 0, last)`: default: keep the `0xd040` bit when
   `last`, otherwise **`0xd040 &= ~bit` with the input live**; stock-like
   (`keep=2`): keep always.
4. If `last` (default only): 2.2 (sensor off, CSI shut down, `0x1060 = 0`,
   **`0x800 = 0`**, IRQs off).

close → same (owner check). REQBUFS (l. 17325): forgets the slot table if
not streaming (software only).

### 2.5 QBUF (`regtrace_framechan_record_qbuf`, l. 16505)

rmem guard, build NV12 addresses, `spin_lock_irqsave(done_lock)`, slot
bookkeeping, **`tisp_msca_addr_fifo_write()` (`0xdN30/0xdN50`) at once,
streaming or not**, unlock. **No cache maintenance.**

### 2.6 Core IRQ (`isp_irq_handle_body`, l. 34058)

`st = rd(0xb4); wr(0xb8, st); wmb`; stats/AE/AWB/ADR/AF; then for every
channel in `msca_ch_en | msca_kept` (**not** keyed on status bits 0/1/2):
drain the done FIFO (bounded to the slot count), match against the slot
table, unmatched → `"unmatched MSCA completion"`.

### 2.7 HV flip / day-night

Flip (l. 102556 and the stock-API copy at l. 46228): same `0xd050` +
`0xd010` writes; default skips both when `0xd050` would not change.
Day/night: bank switch block refresh (not traced here).

---

## 3. Side-by-side diff

| path | stock | ours (default) | ours (stock-like) | tag |
|---|---|---|---|---|
| core `0x800` | on at sensor init, never stopped by streams | **off at last chan STREAMOFF / tx-isp STREAMOFF, full re-init (DMA rings, MDNS DMA restore) at next start** | off only at tx-isp STREAMOFF | [P][S] |
| CSI | programmed at init; stream = state only | **PHY shut down at every input stop, re-initialised at every start** | at tx-isp off/on only | [P][S] |
| sensor | stream on/off only (I2C) | **full sensor init at every VIC start** + stream on/off | at tx-isp on only | [P][S] |
| sensor vs VIC order | sensor on, then whole VIC start | VIC config, sensor on, then VIC 2/4/poll/1 | same as default | [P][S] |
| VIC `0x104/0x108` | from sensor attr | 0 | 0 | [P][S] |
| VIC `0x10` | not written (OTHER_MIPI) | `= 1` after idle | same | [P][S] |
| VIC idle poll | unbounded spin | 100 × 1-2 ms, then fails | same | [P][S] |
| VIC STREAMOFF | state only, keeps running | state only + sensor off | same | [P][S] |
| ISP IRQ mask `0xb0` | ~0 at tx-isp on, 0 at tx-isp off | written at every channel start/last stop | same | [P][S] |
| channel QBUF → FIFO | only while streaming; before STREAMON the buffers are held and written at STREAMON **before** the channel start | **written immediately, streaming or not** | same | [P][S] |
| QBUF cache | `dma_sync_single_for_device(FROM_DEVICE)` per buffer | **none** (openimp: none either) | none | [P][S] |
| FIFO clear `0xdN44/48/64/68` | **never** (no caller) | **every STREAMON of a non-enabled channel with queued buffers (input may be live), and at session release** | session release only (input stopped) | [P][S] |
| done-FIFO pop outside ISR | never | `drop_stale_done` at STREAMON, session release | same | [P][S] |
| GIB `0x1008/0x1010/0x1014/0x1060` at channel start | not written by any stream path | **written at every channel start, input live** | same | [I] (no immediate in stock) [S] |
| channel start locking | **inside `spin_lock_irqsave`**, nothing between `0xd010` and enable | mutex only, IRQs on, `printk` inside | same | [P][S] |
| `0xd040` before `0xd010` | **absolute write of all enables before the update request** | missing: `0xd010` issued with the new channel disabled, bit set afterwards (+ extra RMW) | same | [E] |
| `0xd04c` | from `msca_ch_ctl_mode` (tuning) | 0 | 0 | [P][S] |
| reload on restart | always full `tisp_channel_start` | skipped if bit kept and same geometry | full reload (stock) | [P][S] |
| STREAMOFF drain | **≤ 21 × msleep(10) until all buffers completed**, input + output running | **none**; buffers forgotten at once | **none** | [P][S] |
| STREAMOFF `0xd040` | never cleared | cleared if not the last channel (input live) | kept | [P][S] |
| core ISR FIFO drain | per status bit, any channel | per `ch_en|kept`, ignores status bits | same | [P][S] |
| flip | always `0xd050` + `0xd010` | skip if unchanged | always (stock) | [P][S] |
| barriers | none (uncached stores; the spinlock orders) | `wmb()` after VIC/irq writes; none after FIFO writes | same | [P][S] |

---

## 4. Ranking: deviations most likely to cause the bus hang / DMA to address 0

Evidence summary: every dying build has `msca_fifo_rearm` (1911bb83, in
agg-23..26 and d28a0177) and the per-wake input/core restart; the only
surviving configuration (`2354-stocklike2`) has neither, but shows
`"framechan0 unmatched MSCA completion"` 40 ms before the next
`EnsureLinkStreamOn`, i.e. the MSCA wrote frames into addresses of a
channel that was already stopped and whose pool openimp frees right after
the close (`IMP_FrameSource_DisableChn`: STREAMOFF → worker stop →
`VBMFlushFrame` → close → `VBMDestroyPool`). The openimp comment "the ISP
driver drops the buffer addresses still queued in its hardware FIFO on
release" is **not true for T23**: our close only forgets them in software.

1. **FIFO clear with a live input** — `regtrace_framechan_rearm_fifo()`
   l. 16939 (`tisp_channel_main_fifo_clear()` l. 42026), called from
   `regtrace_framechan_stream_on()` l. 16999; also
   `regtrace_t23_msca_release_stale()` l. 17084. Stock never writes these
   reset bits [P]; their semantics are unknown, and on the second
   channel start the input and the other output are live. In the rearm the
   kept-and-live case also clears `0xd040` mid-frame first. The README's
   own failure mode ("cleared FIFO → next frame to address 0") is a
   bus-hang candidate on its own [I].
   *Stock-exact fix:* `msca_fifo_rearm=0` (stock never clears); session
   release only with the input stopped (as now) or drop it too.
2. **Buffers freed while still in the FIFO of an enabled output (missing
   STREAMOFF drain wait)** — `regtrace_framechan_stream_off_locked()`
   l. 17176 → `regtrace_framechan_set_streaming(ch, false)` l. 16606. Stock
   waits ≤ 21 × 10 ms with input and output running, so the FIFO is empty
   before the pool can be freed [P][I]; we return at once and openimp
   frees the pool while the MSCA still holds its addresses. With
   `msca_keep_enabled=2` (stock-like) this happens on every DisableChn
   (the unmatched completions above); with the default, at the next
   input start. If that rmem is reused (next pool, encoder buffers,
   IVS), the MSCA overwrites live DMA structures of another engine [I].
   *Stock-exact fix:* before step 2 of 2.4 (and before the input stop when
   `last`): `for (n = 0; n < 21 && channel still has buffers marked
   queued in hardware; n++) msleep(10);` with the stream mutex dropped or
   held (stock holds no fs lock there), completions still matched
   (streaming still true), ISR running.
3. **Per-wake input/core restart under enabled outputs**
   (`chan_stop_keep_input=0`) — l. 17176 (`last` branch) →
   `regtrace_t23_source_input_stream(0)` l. 12353 (CSI PHY shut down),
   `regtrace_t23_tisp_stream_regs(0)` l. 15184 (`0x800 = 0`), and the
   reverse on the next start (core DMA rings / MDNS DMA reprogrammed,
   sensor fully re-initialised, VIC reprogrammed) while `0xd040` keeps
   outputs enabled (`msca_keep_enabled=1`). Stock does none of this at
   run time [P]. *Stock-exact fix:* `chan_stop_keep_input=1`.
4. **Output disabled mid-stream** — `regtrace_t23_set_msca_stream(ch, 0)`
   l. 16410 with `msca_keep_enabled` 0/1 on a non-last STREAMOFF, and the
   rearm in 1. Stock never clears a `0xd040` bit [P]. *Fix:*
   `msca_keep_enabled=2`.
5. **QBUF writes the FIFO of a non-streaming channel** —
   `regtrace_framechan_record_qbuf()` l. 16505. With a kept-enabled output
   and a live input the MSCA starts writing into a buffer before
   STREAMON / after STREAMOFF; together with 2. this is how stale
   addresses accumulate across a DisableChn/EnableChn. *Stock-exact fix:*
   write `0xdN30/50` only while the channel streams; at STREAMON push the
   queued slots **before** `tisp_channel_start` (stock 1.3 step 2).
6. **Channel start not atomic / update request before enable** —
   `tisp_msca_chx_cfg_load()` l. 45289 + `commit_global_window()`
   l. 45237, called from `regtrace_t23_set_msca_stream()` without IRQs
   off; `0xd040` absolute write missing before `0xd010` [E]. A frame
   boundary or the core ISR (which reads FIFO status of `ch_en|kept`
   channels) can fall between `0xd010` and strides/enable.
   *Stock-exact fix:* write `0xd040 = Σ cfg[i].en << i` before `0xd010` in
   `commit_global_window()`, and run `tisp_channel_start()` under a
   `spin_lock_irqsave` (no `printk` inside).
7. **GIB/stream regs rewritten at every channel start** —
   `regtrace_t23_tisp_stream_regs(1, ch)` l. 15184 from
   `regtrace_framechan_stream_on()`; stock has no such writes in any
   stream path [I]. *Fix:* only on the core start edge.
8. **No cache invalidate at QBUF** — l. 16505. Stock invalidates every
   buffer before it reaches the FIFO [P]; a dirty line evicted later
   overwrites DMA data (image corruption, not a bus hang) [I]. *Fix:*
   `dma_sync_single_for_device(NULL, phys, len, DMA_FROM_DEVICE)` before the
   FIFO write (or `dma_cache_inv` of the kseg0 alias).
9. VIC start details (sensor init inside VIC start, `0x104/0x108 = 0`,
   extra `0x10 = 1`, sensor-on placement) — `regtrace_t23_vic_start_repaired()`
   l. 12170. Same on the surviving path, so low for this hang.
10. ISR drain keyed on `ch_en|kept` instead of the status bits — l. 34058.
    A released output's completions stay in its done FIFO (hence
    `drop_stale_done`). Low.

Flip no-op skip and restart skip are **not** suspects: the surviving
stock-like run had both off (stock behaviour).

## 5. Bisect order for the device (cam-B, one knob per run, cold start)

Baseline = the surviving stock-like set on the current module, with
`t23_runtime_trace=1` (to finally see which channel the second
EnableChn is: chn0 after the kick DisableChn, or chn1/JPEG on a live
input) and kmsg streamed to the host. ≥ 5 min and ≥ 10 wakes each.

| run | change vs. baseline | dies → | survives → |
|---|---|---|---|
| B0 | none (repeat `2354-stocklike2`) | environment/timps, stop | baseline confirmed |
| B1 | `msca_fifo_rearm=1` | **rank 1 confirmed**: FIFO clear under live input | rank 1 cleared |
| B2 | `chan_stop_keep_input=0` (with `msca_fifo_rearm=0`) | rank 3 (input/core restart) | cleared |
| B3 | `msca_keep_enabled=1` | rank 4 (mid-stream disable) / kept bit across input stop | cleared |
| B4 | default module, only `msca_fifo_rearm=0` | rank 1 is not the only trigger, go to B2/B3 | default + rearm off is a usable interim default |

All switches are 0644, so B1-B3 can also be flipped at run time between
timps restarts, but a cold start per run is cleaner (the hang lives in
the minutes after a timps start).

Code changes for after the bisect (one build, each behind a knob, default
stock): STREAMOFF drain wait (rank 2), FIFO writes only while streaming +
push at STREAMON (rank 5), `0xd040` before `0xd010` + IRQ-off channel start
(rank 6), stream regs only on the core edge (rank 7), QBUF cache invalidate
(rank 8). Rank 2 matters even if B0 keeps surviving: the unmatched
completions in `2354-stocklike2` are writes into freed openimp pools.

## 7. Cross-check against the T30 C source (ingenic-sdk `common/isp/t30`)

The thingino ingenic-sdk tree (`build/ingenic-sdk-9e87773d…/common/isp/t30/`)
ships the T30/T21 ISP driver as C source. Its MSCA register layout
(`include/tx-mscaler-regs.h`: `MSCA_CH_EN 0x4`, `MSCA_CH_STAT 0x8`,
`CHx_DMAOUT_Y_ADDR 0x16c`, `CHx_Y_ADDR_FIFO_STA 0x170`,
`CHx_DMAOUT_Y_ADDR_CLR 0x19c`, …) does **not** match T23 (`0xd040`,
`0xdN30/38/3c/44/48`), so only the intended *ordering* carries over [I].

| topic | T30 source | T23 stock | ours | agreement |
|---|---|---|---|---|
| QBUF cache | `__buf_prepare`: `dma_sync_single_for_device(NULL, addr, len, DMA_FROM_DEVICE)` | same [P] | none | T30 = T23; ours deviates |
| QBUF → HW FIFO | `mscaler_frame_channel_qbuf`: software FIFO + `configure_channel_dma_addr()` writes only while `CHx_Y_ADDR_FIFO_STA & FULL` is clear; called via the fs enqueue, i.e. only while streaming | direct write, no FULL check, only while streaming [P] | direct write, no FULL check, **also when not streaming** | T30 ≈ T23 |
| channel start | under `spin_lock_irqsave(chan->slock)`: strides, push FIFO, **then** `MSCA_CH_EN` bit | FIFO filled at fs STREAMON, then `tisp_channel_start` under `spin_lock_irqsave` [P] | FIFO at QBUF, start with IRQs on | T30 = T23; ours deviates in locking |
| channel stop | `MSCA_CH_EN` bit cleared → **poll `MSCA_CH_STAT` bit until idle, ≤ 200 × `msleep(2)`** → software FIFO dropped → **`Y/UV_ADDR_CLR = 1` only now** | no disable, no clear; fs **drain wait ≤ 21 × `msleep(10)`** [P] | default: bit cleared on non-last stop, no idle wait; FIFO clear at the next STREAMON (rearm) — for a kept+live output **disable and clear back to back, no idle wait** | T30 and T23 both make sure the engine no longer writes before buffers go back; ours does neither |
| frame done | ISR per channel status bit: drain `LAST_ADDR` FIFO until empty, refill from the software FIFO | per status bit, drain until empty [P] | per `ch_en|kept`, ignores status bits | T30 = T23 |
| module STREAMOFF | `MSCA_IRQ_MASK = 0x1ff`, `disable_irq` | `0xb0 = 0`, `disable_irq` [P] | same + core/CSI/sensor teardown | T30 = T23 |
| vb2 STREAMOFF | `__vb2_queue_cancel` only (the wait is in the MSCA stop) | drain wait, then cancel [P] | cancel only | different place, same intent |

Consequences for section 4:
- Rank 1 is reinforced: the only vendor code that clears an address FIFO
  (T30) does it strictly **after** the output bit is off **and** the
  channel-busy status reads idle. Our rearm clears without any idle check,
  and in the kept-and-live case right after disabling the output in the
  same critical section. If a clear is kept at all, the stock-exact
  ordering is T30's: disable → wait for idle (T23: no `CH_STAT` equivalent
  known; wait for the channel's next frame-done / done-FIFO pop, or for
  the input to be stopped) → clear.
- Rank 2 is reinforced: both vendor drivers wait before STREAMOFF returns
  (T30 for the MSCA idle bit, T23 for the queue to drain); ours waits for
  nothing, and openimp frees the pool right after close.
- Rank 8 (cache): T30 and T23 agree; adding the invalidate is stock-exact.

## 6. Reproduce

```
objdump -dr tx-isp-t23.ko > stock.dis; nm -n tx-isp-t23.ko > stock.nm
audit/dis_annotate.py stock.dis stock.nm > stock.ann     # symbols for %hi/%lo pairs
audit/dis_strings.py tx-isp-t23.ko > lc.map               # $LC string per .text offset
audit/seq_emu.py tx-isp-t23.ko <built>.ko 0               # channel 0 start MMIO order
audit/seq_emu.py tx-isp-t23.ko <built>.ko 1 live          # channel 1 start, channel 0 live
```

Addresses used above (stock): `tx_isp_video_s_stream` 0x103c0,
`tx_isp_video_link_stream` 0x104c0, `tx_isp_create_graph_and_nodes`
0x12a20, `tx_isp_vic_start` 0x290, `vic_core_s_stream` 0x1894,
`vin_s_stream` 0x3b24, `csi_video_s_stream` 0x582c,
`__enqueue_in_driver` 0xd62c, `__vb2_queue_cancel` 0xd6ac,
`__frame_channel_vb2_streamoff` 0xd7b8, `__vb2_queue_free` 0xd8f8,
`frame_channel_release` 0xdadc, `frame_chan_event` 0xe0a8,
`frame_channel_unlocked_ioctl` 0xeb44, `tisp_channel_start` 0x167a4,
`tisp_channel_main_fifo_clear` 0x16824, `tisp_msca_scaling_algorithm`
0x19938, `tisp_msca_write_reg` 0x19a70, `tisp_msca_chx_cfg_load` 0x19e48,
`tisp_msca_api_set_mirr_flip` 0x1b22c, `tisp_day_or_night_s_ctrl`
0x64c48, `tisp_hv_flip_enable` 0x65544, `ispcore_frame_channel_streamoff`
0x687fc, `ispcore_pad_event_handle` 0x69b70 (jump table `.rodata+0x1904`,
events 0x3000001..8), `exception_handle` 0x6a6c0, `ispcore_video_s_stream`
0x6a818, `ispcore_interrupt_service_routine` 0x6b7e4.
