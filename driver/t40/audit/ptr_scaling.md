# T40 pointer scaling (translation class 2)

Register variables of the recovered code (`v0`, `s1`, `a2`, `t3`, `local_*`) are declared as `uint32_t *` / `uintptr_t *`; the decompiler emits byte offsets (as in the assembly, e.g. stock `addiu v1,v1,1`), so `p + n` on them scales `n` by the element size. Stage 1 (branch `claude/t40-ptr-scaling`) casts the operand to `uintptr_t` in every function reachable from the module (call graph of a `-ffunction-sections` analysis build: init/exit, exports, address-taken functions; 1364 of 1824 functions). The build is `-O0`, so the second pass finds no further reachable functions.

| set | sites | functions | state |
|---|---|---|---|
| reachable, register variables | 2588 | 235 | cast (stage 1) |
| reachable, named (HLIL-style) variables | 15 | 9 | listed below, needs a check against the stock code |
| dormant, register variables | 667 | 86 | not changed (listed below) |
| dormant, named variables | 21 | 7 | not changed (listed below) |

Named variables mix element steps (`p = p + 1` over an `int *` may be meant as one element) with byte offsets and counters declared as pointers (`tx_isp_video_s_stream`: `int32_t *i`, `for (i = 0; i != 16;) ... i = i + 1` runs 4 times instead of 16), so they are not cast mechanically.

## Reachable, named variables (check each)

| line (at 434bfcf0) | function | variable | statement |
|---|---|---|---|
| 42416 | `tx_isp_csi_slake_subdev` | `s1_1` | `s1_1 = s1_1 + 1;` |
| 42432 | `tx_isp_csi_slake_subdev` | `s0_4` | `s0_4 = s0_4 - 1;` |
| 55551 | `tx_isp_video_s_stream` | `i` | `i = i + 1;` |
| 94561 | `tisp_ae_sec_long_process_api` | `v1_4` | `v1_5 = v1_4 + data_90844;` |
| 135173 | `tisp_lce_draw_lines` | `v0_4` | `v0_5 = v0_4 + s6_2 + 8;` |
| 135182 | `tisp_lce_draw_lines` | `v0_4` | `v0_5 = v0_4 + a0_6 + 7;` |
| 154430 | `tisp_mdns_reg_cfg_equation_smp` | `a0_1` | `a0_2 = a0_1 + lo;` |
| 154433 | `tisp_mdns_reg_cfg_equation_smp` | `a0_2` | `a0_3 = a0_2 + lo;` |
| 154439 | `tisp_mdns_reg_cfg_equation_smp` | `a0_4` | `data_a89ec = a0_4 + lo;` |
| 169766 | `tisp_bcsh_ct_interpolation` | `v1_7` | `t1_19 = v1_7 - t3_2;` |
| 169776 | `tisp_bcsh_ct_interpolation` | `v1_7` | `t1_17 = v1_7 - t3_2;` |
| 174025 | `Tzn_Msca_Shd_mask` | `s4_1` | `i = s4_1 + 0x16168;` |
| 174029 | `Tzn_Msca_Shd_mask` | `i` | `s2_1 = i - 0x30;` |
| 174111 | `Tzn_Msca_Shd_osd` | `v0_3` | `s4_1 = v0_3 + var_40_1;` |
| 182633 | `tisp_g_mscaler_mask_attr` | `i` | `limit = i + 924;` |

## Dormant functions (not reachable from the module in this build)

