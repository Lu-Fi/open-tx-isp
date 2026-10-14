# T41 OEM stack-frame layout (translation class)

Same class as `driver/t40/audit/stack_frames.md` (open-tx-isp `claude/t40-stack-frames-list`): the recovered locals are named after their OEM stack slot (`local_20` = `sp + 0x20`), the OEM code passes such a slot as the start of a whole struct, and the C translation declares each slot as its own 4-byte scalar, so a copy of more than 4 bytes to `&local_XX` overwrites the neighbouring stack (on T40 the stock VIDIOC_QBUF branch did this and oopsed with epc = ra = 0).

Fix approach (not implemented here): per affected function, back the OEM slots with one byte buffer and map `local_XX` to offset `0xXX`; large areas in static or heap storage with locking.

Scan of `claude/agg39` (b7a284b8), `driver/t41/tx_isp_t41_recovered.c`: copies, fills or memset/memcpy to `&local_XX` with more than 4 bytes or an unknown size, `local_XX` declared as a scalar. **64 sites in 12 reachable functions, 96 sites in 45 dormant functions** (reachability: call graph of the built object with `-ffunction-sections`, roots init/exit, exports, address-taken; 733 of 1652 functions). Size `?` = not a constant. Hand-written replacements of a function (code before the recovered body that returns early) are not taken into account, so a listed site can sit in a body that no longer runs.

Note on the ioctl paths: on T41 the hand-written front part of `frame_channel_unlocked_ioctl` handles SET_FORMAT, GET_FORMAT, REQBUFS, QUERYBUF/QBUF and DQBUF (`t41_frame_channel_*_clean`) before the recovered body, so the 68-byte QBUF copy to `&local_20` (line 30767, the site that oopsed on T40) is not reached on T41; the remaining recovered branches of that function run only for the other commands. `tx_isp_unlocked_ioctl` likewise returns early from regtrace handlers for the commands they cover. A site in these two functions matters only for commands that still fall through to the recovered body.

## Reachable functions

