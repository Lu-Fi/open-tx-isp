# T40 OEM stack-frame layout (translation class)

The recovered locals are named after their slot in the OEM stack frame (`local_20` = `sp + 0x20`). The OEM code passes such a slot as the start of a whole struct (`copy_from_user(sp + 0x20, arg, 68)`, `__fill_tisp_buffer(..., sp + 0x20)`), but the C translation declares every slot as its own 4-byte scalar. A copy of more than 4 bytes to `&local_XX` therefore overwrites whatever the compiler placed next to that scalar, including saved registers and the return address.

Found while bisecting `claude/t40-ptr-scaling`: with the ioctl command constants of `frame_channel_unlocked_ioctl` cast correctly, the stock `VIDIOC_QBUF` branch runs for the first time and copies 68 bytes to `&local_20` (line 52294 at 434bfcf0); t40_devtest then oopses with epc = ra = 0. On the base the wrong constants kept that branch from running.

Fix approach (not implemented here): per affected function, back the OEM slots with one byte buffer and map `local_XX` to offset `0xXX`, so struct copies land as in the OEM frame; large areas (several KB) belong in static or heap storage with locking, not on the kernel stack.

Scan of 434bfcf0 (copies, fills or memset/memcpy to `&local_XX` with more than 4 bytes or an unknown size, `local_XX` declared as a scalar): **116 sites in 45 reachable functions, 39 sites in 18 dormant functions** (reachability: call graph of the `-ffunction-sections` analysis build, see `ptr_scaling.md`). Size `?` = taken from a variable or a struct size the scan does not resolve.

## Reachable functions