| function | register sites | named sites |
|---|---|---|
| `Tiziano_wdr_deghost_fpga` | 99 | 0 |
| `Tiziano_wdr_fusion_fpga` | 95 | 0 |
| `tiziano_load_custom` | 43 | 0 |
| `tisp_wdr_softout_reg_refresh` | 41 | 0 |
| `Tisp_lce_soft` | 30 | 0 |
| `vic_mdma_enable` | 30 | 0 |
| `tisp_lce_get_data` | 21 | 0 |
| `lce_self_light_correct` | 20 | 0 |
| `lce_hist_method` | 16 | 0 |
| `isp_save_cmd_set` | 12 | 0 |
| `tisp_s_defog_str_internal` | 12 | 0 |
| `lce_hist_filter_and_judge` | 11 | 0 |
| `tisp_defog_max_filter` | 10 | 0 |
| `tisp_clm_wdr_en` | 9 | 0 |
| `tisp_gib_interp_by_ir` | 9 | 0 |
| `tx_isp_create_graph_and_nodes` | 9 | 0 |
| `lce_light_lock_adjust_hist` | 8 | 0 |
| `tisp_defog_img_filter25` | 8 | 0 |
| `tiziano_adr_sec_interrupt_static` | 8 | 0 |
| `ispcore_pad_event_handle` | 7 | 0 |
| `tisp_defog_img_filter9` | 7 | 0 |
| `tisp_wdr_get_lum_s` | 7 | 0 |
| `tisp_ae_wdr_en` | 6 | 0 |
| `tisp_gsm_dual_get_hist` | 6 | 0 |
| `tisp_wdr_process_init` | 6 | 0 |
| `tisp_ae_g_scene_luma` | 5 | 0 |
| `tisp_ae_sort_H2L` | 5 | 0 |
| `tisp_gsm_single_get_hist` | 5 | 0 |
| `tisp_wdr_main_init` | 5 | 0 |
| `tisp_wdr_quicksort` | 5 | 0 |
| `tiziano_adr_5x5_init` | 5 | 0 |
| `tiziano_adr_5x5_out` | 5 | 0 |
| `isp_info_show_isra_4` | 4 | 0 |
| `tisp_g_ae_scence_attr` | 4 | 0 |
| `tx_isp_reg_set` | 4 | 0 |
| `func_gam_x2y` | 3 | 0 |
| `func_matrix_check_short` | 3 | 0 |
| `func_subsection_light` | 3 | 0 |
| `tisp_adr_gamma_array_get` | 3 | 0 |
| `tisp_awb_sec_face_pos` | 3 | 0 |
| `tisp_bcsh_wdr_en` | 3 | 0 |
| `tisp_gamma_wdr_en` | 3 | 0 |
| `tisp_lce_main_interrupt_static` | 3 | 0 |
| `tisp_wdr_seco_init` | 3 | 0 |
| `tiziano_save_to_local` | 3 | 0 |
| `tx_isp_remove` | 3 | 0 |
| `vic_mdma_sec_enable` | 3 | 0 |
| `frame_channel_reqbufs` | 2 | 0 |
| `lce_head_tail_search` | 2 | 0 |
| `lce_std_hist_transform` | 2 | 0 |
| `printf_func_long` | 2 | 0 |
| `printf_func_short` | 2 | 0 |
| `private_reset_tx_isp_module` | 2 | 0 |
| `tisp_ae_sec_get_antiflicker_step` | 2 | 0 |
| `tisp_af_get_attr` | 2 | 0 |
| `tisp_bcsh_s_hue` | 2 | 0 |
| `tisp_ccm_wdr_en` | 2 | 0 |
| `tisp_gib_print_tool_para` | 2 | 2 |
| `tisp_lce_seco_process` | 2 | 0 |
| `tisp_wdr_interplate` | 2 | 0 |
| `tisp_wdr_main_process` | 2 | 0 |
| `tisp_wdr_seco_process` | 2 | 0 |
| `frame_channel_vidioc_get_fmt` | 1 | 0 |
| `isp_frame_done_wakeup` | 1 | 0 |
| `isp_mem_init` | 1 | 0 |
| `sensor_alloc_integration_time` | 1 | 0 |
| `sensor_alloc_integration_time_short` | 1 | 0 |
| `sensor_set_integration_time` | 1 | 0 |
| `table_intp` | 1 | 0 |
| `tisp_adr_sec_process` | 1 | 0 |
| `tisp_ae_fliker_detect` | 1 | 0 |
| `tisp_ae_main_s_min` | 1 | 0 |
| `tisp_ae_sec_s_min` | 1 | 0 |
| `tisp_af_main_interrupt_static` | 1 | 0 |
| `tisp_channel_main_attr_set` | 1 | 0 |
| `tisp_channel_sec_attr_set` | 1 | 0 |
| `tisp_defog_wdr_en` | 1 | 0 |
| `tisp_g_defog_str_internal` | 1 | 0 |
| `tisp_gsm_main_long_process_api` | 1 | 0 |
| `tisp_hldc_quadratic_func` | 1 | 0 |
| `tisp_lce_main_process` | 1 | 0 |
| `tisp_lsc_lut_mirror_exchange` | 1 | 0 |
| `tisp_lsc_upside_down_lut` | 1 | 0 |
| `tisp_wdr_main_interrupt_static` | 1 | 0 |
| `vic_core_ops_ioctl_1` | 1 | 0 |
| `vic_frame_done_wakeup` | 1 | 0 |
| `tisp_blc_print_tool_para` | 0 | 8 |
| `tisp_gib_print_real_para` | 0 | 4 |
| `fix_point_intp` | 0 | 2 |
| `tisp_wdr_getVar` | 0 | 2 |
| `tisp_wdr_paixu` | 0 | 2 |
| `video_input_cmd_show` | 0 | 1 |

