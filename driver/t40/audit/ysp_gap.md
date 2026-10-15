# T40 YSP (sharpening, TOP bit 17): chain check

Desk check of open-tx-isp `claude/t40-gates-offset` (06508ce5 + gates offset + 0x6c) against the
OEM sc830ai module (t40prod `tx-isp-t40.ko`). No device run.

## 1. Is the chain faithful?

| function | OEM | ours | result |
|---|---|---|---|
| `tisp_ysp_noref_reg_cfg` | 0x649d0 | literal | identical: regenerated with `tools/mips2c_literal.py`, 0 lines differ |
| `tisp_ysp_ref_reg_cfg` | 0x64da8 | literal | identical (1524 lines) |
| `tisp_ysp_intp` | 0x66480 | literal | identical (516 lines) |
| `tisp_ysp_wdr_en` | 0x66c84 | literal | identical (443 lines); wdr flags in a private `[2]` array |
| `tisp_ysp_reg_trig` | ((ch+0x98)<<9)+0x78 = 1 | same | ok |
| `tisp_ysp_params_refresh` | memcpy 3432 B from bank +0x16d98 | same | ok |
| `tisp_ysp_main_init` | vmalloc 3432/420/146, params, wdr_en, gain_old=-1, refresh(0,0x10000), par_refresh(0,0x10000,0x10000) | same | ok |
| `tisp_ysp_par_refresh` | first value -> all_reg_refresh, later -> **intp_reg_refresh** | always all_reg_refresh | minor deviation (fixed) |
| `tisp_ysp_intp_reg_refresh` | intp -> ref_reg_cfg -> reg_trig | = all_reg_refresh (writes noref too) | minor deviation (fixed) |

Emulated stack frames are large enough (OEM frames 24-56 B; buffer 96 B below sp, deepest access sp+52).
No pointer-scaling or stack-frame defect was found in the chain.

## 2. What was the old "YSP panic"?

- README r22a/r24a: both boots ran **without a tuning bin** (the bin was only added later). The panic
  left no kmsg.
- Without a bin, `tisp_ysp_params_refresh` returns -EFAULT, so `tisp_ysp_main_init` fails after
  allocating the buffers but before `tisp_ysp_wdr_en`. The 105 comb pointers therefore stay NULL.
- The frame-done path (`ysp_ev` change) still called `tisp_ysp_par_refresh` -> `all_reg_refresh` ->
  `tisp_ysp_intp`, which reads through those NULL comb pointers. Unlike MDNS/SDNS, which check their
  `*_dbg_init_ret`, YSP had no init guard. That is a NULL dereference in workqueue context at every
  gain change. README:511 ("NULL comb buffers") points the same way.
- It was not the TOP override class, since YSP has a real init. It was also not the gates-offset
  error directly. But the gates offset plays a part: with the old +0x20 (WDR) set, the sc830ai bin
  bypasses YSP (`flags[17]=1`), while the linear set wants it active (`flags[17]=0`). So even with a
  bin and no `force_bypass`, YSP would have stayed off.
- Not verified: README:365 alternatively suggests a bus hang from YSP register writes while bit 17
  still bypasses. The OEM writes YSP registers in `tisp_main_init` before it sets TOP, so writes while
  bypassed are the stock order. That makes this explanation unlikely.

## 3. Patch (on dcf547eb)

- Init guard: `tisp_ysp_main_init` sets `ysp_init_ret` to 0 only after params and wdr_en.
  `all_reg_refresh` and `intp_reg_refresh` return while init has not succeeded (counted in
  `ysp_refresh_blocked`). This always applies; it is harmless because YSP is off by default.
- `par_refresh` / `intp_reg_refresh` now follow the OEM (steady state: intp + ref_reg_cfg + trig).
- `t40_ysp_on=1` (default 0) lets the bin gates enable YSP despite the `t40_force_bypass` 0x20000
  default. TOP bit 17 is cleared only when init returned 0, at block init and on day/night.
- Read-only: `ysp_init_ret`, `ysp_refresh_count`, `ysp_refresh_blocked`.
- Test order: needs the bin and `t40_gates_oem_offset=1` (otherwise the WDR set keeps YSP bypassed);
  then `t40_ysp_on=1`. Expected kmsg: `isp-block-init ysp ret=0`, `ysp-main-init done`. TOP bit 17
  should be clear and `ysp_refresh_count` should rise with gain changes.