| function | line (434bfcf0) | call | slot | bytes |
|---|---|---|---|---|
| `tx_isp_unlocked_ioctl` | 56767 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 56796 | `copy_to_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 56834 | `copy_from_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 56888 | `copy_from_user` | `local_20` | 64 |
| `tx_isp_unlocked_ioctl` | 56917 | `copy_to_user` | `local_20` | 64 |
| `tx_isp_unlocked_ioctl` | 56957 | `copy_from_user` | `local_20` | 80 |
| `tx_isp_unlocked_ioctl` | 56986 | `copy_to_user` | `local_20` | 80 |
| `tx_isp_unlocked_ioctl` | 57127 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 57157 | `copy_from_user` | `local_20` | 68 |
| `tx_isp_unlocked_ioctl` | 57208 | `copy_from_user` | `local_20` | ? |
| `tx_isp_unlocked_ioctl` | 57235 | `copy_from_user` | `local_20` | ? |
| `tx_isp_unlocked_ioctl` | 57273 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 57302 | `copy_to_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 57335 | `copy_from_user` | `local_20` | 64 |
| `tx_isp_unlocked_ioctl` | 57390 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 57574 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 57797 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 58146 | `copy_from_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 58254 | `copy_to_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 58285 | `copy_from_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 58497 | `copy_from_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 58578 | `copy_to_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 58904 | `copy_from_user` | `local_20` | 24 |
| `tx_isp_unlocked_ioctl` | 58922 | `memcpy` | `local_20` | 24 |
| `tx_isp_unlocked_ioctl` | 58996 | `copy_from_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 59273 | `copy_from_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 59355 | `copy_to_user` | `local_20` | 12 |
| `tx_isp_unlocked_ioctl` | 59435 | `copy_from_user` | `local_20` | 56 |
| `tx_isp_unlocked_ioctl` | 59474 | `copy_from_user` | `local_20` | 8 |
| `tx_isp_unlocked_ioctl` | 59543 | `copy_from_user` | `local_20` | 24 |
| `tx_isp_unlocked_ioctl` | 59574 | `copy_from_user` | `local_20` | 28 |
| `tx_isp_unlocked_ioctl` | 59625 | `copy_to_user` | `local_20` | 28 |
| `tx_isp_unlocked_ioctl` | 59661 | `copy_from_user` | `local_20` | 68 |
| `tx_isp_unlocked_ioctl` | 59689 | `memcpy` | `local_20` | 68 |
| `tx_isp_unlocked_ioctl` | 59718 | `copy_to_user` | `local_20` | 68 |
| `tx_isp_unlocked_ioctl` | 59744 | `copy_from_user` | `local_20` | 40 |
| `tx_isp_unlocked_ioctl` | 59775 | `copy_from_user` | `local_20` | 40 |
| `tx_isp_unlocked_ioctl` | 59802 | `copy_to_user` | `local_20` | 40 |
| `tx_isp_unlocked_ioctl` | 59833 | `copy_from_user` | `local_20` | 24 |
| `tx_isp_unlocked_ioctl` | 60106 | `copy_from_user` | `local_20` | 8 |
| `tisp_defog_dn_params_refresh` | 141927 | `memcpy` | `local_68` | 48 |
| `tisp_defog_dn_params_refresh` | 141932 | `memcpy` | `local_28` | 62 |
| `tisp_defog_dn_params_refresh` | 141939 | `memcpy` | `local_118` | 32 |
| `tisp_defog_dn_params_refresh` | 141942 | `memcpy` | `local_f8` | 32 |
| `tisp_defog_dn_params_refresh` | 141946 | `memcpy` | `local_d8` | 32 |
| `tisp_defog_dn_params_refresh` | 141950 | `memcpy` | `local_b8` | 32 |
| `tisp_defog_dn_params_refresh` | 141953 | `memcpy` | `local_98` | 32 |
| `tisp_defog_dn_params_refresh` | 142060 | `memcpy` | `local_68` | 48 |
| `tisp_defog_dn_params_refresh` | 142064 | `memcpy` | `local_28` | 62 |
| `tisp_defog_dn_params_refresh` | 142068 | `memcpy` | `local_118` | 32 |
| `tisp_defog_dn_params_refresh` | 142071 | `memcpy` | `local_f8` | 32 |
| `tisp_defog_dn_params_refresh` | 142074 | `memcpy` | `local_d8` | 32 |
| `tisp_defog_dn_params_refresh` | 142077 | `memcpy` | `local_b8` | 32 |
| `tisp_defog_dn_params_refresh` | 142080 | `memcpy` | `local_98` | 32 |
| `frame_channel_unlocked_ioctl` | 52294 | `copy_from_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 52421 | `copy_from_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 52558 | `__fill_tisp_buffer` | `local_20` | ? |
| `frame_channel_unlocked_ioctl` | 52561 | `copy_to_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 52823 | `__fill_tisp_buffer` | `local_20` | ? |
| `frame_channel_unlocked_ioctl` | 52829 | `copy_to_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 52861 | `copy_from_user` | `local_20` | 68 |
| `frame_channel_unlocked_ioctl` | 52958 | `__fill_tisp_buffer` | `local_20` | ? |
| `frame_channel_unlocked_ioctl` | 52962 | `copy_to_user` | `local_20` | 68 |
| `isp_core_tunning_unlocked_ioctl` | 46003 | `copy_from_user` | `local_10` | 16 |
| `isp_core_tunning_unlocked_ioctl` | 46035 | `copy_from_user` | `local_10` | 8 |
| `isp_core_tunning_unlocked_ioctl` | 46061 | `copy_from_user` | `local_10` | ? |
| `isp_core_tunning_unlocked_ioctl` | 46075 | `copy_to_user` | `local_10` | 8 |
| `isp_core_tunning_unlocked_ioctl` | 46101 | `copy_to_user` | `local_10` | 16 |
| `tisp_s_ccm_attr` | 181424 | `memcpy` | `local_38` | 36 |
| `tisp_s_ccm_attr` | 181464 | `memcpy` | `local_10` | 40 |
| `tisp_s_ccm_attr` | 181467 | `memcpy` | `local_14` | 36 |
| `tisp_s_ccm_attr` | 181467 | `memcpy` | `local_38` | 36 |
| `ispcore_core_ops_init` | 193817 | `memset` | `local_28` | 152 |
| `ispcore_core_ops_init` | 193995 | `memcpy` | `local_a8` | 24 |
| `tisp_blc_dn_params_refresh` | 111911 | `memcpy` | `local_10` | 12 |
| `tisp_blc_dn_params_refresh` | 111914 | `memcpy` | `local_10` | 12 |
| `tisp_wdr_params_used_refresh` | 120908 | `memcpy` | `local_10` | 26 |
| `tisp_wdr_params_used_refresh` | 120914 | `memset` | `local_10` | 26 |
| `tx_isp_driver_version_isra_15` | 56164 | `memcpy` | `local_18` | 96 |
| `tx_isp_driver_version_isra_15` | 56167 | `copy_to_user` | `local_18` | 96 |
| `csi_sensor_ops_sync_sensor_attr` | 41301 | `memcpy` | `local_10` | 96 |
| `defog_count_weight35abc` | 136826 | `memcpy` | `local_90` | 126 |
| `func_map_y_filter_sp` | 149395 | `memcpy` | `local_98` | 66 |
| `ispcore_sync_sensor_attr` | 187662 | `memcpy` | `local_b0` | ? |
| `subdev_sensor_ops_ioctl` | 40240 | `memset` | `local_28` | 44 |
| `tisp_bcsh_Tccm_RGB2YUV` | 170545 | `memcpy` | `local_18` | 36 |
| `tisp_csc_get_attr` | 180164 | `memset` | `local_10` | ? |
| `tisp_data_info_get` | 64494 | `memcpy` | `local_10` | 5400 |
| `tisp_defog_update_trans_k` | 137794 | `memcpy` | `local_20` | 5 |
| `tisp_gib_interp_by_gain` | 106829 | `memcpy` | `local_10` | 40 |
| `tisp_s_defog_ratio` | 141755 | `memcpy` | `local_20` | 5 |
| `tisp_set_awb_info` | 64158 | `memcpy` | `local_10` | 28 |
| `tiziano_adr_algorithm` | 152924 | `memcpy` | `local_18` | 14 |
| `tiziano_isp_ccm_attr_g_ctrl_isra_31` | 43639 | `copy_to_user` | `local_18` | 40 |
| `tx_isp_ae_expoinfo_s_attr_isra_17` | 43284 | `copy_from_user` | `local_18` | ? |
| `tx_isp_ae_g_explist_isra_56` | 44601 | `copy_to_user` | `local_18` | 324 |
| `tx_isp_ae_s_explist_isra_28` | 43581 | `copy_from_user` | `local_18` | ? |
| `tx_isp_ae_weight_g_attr_isra_48` | 44436 | `copy_to_user` | `local_18` | ? |
| `tx_isp_ae_weight_s_attr_isra_18` | 43341 | `copy_from_user` | `local_18` | ? |
| `tx_isp_af_weight_g_attr_isra_39` | 44106 | `copy_to_user` | `local_18` | ? |
| `tx_isp_af_weight_s_attr_isra_13` | 43113 | `copy_from_user` | `local_18` | ? |
| `tx_isp_autozoom_g_attr_isra_34` | 43781 | `copy_to_user` | `local_18` | ? |
| `tx_isp_awb_attr_g_attr_isra_41` | 44238 | `copy_to_user` | `local_18` | ? |
| `tx_isp_awb_attr_s_attr_isra_15` | 43227 | `copy_from_user` | `local_18` | ? |
| `tx_isp_awb_weight_g_attr_isra_40` | 44172 | `copy_to_user` | `local_18` | ? |
| `tx_isp_awb_weight_s_attr_isra_14` | 43170 | `copy_from_user` | `local_18` | ? |
| `tx_isp_gamma_s_attr_isra_6` | 42773 | `copy_from_user` | `local_18` | ? |
| `tx_isp_mask_g_attr_isra_35` | 43823 | `copy_to_user` | `local_18` | 288 |
| `tx_isp_mask_s_attr_isra_9` | 42905 | `copy_from_user` | `local_18` | 288 |
| `tx_isp_module_ratio_g_attr_isra_50` | 44544 | `copy_to_user` | `local_18` | 128 |
| `tx_isp_module_ratio_s_attr_isra_7` | 42839 | `copy_from_user` | `local_18` | ? |
| `tx_isp_sensor_release_sensor` | 55408 | `copy_from_user` | `local_18` | ? |
| `tx_isp_statis_config_g_attr_isra_49` | 44502 | `copy_to_user` | `local_18` | ? |
| `tx_isp_statis_config_s_attr_isra_19` | 43398 | `copy_from_user` | `local_18` | ? |
| `tx_isp_switch_bin_isra_22` | 43459 | `copy_from_user` | `local_18` | 68 |
| `tx_isp_video_link_stream` | 55587 | `copy_from_user` | `local_18` | ? |

