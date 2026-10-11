# T41 tuning controls (isp-m0, envelope `{channel, is_get, id, ptr}`)

Controls added on top of the AE/AWB/BCSH/flip set. Stock reference:
`tx_isp_core_ops_s_ctrl` / `g_ctrl` of the vendor `tx-isp-t41.ko`; the wire
structures below are what that module copies from user space. OpenIMP builds
them (`src/t40/openimp_p3_controls.c`); the vendor libimp 1.2.5/1.2.6 sends a
different CCM/CSC payload (Q10/10-bit words, 12-word CSC) than this kernel
reads (Q16 words, 92-byte CSC table), so for those two the module follows the
kernel, not the library.

| id | function | wire | notes |
|---|---|---|---|
| 0x08000072 | Set/GetModuleControl | u32 key | 25-bit TOP bypass word, 1 = bypass. Bits kept: the upper 7 reserved bits. Accepted: LSC 1, DPC 4, ADR 7, CCM 9, GAMMA 10, DEFOG 11, MDNS 13, YDNS 14, BCSH 15, YSP 17, SDNS 18; a key that changes any other bit gets -EOPNOTSUPP. A bit that differs from the calibrated bypass byte is an override that the modules' own calibration restore keeps (`t41_modctl_mask/value`). MDNS leaves bypass only when its buffers exist. |
| 0x08000025 | Set/GetGammaAttr | u32 type + u16[129] (264 B) | type 0 = exposure-driven calibration curve, 1 sRGB, 2 Rec709, 3 HDR, 4 user (points <= 4095). Types 1..4 fix the curve (strength 255, tone worker leaves it alone); get returns the stored request with the live curve. |
| 0x08000080 | Set/GetCCMAttr | u8 manual, u8 sat, 2 pad, s32[9] Q16 (40 B) | ManualEn replaces the selected matrix with the rounded Q10 user matrix; SatEn = 0 skips the saturation transform. The matrix is applied in the CCM block and in the BCSH matrix (the shipped calibrations may carry the colour correction in BCSH; stock `tisp_bcsh_api_set_ccm` only stores it). |
| 0x08000096 | Set/GetISPCSCAttr | s32 version + s32[9] RGB->YUV Q16 + 2 byte words + s32[9] YUV->RGB Q16 + 2 words (92 B) | versions 0..5 presets (BT601 full/limited, BT709 full/limited, BT2020 full/limited), 6 = user table. Re-programs the CSC registers and refreshes CCM and BCSH. |
| 0x080000a4 | Set/GetModule_Ratio | 16 x {u32 en, u8 ratio} | SINTER, TEMPER as before; DRC (scales the ADR strength fields from the pristine copy), DPC (long-bank thresholds). DEFOG is still -EOPNOTSUPP for a non-neutral request. |
| 0x08000023 | Set/GetAeExprInfo | 232 B | additionally: AeMode (freeze), manual integration time (lines or microseconds, unit field) and manual analog gain (x1024), sensor digital gain cap (accepted and reported; the open AE only allocates the sensor analog gain). Manual sensor/ISP digital gain and minimum caps stay -EOPNOTSUPP. |

| 0x08000077 | Set/GetAutoZoom | s32 en[3], left[3], top[3], width[3], height[3] (60 B) | input crop window per output (stock `tisp_s_autozoom_control`); enabled windows outside the sensor are -EINVAL. Streaming outputs are only reprogrammed with `t41_msca_cfg_update=2` + `stop_disable=1` and output <= 768x432, otherwise stored for the next start. Branch `claude/t41-zoom-mask`, see `driver/t41/README.md` "MSCA zoom, mask and scaler level". |
| 0x08000074 | Set/GetMaskBlock | u8 chx, pinum, en, pad, u16 top, left, w, h, s32 type, rgb, yuv (24 B) | 3 x 4 blocks, written through the MSCA shadow port like stock `tisp_msca_set_mask`; same live/stored rule. GET reports en 0 (stock). |
| 0x080000a6 | SetScalerLv | u8 chx, s32 mode, u8 level (12 B) | FIXED_WEIGHT / FITTING_CURVE level 0..128 (`0xf0708 + ch * 8`); the level is kept across reloads (stock drops it). Same live/stored rule. |

Not routed yet (-EOPNOTSUPP): WdrOutputMode (0x08000054).