| function | line (b7a284b8) | call | slot | bytes |
|---|---|---|---|---|
| `tx_isp_unlocked_ioctl` | 37412 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 37475 | `copy_from_user` | `local_70` | 12 |
| `tx_isp_unlocked_ioctl` | 37505 | `copy_from_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 37663 | `copy_from_user` | `local_70` | 24 |
| `tx_isp_unlocked_ioctl` | 37717 | `copy_from_user` | `local_20` | 42 |
| `tx_isp_unlocked_ioctl` | 37765 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 37794 | `copy_to_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 37823 | `copy_from_user` | `local_20` | 64 |
| `tx_isp_unlocked_ioctl` | 37858 | `copy_from_user` | `local_70` | 8 |
| `tx_isp_unlocked_ioctl` | 37900 | `memcpy` | `local_20` | 16 |
| `tx_isp_unlocked_ioctl` | 37903 | `memcpy` | `local_30` | 16 |
| `tx_isp_unlocked_ioctl` | 37906 | `memcpy` | `local_40` | 16 |
| `tx_isp_unlocked_ioctl` | 37981 | `copy_from_user` | `local_20` | 80 |
| `tx_isp_unlocked_ioctl` | 38010 | `copy_to_user` | `local_20` | 80 |
| `tx_isp_unlocked_ioctl` | 38079 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 38108 | `copy_to_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 38234 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 38544 | `copy_from_user` | `local_20` | 64 |
| `tx_isp_unlocked_ioctl` | 38573 | `copy_to_user` | `local_20` | 64 |
| `tx_isp_unlocked_ioctl` | 38903 | `copy_from_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 39020 | `copy_to_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 39061 | `copy_from_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 39288 | `copy_to_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 39561 | `copy_from_user` | `local_20` | 56 |
| `tx_isp_unlocked_ioctl` | 39602 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 39649 | `copy_from_user` | `local_20` | 40 |
| `tx_isp_unlocked_ioctl` | 39684 | `copy_from_user` | `local_20` | 40 |
| `tx_isp_unlocked_ioctl` | 39715 | `copy_to_user` | `local_20` | 40 |
| `tx_isp_unlocked_ioctl` | 40015 | `copy_to_user` | `local_70` | 24 |
| `tx_isp_unlocked_ioctl` | 40118 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 40186 | `copy_from_user` | `local_20` | 42 |
| `frame_channel_unlocked_ioctl` | 30721 | `copy_from_user` | `local_20` | 8 |
| `frame_channel_unlocked_ioctl` | 30767 | `copy_from_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 30831 | `copy_from_user` | `local_20` | 20 |
| `frame_channel_unlocked_ioctl` | 31154 | `copy_to_user` | `local_20` | 20 |
| `frame_channel_unlocked_ioctl` | 31242 | `__fill_tisp_buffer` | `local_20` | ? |
| `frame_channel_unlocked_ioctl` | 31245 | `copy_to_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 31533 | `__fill_tisp_buffer` | `local_20` | ? |
| `frame_channel_unlocked_ioctl` | 31539 | `copy_to_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 31574 | `copy_from_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 31673 | `__fill_tisp_buffer` | `local_20` | ? |
| `frame_channel_unlocked_ioctl` | 31712 | `copy_to_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 32250 | `copy_from_user` | `local_20` | 68 |
| `Tiziano_wdr_fusion_fpga` | 91591 | `memcpy` | `local_20` | 320 |
| `Tiziano_wdr_fusion_fpga` | 91605 | `memcpy` | `local_160` | 50 |
| `Tiziano_wdr_fusion_fpga` | 91635 | `memcpy` | `local_160` | 50 |
| `Tiziano_wdr_fusion_fpga` | 94479 | `memcpy` | `local_160` | 50 |
| `tisp_lce_init` | 109704 | `memcpy` | `local_10` | 36 |
| `tisp_lce_init` | 109931 | `memcpy` | `local_58` | 16 |
| `tisp_lce_init` | 109934 | `memcpy` | `local_34` | 36 |
| `tisp_lce_init` | 109937 | `memcpy` | `local_10` | 36 |
| `isp_core_tunning_unlocked_ioctl_body` | 22963 | `copy_from_user` | `local_10` | 8 |
| `isp_core_tunning_unlocked_ioctl_body` | 22990 | `copy_from_user` | `local_10` | 8 |
| `isp_core_tunning_unlocked_ioctl_body` | 23003 | `copy_to_user` | `local_10` | 8 |
| `tisp_ae_fliker_detect` | 70621 | `memcpy` | `local_2e0` | 40 |
| `tisp_ae_fliker_detect` | 70630 | `memcpy` | `local_308` | 12 |
| `tisp_ae_fliker_detect` | 70654 | `memcpy` | `local_308` | 12 |
| `tisp_tmo_fpga` | 152255 | `memset` | `local_d8` | ? |
| `tisp_tmo_fpga` | 152264 | `memcpy` | `local_12c` | 20 |
| `isp_frame_done_wakeup` | 23172 | `memcpy` | `local_10` | 8 |
| `ispcore_core_ops_init` | 168089 | `memset` | `local_28` | 160 |
| `ispcore_frame_channel_s_fmt_isra_2` | 164632 | `memset` | `local_18` | 52 |
| `tx_isp_video_s_stream` | 35561 | `copy_from_user` | `local_18` | ? |
| `vic_frame_channel_get_fmt` | 7671 | `memcpy` | `local_10` | 116 |

## Dormant functions

