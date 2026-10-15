# T40 chroma gap (IR-cut in): vendor vs open driver

Desk analysis of open-tx-isp `claude/t40-sc830ai` 06508ce5, default insmod line (bring-up profile,
`t40_quality_profile=1`, `t40_force_bypass=0x20000`, `t40_force_active=0x6020`). No device run.
Symptom: with the IR-cut filter in (colour mode) the vendor stack shows about 4x less chroma noise
(Csat 0.56/0.58 vs 2.71/2.22).

## 1. How the vendor drives chroma per gain/EV

The OEM AE chain pushes two kinds of events per exposure change. Both callbacks read TOP 0x40 and
skip every block whose bypass bit is set:

| OEM callback (event) | input | refreshes |
|---|---|---|
| `tisp_main_long_tgain_update` (6) | total gain, log2 16.16 | YSP, YDNS, **CDNS** (`tisp_cdns_refresh`), SDNS, MDNS |
| `tisp_main_long_ev_update` (8) | AE EV | gamma, AWB, **CCM** (`tisp_ccm_main_ev_update`), ADR, defog, **BCSH** (`tisp_bcsh_main_ev_update`) |
| `tisp_ct_update` (13) | colour temperature | CCM/BCSH CT side |

In each block the tuning bin's per-EV/per-gain table is interpolated: `*_par_refresh` -> `*_intp`
-> `*_all_reg_refresh`. For chroma this lowers the CCM and BCSH saturation and raises the CDNS
strength as gain grows.

TOP word, bit set = bypass (map: docs/T40_TUNING_HURDLES.md "Top40 ground truth"):

| | TOP | SDNS (18) | CDNS (19) | YSP (17) | LCE (21) | DPC (4) | ADR (7) |
|---|---|---|---|---|---|---|---|
| vendor | 0x7FD0000D | active | **active** | active | active | active | active |
| open (Eufy, README) | 0x7FFF08D8 | bypass | **bypass** | bypass | bypass | bypass | bypass |

## 2. Status in the open driver