## Dormant functions

| function | line (434bfcf0) | call | slot | bytes |
|---|---|---|---|---|
| `Tiziano_wdr_fusion_fpga` | 113606 | `memcpy` | `local_18` | 320 |
| `Tiziano_wdr_fusion_fpga` | 113619 | `memcpy` | `local_158` | 194 |
| `Tiziano_wdr_fusion_fpga` | 113622 | `memcpy` | `local_21c` | 66 |
| `Tiziano_wdr_fusion_fpga` | 113654 | `memcpy` | `local_21c` | 66 |
| `Tiziano_wdr_fusion_fpga` | 115367 | `memcpy` | `local_158` | 194 |
| `Tiziano_wdr_fusion_fpga` | 115561 | `memcpy` | `local_21c` | 66 |
| `Tiziano_wdr_fusion_fpga` | 117361 | `memcpy` | `local_21c` | ? |
| `tiziano_adr_5x5_init` | 145370 | `memcpy` | `local_98` | 32 |
| `tiziano_adr_5x5_init` | 145373 | `memcpy` | `local_78` | 32 |
| `tiziano_adr_5x5_init` | 145376 | `memcpy` | `local_58` | 32 |
| `tiziano_adr_5x5_init` | 145379 | `memcpy` | `local_38` | 32 |
| `tiziano_adr_5x5_init` | 145382 | `memcpy` | `local_18` | 32 |
| `Tisp_lce_soft` | 131828 | `memset` | `local_68` | 72 |
| `Tisp_lce_soft` | 132177 | `memcpy` | `local_f0` | 64 |
| `Tisp_lce_soft` | 132197 | `memcpy` | `local_f0` | 64 |
| `frame_channel_vidioc_get_fmt` | 50779 | `copy_from_user` | `local_18` | ? |
| `frame_channel_vidioc_get_fmt` | 50842 | `copy_to_user` | `local_18` | 112 |
| `frame_channel_vidioc_get_fmt` | 50860 | `memcpy` | `local_18` | 112 |
| `tisp_ae_fliker_detect` | 80776 | `memcpy` | `local_2e0` | 40 |
| `tisp_ae_fliker_detect` | 80785 | `memcpy` | `local_308` | 12 |
| `tisp_ae_fliker_detect` | 80809 | `memcpy` | `local_308` | 12 |
| `frame_channel_reqbufs` | 50925 | `copy_from_user` | `local_98` | ? |
| `frame_channel_reqbufs` | 51189 | `copy_to_user` | `local_98` | 20 |
| `lce_std_hist_transform` | 131476 | `memcpy` | `local_10` | 128 |
| `lce_std_hist_transform` | 131535 | `memcpy` | `local_10` | 128 |
| `tiziano_adr_base_pars` | 145892 | `memcpy` | `local_60` | ? |
| `tiziano_adr_base_pars` | 145895 | `memcpy` | `local_50` | 14 |
| `tiziano_save_to_local` | 73276 | `memcpy` | `local_10` | 8 |
| `tiziano_save_to_local` | 73290 | `memcpy` | `local_10` | ? |
| `video_input_cmd_set` | 39286 | `memcpy` | `local_18` | 32 |
| `video_input_cmd_set` | 39423 | `memcpy` | `local_18` | 32 |
| `Tiziano_wdr_deghost_fpga` | 118053 | `memcpy` | `local_18` | 324 |
| `isp_frame_done_wakeup` | 46132 | `memcpy` | `local_10` | 8 |
| `ispcore_pad_event_handle` | 192245 | `memset` | `local_20` | 64 |
| `lce_self_light_correct` | 130489 | `memcpy` | `local_194` | 20 |
| `tx_isp_ae_expoinfo_g_attr_isra_45` | 44370 | `copy_to_user` | `local_18` | ? |
| `tx_isp_ae_scence_g_attr_isra_44` | 44304 | `copy_to_user` | `local_18` | ? |
| `tx_isp_gamma_g_attr_isra_32` | 43715 | `copy_to_user` | `local_18` | ? |
| `vic_frame_done_wakeup` | 46206 | `memcpy` | `local_10` | 8 |