| function | line (b7a284b8) | call | slot | bytes |
|---|---|---|---|---|
| `tx_isp_core_ops_g_ctrl` | 18742 | `memset` | `local_18` | 16 |
| `tx_isp_core_ops_g_ctrl` | 19098 | `copy_from_user` | `local_18` | 24 |
| `tx_isp_core_ops_g_ctrl` | 19167 | `copy_to_user` | `local_18` | 6 |
| `tx_isp_core_ops_g_ctrl` | 19299 | `copy_to_user` | `local_18` | 48 |
| `tx_isp_core_ops_g_ctrl` | 19388 | `memset` | `local_18` | 16 |
| `tx_isp_core_ops_g_ctrl` | 19463 | `copy_to_user` | `local_18` | 16 |
| `tx_isp_core_ops_g_ctrl` | 19523 | `copy_to_user` | `local_18` | 92 |
| `tx_isp_core_ops_g_ctrl` | 19549 | `copy_to_user` | `local_18` | 40 |
| `tx_isp_core_ops_g_ctrl` | 19608 | `copy_to_user` | `local_18` | 8 |
| `tx_isp_core_ops_g_ctrl` | 19629 | `copy_to_user` | `local_18` | 60 |
| `tx_isp_core_ops_g_ctrl` | 19714 | `copy_to_user` | `local_18` | 24 |
| `tx_isp_core_ops_g_ctrl` | 19738 | `copy_to_user` | `local_18` | 24 |
| `tx_isp_core_ops_g_ctrl` | 19775 | `copy_to_user` | `local_18` | 12 |
| `tx_isp_core_ops_g_ctrl` | 19796 | `copy_to_user` | `local_18` | 20 |
| `tx_isp_core_ops_g_ctrl` | 19833 | `copy_to_user` | `local_18` | 32 |
| `tx_isp_core_ops_g_ctrl` | 19929 | `copy_to_user` | `local_18` | 16 |
| `tx_isp_core_ops_g_ctrl` | 19950 | `copy_to_user` | `local_18` | 52 |
| `tx_isp_core_ops_g_ctrl` | 20080 | `copy_to_user` | `local_18` | 72 |
| `tx_isp_core_ops_g_ctrl` | 20125 | `copy_to_user` | `local_18` | 12 |
| `tx_isp_core_ops_g_ctrl` | 20162 | `copy_to_user` | `local_18` | 12 |
| `tx_isp_core_ops_g_ctrl` | 20183 | `copy_to_user` | `local_18` | 24 |
| `tx_isp_core_ops_g_ctrl` | 20205 | `memset` | `local_18` | 16 |
| `tx_isp_core_ops_g_ctrl` | 20249 | `memcpy` | `local_1c` | 12 |
| `tx_isp_core_ops_g_ctrl` | 20303 | `copy_to_user` | `local_18` | 12 |
| `tx_isp_core_ops_g_ctrl` | 20335 | `copy_to_user` | `local_18` | 8 |
| `tx_isp_core_ops_g_ctrl` | 20450 | `copy_from_user` | `local_18` | 12 |
| `tx_isp_core_ops_g_ctrl` | 20482 | `copy_to_user` | `local_18` | 12 |
| `tx_isp_core_ops_g_ctrl` | 20514 | `copy_to_user` | `local_18` | 28 |
| `tx_isp_core_ops_g_ctrl` | 20554 | `copy_to_user` | `local_78` | 8 |
| `tx_isp_core_ops_g_ctrl` | 20595 | `copy_to_user` | `local_78` | 20 |
| `tisp_bypass_update` | 53469 | `memset` | `local_20` | ? |
| `tisp_bypass_update` | 53475 | `memset` | `local_20` | ? |
| `tisp_bypass_update` | 53479 | `memset` | `local_20` | ? |
| `tisp_bypass_update` | 53485 | `memset` | `local_20` | ? |
| `tisp_bypass_update` | 53491 | `memset` | `local_20` | ? |
| `tiziano_adr_5x5_init` | 118099 | `memcpy` | `local_98` | 32 |
| `tiziano_adr_5x5_init` | 118102 | `memcpy` | `local_78` | 32 |
| `tiziano_adr_5x5_init` | 118105 | `memcpy` | `local_58` | 32 |
| `tiziano_adr_5x5_init` | 118108 | `memcpy` | `local_38` | 32 |
| `tiziano_adr_5x5_init` | 118111 | `memcpy` | `local_18` | 32 |
| `tiziano_adr_base_pars` | 118531 | `memcpy` | `local_60` | ? |
| `tiziano_adr_base_pars` | 118532 | `memcpy` | `local_60` | 10 |
| `tiziano_adr_base_pars` | 118534 | `memcpy` | `local_50` | ? |
| `tiziano_adr_base_pars` | 118535 | `memcpy` | `local_50` | 14 |
| `Tisp_lce_soft` | 106550 | `memset` | `local_68` | 72 |
| `Tisp_lce_soft` | 106899 | `memcpy` | `local_f0` | 64 |
| `Tisp_lce_soft` | 106919 | `memcpy` | `local_f0` | 64 |
| `tisp_ae_algo_init` | 74885 | `memcpy` | `local_24` | ? |
| `tisp_ae_algo_init` | 74888 | `memcpy` | `local_10` | 24 |
| `tisp_bcsh_H_adjust` | 137711 | `memcpy` | `local_60` | 36 |
| `tisp_bcsh_H_adjust` | 137720 | `memcpy` | `local_84` | 20 |
| `tisp_msca_normalized` | 140617 | `memcpy` | `local_30` | 32 |
| `tisp_msca_normalized` | 140621 | `memcpy` | `local_10` | 32 |
| `tisp_s_raw_row_control` | 160280 | `memcpy` | `local_20` | 16 |
| `tisp_s_raw_row_control` | 160283 | `memcpy` | `local_30` | 16 |
| `tisp_s_raw_rw_control_internal` | 160157 | `memset` | `local_10` | 64 |
| `tisp_s_raw_rw_control_internal` | 160191 | `memcpy` | `local_40` | 16 |
| `tx_isp_get_default_bin_path_isra_25` | 36590 | `copy_from_user` | `local_18` | ? |
| `tx_isp_get_default_bin_path_isra_25` | 36637 | `copy_to_user` | `local_18` | 132 |
| `tx_isp_set_ae_algo_open_isra_23` | 36415 | `copy_from_user` | `local_18` | ? |
| `tx_isp_set_ae_algo_open_isra_23` | 36524 | `copy_to_user` | `local_18` | 136 |
| `tx_isp_set_default_bin_path_isra_16` | 36150 | `copy_from_user` | `local_18` | ? |
| `tx_isp_set_default_bin_path_isra_16` | 36178 | `memcpy` | `local_18` | 132 |
| `video_input_cmd_set` | 14473 | `memcpy` | `local_18` | 32 |
| `video_input_cmd_set` | 14601 | `memcpy` | `local_18` | 32 |
| `isp_core_tunning_default_ioctl_isra_80` | 20744 | `copy_to_user` | `local_10` | 16 |
| `isp_save_cmd_set` | 162864 | `memcpy` | `local_18` | ? |
| `lce_self_light_correct` | 105111 | `memcpy` | `local_194` | 20 |
| `tisp_ae_get_bv` | 60411 | `memset` | `local_20` | ? |
| `tisp_defog_update_trans_k` | 111737 | `memcpy` | `local_20` | 5 |
| `tisp_get_awb_info` | 43429 | `memcpy` | `local_10` | 28 |
| `tisp_s_defog_ratio` | 115479 | `memcpy` | `local_20` | 5 |
| `tisp_s_raw_rw_control` | 160022 | `memset` | `local_10` | 64 |
| `tisp_set_awb_info` | 43458 | `memcpy` | `local_10` | 28 |
| `tiziano_adr_algorithm` | 124349 | `memcpy` | `local_18` | 14 |
| `tx_isp_ae_expoinfo_g_attr_isra_55` | 17723 | `copy_to_user` | `local_18` | ? |
| `tx_isp_ae_expoinfo_s_attr_isra_20` | 17194 | `copy_from_user` | `local_18` | ? |
| `tx_isp_ae_g_at_list_isra_70` | 18002 | `copy_to_user` | `local_18` | 128 |
| `tx_isp_ae_g_ev_list_isra_69` | 17945 | `copy_to_user` | `local_18` | 128 |
| `tx_isp_ae_g_explist_isra_63` | 17888 | `copy_to_user` | `local_18` | 324 |
| `tx_isp_ae_s_explist_isra_24` | 17311 | `copy_from_user` | `local_18` | ? |
| `tx_isp_ae_weight_g_attr_isra_58` | 17789 | `copy_to_user` | `local_18` | ? |
| `tx_isp_ae_weight_s_attr_isra_21` | 17254 | `copy_from_user` | `local_18` | ? |
| `tx_isp_af_weight_g_attr_isra_49` | 17525 | `copy_to_user` | `local_18` | ? |
| `tx_isp_af_weight_s_attr_isra_16` | 17080 | `copy_from_user` | `local_18` | ? |
| `tx_isp_ai_g_ratio_table_isra_75` | 18108 | `copy_to_user` | `local_18` | 80 |
| `tx_isp_awb_attr_g_attr_isra_51` | 17657 | `copy_to_user` | `local_18` | ? |
| `tx_isp_awb_weight_g_attr_isra_50` | 17591 | `copy_to_user` | `local_18` | ? |
| `tx_isp_awb_weight_s_attr_isra_17` | 17137 | `copy_from_user` | `local_18` | ? |
| `tx_isp_gamma_g_attr_isra_42` | 17459 | `copy_to_user` | `local_18` | ? |
| `tx_isp_gamma_s_attr_isra_9` | 16946 | `copy_from_user` | `local_18` | ? |
| `tx_isp_module_ratio_g_attr_isra_60` | 17831 | `copy_to_user` | `local_18` | 128 |
| `tx_isp_module_ratio_s_attr_isra_10` | 17012 | `copy_from_user` | `local_18` | ? |
| `tx_isp_switch_bin_isra_78` | 18321 | `copy_from_user` | `local_18` | 132 |
| `tx_isp_tmo_g_curve_isra_72` | 18059 | `copy_to_user` | `local_18` | 404 |
| `tx_isp_tmo_s_curve_isra_34` | 17368 | `copy_from_user` | `local_18` | ? |