| function (OEM) | status | evidence |
|---|---|---|
| event chain 6/8/13 (`tisp_main_long_tgain_update`, `tisp_main_long_ev_update`, `tisp_ct_update`) | **dead** | Registered by `tisp_stream_on` (71168), but only events 14 (AWB, 101669) and 2 (ADR, 144701) are ever pushed. Nothing pushes 1/6/7/8/9/10/12/13 (DeepSeek's push/process patch covers that loop). |
| `tisp_cdns_main_init` / `params_refresh` / `par_refresh` / `refresh` / `intp` / `reg_cfg` | **dead + broken lift** | Not called from the live init (`regtrace_isp_block_init_once`, 15265) or from `regtrace_t40_refresh_daynight_pipeline` (22108). The only caller is the unused fragment `tisp_main_init` (75603). The lift is also wrong: the tail calls `jr.hb t9` to `tisp_cdns_all_reg_refresh` are "unmatched fragments", so registers are never written. `tisp_cdns_params_refresh` (model output) memcpys 683 B onto `&tparamsN[arg1]` itself. `tisp_cdns_par_refresh` stores through `&sclk_name - 14092` (pointer-scaling class). TOP bit 19 stays set, so CDNS is bypassed in hardware. |
| SDNS (`regtrace_sdns_main_init_lit`, `tisp_sdns_par_refresh_lit`) | **runs only if `enable_sdns`**; bypassed on the Eufy | `regtrace_t40_apply_raw_tuning_gates` (11637) sets `enable_sdns = quality>=1 && !flags[18]`, so the bin's day flag decides. The Eufy TOP word has bit 18 set, while the vendor runs SDNS with the same bin. The 06508ce5 `tuning gates ... SDNS=%u` kmsg line shows the current state. When it runs, gain tracking works (`dns_ev` per frame, 48937). |
| CCM saturation per EV (`tisp_ccm_main_ev_update`, OEM 0x70150) | **static value** | Live path `regtrace_ccm_main_ct_update_lit` (11455) feeds `regtrace_ccm_ev`, a module parameter fixed at 400 (8956). `regtrace_gc4653_tuning_again_update` (11938) updates BLC/GIB/DMSC/LSC/DNS/YSP gain but not `ccm_ev`. The CCM stays on its bright-scene matrix at every gain. |
| BCSH saturation per EV (`tisp_bcsh_main_ev_update`, OEM 0x6a900) | **static value** | Lift is an empty body (170715). `regtrace_bcsh_main_init_lit` writes `regtrace_bcsh_ev` (400, bcsh_lit.inc:8) into EV slot `pstMainBcshOri_lit[38]` once, and nothing updates it. The comment at bcsh_lit.inc:3728 says so. `regtrace_isp_m0_apply_saturation` only changes the user saturation (slot 48, default 128) and calls `tisp_bcsh_ev_update_lit` with the stale EV. |
| CCM/BCSH CT (`tisp_ct_update`) | static unless `ccm_ct_track` | CT comes from module parameters (`ccm_ct`, `bcsh_ct` 6000). This is secondary for chroma noise. |
| YDNS / MDNS (luma, for comparison) | runs, gain-tracked | 48918-48940 |
| gain source `regtrace_gc4653_again_log2_gain` | runs, GC4653 table | 26-entry GC4653 log2 LUT, also used for the sc830ai index. Check that the sc830ai again index maps to the same 1/4-stop steps. If it doesn't, every gain-tracked block (incl. SDNS) interpolates at the wrong gain. |

No stack-frame defect was found on these paths. The live chroma helpers (`*_lit`) use globals and
the pstMain* slot arrays. The pointer-scaling defect is in the dead CDNS lift (see above).

## 3. Fix proposals (one per real function, no register override)

1. **CDNS: source-derived `regtrace_cdns_main_init_lit()` plus `tisp_cdns_par_refresh_lit(ch, tgain, thresh)`**, following the SDNS lit port (`tx_isp_t40_sdns_lit.inc`).
   - Translate OEM 0x618e4/0x61844/0x617f0/0x61268/0x60fd0 from the disassembly: `main_cdns` 683 B from bank + 0x11b8b (OEM sc830ai module; the lift's 73771 is wrong), comb 120 B, intp 32 B, `wdr_en`, then `all_reg_refresh`.
   - Call init from `regtrace_isp_block_init_once` and the day/night refresh.
   - Call `par_refresh` next to the SDNS call in the frame-done DNS block with `dns_ev`, which has the same 16.16 tgain unit the OEM tgain callback uses.
   - Clear TOP bit 19 only when init returned 0, as SDNS does with bit 18.
   - Add a parameter `enable_cdns`, default 0 until a device run. Expected to be the largest single gain.
2. **SDNS: make the Eufy run it.**
   - Read the `tuning gates` kmsg line on the Eufy.
   - If `flags[18]` comes from the bin, follow the vendor: it runs SDNS with the same bin, so check whether the vendor reads the gate from a different bank offset than `nbuf + 0x20`.
   - Otherwise find what sets bit 18 after the gate.
   - README 570 "SDNS initialiser not run" predates `regtrace_sdns_main_init_lit`, so it is obsolete.
3. **CCM: feed the AE EV into `regtrace_ccm_ev`** from the same place that calls `regtrace_gc4653_tuning_again_update`. The value is in OEM EV units (the `tisp_main_long_ev_update` argument), not log2 gain. Derive the unit from OEM `tisp_ae_main_process` (the event 8 payload) before wiring it; the default 400 is the bright end.
4. **BCSH: same EV into `pstMainBcshOri_lit[38]`, then `tisp_bcsh_ev_update_lit`** when the change passes the OEM threshold slot. This is the body of `tisp_bcsh_main_ev_update`; port it as `_lit`, since the lift is empty. The user saturation (slot 48) stays a separate input.

Implemented: 1 -> `claude/t40-cdns` 5215fb6f (enable_cdns, default 0). SDNS: bit 18 stays bypassed on purpose (black frame with SDNS active on 2026-10-13); a desk cause for the difference to the vendor is still open.

Order for device tests: 1 (CDNS), then 2 (SDNS); each should move Csat on its own. 3 and 4 need
the EV unit first and can share one helper (`regtrace_t40_ev_fanout(ev)`). Once DeepSeek's event
loop delivers events 6/8, that helper is replaced by the OEM callbacks. The `_lit` bodies stay
valid either way.