## Reachable functions changed in stage 1 (sites)

`tisp_dmsc_wdr_en` 233, `tisp_wdr_params_used_refresh` 221, `tisp_wdr_param_array_set_intel` 177, `tisp_wdr_param_array_get` 139, `tisp_sdns_wdr_en` 126, `tisp_af_params_refresh` 100, `tisp_mdns_wdr_en` 85, `Tiziano_adr_fpga` 68, `tx_isp_unlocked_ioctl` 55, `tisp_mdns_reg_cfg` 53, `isp_vic_interrupt_service_routine` 52, `tisp_af_set_hardware_param` 49, `tisp_lce_get_show_data` 38, `tisp_lce_tool_to_awdr` 37, `tisp_awb_long_alogrithm` 35, `tisp_s_sdns_ratio` 34, `Tzn_Msca_cfg_load` 31, `tisp_cdns_wdr_en` 31, `tisp_defog_wdr_choose_para` 26, `frame_channel_unlocked_ioctl` 23, `tisp_awb_spec_calculate` 23, `tisp_dmsc_noref_reg_cfg` 23, `tisp_sdns_ref_reg_cfg` 22, `tisp_tstp_reg_cfg` 22, `tisp_tstp_mark1_func` 20, `tisp_gib_write_reg` 19, `tisp_defog_init` 18, `tisp_lce_curve_init_default` 17, `tisp_sdns_noref_reg_cfg` 17, `func_adr_reg_write_sometimes` 16, `tisp_lce_awdr_to_used` 16, `tisp_s_drc_ratio` 16, `tisp_set_scaler_level_control` 16, `defog_count_weight35abc` 15, `tisp_defog_dn_params_refresh` 15, `tisp_dmsc_ref_reg_cfg` 15, `tisp_dmsc_sharpness_set` 15, `tisp_s_dpc_ratio` 14, `tx_isp_subdev_pipo` 14, `func_adr_reg_write_5x5` 13, `ispcore_slake_module` 13, `tisp_bcsh_ev_update` 13, `tisp_clm_real_write_lut` 13, `tisp_ae_short_weight_mean2` 12, `tisp_defog_update_reg_para2` 12, `func_adr_reg_write_every` 11, `tisp_bcsh_Tccm_RGBYUV` 11, `csi_phy_start` 10, `ispcore_irq_main_fd_work` 10, `ispcore_irq_sec_fd_work` 10, `tisp_hldc_con_par_cfg` 10, `tisp_s_defog_ratio` 10, `tisp_wdr_direct_reg_refresh` 10, `ispcore_activate_module` 9, `ispcore_irq_main_fs_work` 9, `ispcore_irq_sec_fs_work` 9, `tisp_ae_long_target` 9, `tiziano_adr_algorithm` 9, `tx_isp_vin_slake_subdev` 9, `tisp_csc_set_attr` 8, `tisp_gamma_real_write_lut` 8, `isp_core_tunning_open` 7, `tisp_clm_init` 7, `tisp_defog_write_reg_para3` 7, `tisp_set_csc_version` 7, `Tzn_Msca_init_cfg` 6, `cm_control` 6, `func_map_y_filter_sp` 6, `tisp_blc_ag_interp` 6, `tisp_gib_interp_by_gain` 6, `tx_isp_video_link_destroy_isra_5` 6, `vic_mdma_irq_function1` 6, `vic_mdma_irq_function2` 6, `tisp_ae_dn_params_refresh` 5, `tisp_ae_sec_process` 5, `tisp_af_set_weight` 5, `tisp_bcsh_ev_update_TransitParam` 5, `tisp_clm_ct_interp` 5, `tisp_clm_ct_update` 5, `tisp_defog_main_interrupt_static` 5, `tisp_defog_seco_interrupt_static` 5, `tisp_lce_remove` 5, `tisp_lsc_ct_update` 5, `tisp_lsc_write_reg` 5, `tisp_s_mdns_ratio` 5, `tisp_yuv_input_set_csc_version` 5, `tiziano_adr_ev_func` 5, `subdev_sensor_ops_ioctl` 4, `tisp_ae_algo_handle` 4, `tisp_ae_params_refresh` 4, `tisp_ae_sepc_area` 4, `tisp_awb_get_statistics` 4, `tisp_bcsh_set_attr` 4, `tisp_ccm_set_attr` 4, `tisp_clm_param_array_set` 4, `tisp_day_or_night_s_ctrl` 4, `tisp_gamma_ev_update` 4, `tisp_gamma_strength_transform` 4, `tisp_lce_clr_ram0` 4, `tisp_lce_write_all_reg` 4, `tisp_lsc_real_write_lut` 4, `tisp_s_mscaler_mask_attr` 4, `tisp_s_osd_attr` 4, `tx_isp_sensor_release_sensor` 4, `isp_mbus_to_bayer_isra_0` 3, `ispcore_video_s_stream` 3, `tisp_adr_dn_params_refresh` 3, `tisp_ae_get_weight` 3, `tisp_ae_main_process` 3, `tisp_af_alogrithm` 3, `tisp_af_dn_params_refresh` 3, `tisp_bcsh_Toffset_RGB2YUV` 3, `tisp_bcsh_Toffset_RGBYUV` 3, `tisp_bcsh_dn_params_refresh` 3, `tisp_ccm_dn_params_refresh` 3, `tisp_ccm_ev_ct_update` 3, `tisp_clm_param_array_get` 3, `tisp_defog_update_trans_k` 3, `tisp_g_draw_attr` 3, `tisp_hardware_reg_refresh` 3, `tisp_s_autozoom_control` 3, `tisp_s_module_ratio_attr` 3, `tisp_wdr_log2` 3, `tx_isp_video_link_stream` 3, `Tiziano_Awb_Ct_Cal` 2, `cdns_remove` 2, `csi_sensor_ops_sync_sensor_attr` 2, `dpc_remove` 2, `fs_slake_module` 2, `isp_core_tunning_unlocked_ioctl` 2, `tisp_ae_s_comp` 2, `tisp_af_get_metric` 2, `tisp_af_set_attr_refresh` 2, `tisp_blc_calc_digital_gain` 2, `tisp_cdns_par_refresh` 2, `tisp_defog_get_data_part_0` 2, `tisp_defog_write_reg_para2` 2, `tisp_g_ae_statis_attr` 2, `tisp_gamma_dn_params_refresh` 2, `tisp_gamma_init` 2, `tisp_gamma_set_attr` 2, `tisp_lce_clr_ram1` 2, `tisp_lce_param_array_set` 2, `tisp_lce_tgain_update` 2, `tisp_push_frame_count` 2, `tisp_reg_map_set` 2, `tisp_s_ae_scence_attr` 2, `tisp_s_awb_attr` 2, `tisp_set_frame_drop` 2, `tisp_slake_all` 2, `tx_isp_subdev_deinit` 2, `tx_isp_vin_init` 2, `update_isp_osd` 2, `ISPAWBInterpolation1` 1, `ISPAWBInterpolation2` 1, `frame_channel_release` 1, `func_adr_map_curve1` 1, `func_adr_reg_write_one` 1, `func_local_info` 1, `func_zone_ct_weight` 1, `gamma_remove` 1, `isp_main_pre_frame_dequeue` 1, `ispcore_core_ops_init` 1, `ispcore_core_ops_ioctl` 1, `ispcore_frame_channel_streamoff` 1, `ispcore_irqstatusSec_func` 1, `ispcore_sensor_ops_ioctl` 1, `ispint_adr_16` 1, `ispvic_frame_get_raw` 1, `tisp_adr_main_deinit` 1, `tisp_adr_main_process` 1, `tisp_ae_api_set_exp_list` 1, `tisp_ae_get_hist` 1, `tisp_ae_long_alogrithm` 1, `tisp_ae_long_tune` 1, `tisp_ae_main_interrupt_hist` 1, `tisp_ae_main_long_ev_alloc` 1, `tisp_ae_main_short_ev_alloc` 1, `tisp_ae_sec_interrupt_hist` 1, `tisp_ae_sec_interrupt_static` 1, `tisp_ae_sec_long_ev_alloc` 1, `tisp_ae_sec_short_ev_alloc` 1, `tisp_ae_set_exprinfo_attr` 1, `tisp_ae_short_ctrls_update` 1, `tisp_ae_short_expt` 1, `tisp_af_param_array_set` 1, `tisp_af_sec_interrupt_static` 1, `tisp_af_set_attr` 1, `tisp_awb_dn_params_refresh` 1, `tisp_awb_get_weight` 1, `tisp_awb_long_par_update` 1, `tisp_awb_main_process` 1, `tisp_awb_param_array_set` 1, `tisp_awb_sec_interrupt_static` 1, `tisp_awb_set_gain` 1, `tisp_awb_tgain_update` 1, `tisp_bcsh_csc_attr` 1, `tisp_bcsh_param_array_set` 1, `tisp_bcsh_set_mjpeg_contrast` 1, `tisp_bcsh_set_offset_rgb` 1, `tisp_blc_write_reg` 1, `tisp_ccm_main_ct_update` 1, `tisp_ccm_main_ev_update` 1, `tisp_ccm_param_array_set` 1, `tisp_ccm_sec_ct_update` 1, `tisp_ccm_sec_ev_update` 1, `tisp_cdns_refresh` 1, `tisp_cdns_reg_cfg` 1, `tisp_csc_get_attr` 1, `tisp_defog_write_reg_para1` 1, `tisp_dmsc_par_refresh` 1, `tisp_dmsc_refresh` 1, `tisp_dpc_par_refresh` 1, `tisp_dpc_refresh` 1, `tisp_g_ae_exprinfo_attr` 1, `tisp_g_osd_single_attr` 1, `tisp_lce_top_change_state` 1, `tisp_lsc_get_wlut_times` 1, `tisp_lsc_init` 1, `tisp_lsc_interp_by_gain` 1, `tisp_mdns_par_refresh` 1, `tisp_mdns_reg_cfg_equation_dif` 1, `tisp_mdns_reg_cfg_equation_smp` 1, `tisp_s_antiflick` 1, `tisp_s_osd_single_attr` 1, `tisp_s_statis_config_attr` 1, `tisp_sdns_par_refresh` 1, `tisp_sdns_refresh` 1, `tisp_sec_init` 1, `tisp_switch_bin` 1, `tisp_wdr_default_reg_refresh` 1, `tisp_wdr_sec_short_ev_update` 1, `tisp_wdr_sec_short_tgain_update` 1, `tx_isp_vin_reset` 1, `vic_core_ops_ioctl` 1
