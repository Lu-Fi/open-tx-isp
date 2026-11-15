/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 ADR per-frame setup: vendor tiziano_adr_algorithm (tx-isp-t31.ko H20221206a 0x476cc) as plain C, shared by
 * driver/t31/tx_isp_tuning.c and the binary oracle tests/t31_binoracle/adralgo.c.  EV bracket in adr_ev_list_now,
 * the eight EV-interpolated control words, the CTC map-to-cut curve, the working kneepoint X copies, the call of
 * Tiziano_adr_fpga (T31_ADR_FPGA) and the temporal smoothing of map_kneepoint_y toward map_kneepoint_y_pre.
 * The control words are the vendor ones: param_adr_tool_control_array[3] CTC enable, [11] / [12] / [13] smoothing
 * mode / enable / rate; the edge values come from param_adr_min_kneepoint_array[1] / [14] / [9] / [22] (openimp #563).
 * The includer provides the globals with the vendor names and T31_ADR_FPGA / T31_ADR_LOG_CTRL / T31_ADR_LOG_FINAL.
 */
#ifndef TX_ISP_T31_ADRALGO_H
#define TX_ISP_T31_ADRALGO_H

static inline uint32_t adr_ev_interpolate(uint32_t ev_val, uint32_t ev_lo, uint32_t ev_hi,
				   uint32_t val_lo, uint32_t val_hi)
{
	uint32_t ev_delta, ev_range;

	/* abs(ev_now - ev_lo) */
	if (ev_lo < ev_val)
		ev_delta = ev_val - ev_lo;
	else
		ev_delta = ev_lo - ev_val;

	/* abs(ev_hi - ev_lo) */
	if (ev_hi < ev_lo)
		ev_range = ev_lo - ev_hi;
	else
		ev_range = ev_hi - ev_lo;

	if (ev_range == 0)
		return val_lo;

	if (val_hi >= val_lo)
		return ev_delta * (val_hi - val_lo) / ev_range + val_lo;
	else
		return val_lo - ev_delta * (val_lo - val_hi) / ev_range;
}

static inline int t31_adr_algorithm(void)
{
	uint32_t ev_val;
	uint32_t ev_lo, ev_hi;
	int idx;
	int i, j, blk;
	uint16_t *gam_y_16;
	uint32_t val_lo, val_hi;
	uint32_t rate, max_diff, smooth_updated;

	/* vendor 0x476fc: an unchanged EV skips only the EV interpolation (phases 1-3); the X copies,
	 * Tiziano_adr_fpga (new map from this frame's statistics) and the smoothing run every frame (openimp #563) */
	ev_val = ev_now;
	idx = -1;
	if (ev_changed == 1) {
		ev_changed = 0;

		/* === Phase 1: Find EV bracket index === */
		/* Search adr_ev_list_now[0..8] for first entry >= ev_val */
		idx = 0;
		while (idx < 9) {
			if (adr_ev_list_now[idx] >= ev_val)
				break;
			idx++;
		}

		/* === Phase 2: Interpolate 8 parameter arrays === */
		if (idx == 0) {
			/* Below first EV level: use first-entry defaults */
			adr_map_mode_now[0] = param_adr_min_kneepoint_array[1];
			adr_map_mode_now[2] = param_adr_min_kneepoint_array[14];
			adr_map_mode_now[5] = adr_blp2_list_now[0];
			adr_light_end_now[28] = adr_ligb_list_now[0];
			adr_block_light_now[10] = adr_mapb1_list_now[0];
			adr_block_light_now[11] = adr_mapb2_list_now[0];
			adr_block_light_now[12] = adr_mapb3_list_now[0];
			adr_block_light_now[13] = adr_mapb4_list_now[0];
		} else if (idx == 9) {
			/* Past last EV level: use last-entry defaults */
			adr_map_mode_now[0] = param_adr_min_kneepoint_array[9];
			adr_map_mode_now[2] = param_adr_min_kneepoint_array[22];
			adr_map_mode_now[5] = adr_blp2_list_now[8];
			adr_light_end_now[28] = adr_ligb_list_now[8];
			adr_block_light_now[10] = adr_mapb1_list_now[8];
			adr_block_light_now[11] = adr_mapb2_list_now[8];
			adr_block_light_now[12] = adr_mapb3_list_now[8];
			adr_block_light_now[13] = adr_mapb4_list_now[8];
		} else if (adr_ev_list_now[idx] == adr_ev_list_now[idx - 1]) {
			/* Equal EV values: no interpolation needed, use idx values directly */
			adr_map_mode_now[0] = param_adr_min_kneepoint_array[idx + 1];
			adr_map_mode_now[2] = param_adr_min_kneepoint_array[idx + 14];
			adr_map_mode_now[5] = adr_blp2_list_now[idx];
			adr_light_end_now[28] = adr_ligb_list_now[idx];
			adr_block_light_now[10] = adr_mapb1_list_now[idx];
			adr_block_light_now[11] = adr_mapb2_list_now[idx];
			adr_block_light_now[12] = adr_mapb3_list_now[idx];
			adr_block_light_now[13] = adr_mapb4_list_now[idx];
		} else {
			/* Interpolate between ev_list[idx-1] and ev_list[idx] */
			ev_lo = adr_ev_list_now[idx - 1];
			ev_hi = adr_ev_list_now[idx];

			/* 1) adr_map_mode_now[0] from param_adr_min_kneepoint_array[idx] / [idx+1] */
			adr_map_mode_now[0] = adr_ev_interpolate(ev_val, ev_lo, ev_hi,
				param_adr_min_kneepoint_array[idx],
				param_adr_min_kneepoint_array[idx + 1]);

			/* 2) adr_map_mode_now[2] from param_adr_min_kneepoint_array[idx+0xd] / [idx+0xe] */
			adr_map_mode_now[2] = adr_ev_interpolate(ev_val, ev_lo, ev_hi,
				param_adr_min_kneepoint_array[idx + 0xd],
				param_adr_min_kneepoint_array[idx + 0xe]);

			/* 3) adr_map_mode_now[5] from adr_blp2_list_now[idx-1] / [idx] */
			adr_map_mode_now[5] = adr_ev_interpolate(ev_val, ev_lo, ev_hi,
				adr_blp2_list_now[idx - 1], adr_blp2_list_now[idx]);

			/* 4) adr_light_end_now[28] from adr_ligb_list_now[idx-1] / [idx] */
			adr_light_end_now[28] = adr_ev_interpolate(ev_val, ev_lo, ev_hi,
				adr_ligb_list_now[idx - 1], adr_ligb_list_now[idx]);

			/* 5) adr_block_light_now[10] from adr_mapb1_list_now[idx-1] / [idx] */
			adr_block_light_now[10] = adr_ev_interpolate(ev_val, ev_lo, ev_hi,
				adr_mapb1_list_now[idx - 1], adr_mapb1_list_now[idx]);

			/* 6) adr_block_light_now[11] from adr_mapb2_list_now[idx-1] / [idx] */
			adr_block_light_now[11] = adr_ev_interpolate(ev_val, ev_lo, ev_hi,
				adr_mapb2_list_now[idx - 1], adr_mapb2_list_now[idx]);

			/* 7) adr_block_light_now[12] from adr_mapb3_list_now[idx-1] / [idx] */
			adr_block_light_now[12] = adr_ev_interpolate(ev_val, ev_lo, ev_hi,
				adr_mapb3_list_now[idx - 1], adr_mapb3_list_now[idx]);

			/* 8) adr_block_light_now[13] from adr_mapb4_list_now[idx-1] / [idx] */
			adr_block_light_now[13] = adr_ev_interpolate(ev_val, ev_lo, ev_hi,
				adr_mapb4_list_now[idx - 1], adr_mapb4_list_now[idx]);
		}

		/* === Phase 3: CTC interpolation (when param_adr_tool_control_array[3] == 1) === */
		/* OEM gam_y array has 9 CTC entries per EV level, but levels are not
		 * uniformly spaced. Level base offsets (in uint16_t indices):
		 * Level: 0   1   2   3   4   5   6   7   8
		 * Base:  0  10  20  32  42  52  64  74  84
		 * These are hardcoded in the OEM unrolled code. */
		if (param_adr_tool_control_array[3] == 1) {
			static const int ctc_level_base[9] = {
				0, 10, 20, 32, 42, 52, 64, 74, 84
			};
			gam_y_16 = (uint16_t *)param_adr_gam_y_array;

			if (idx == 0) {
				/* Below first EV: use level 0 entries directly */
				for (j = 0; j < 9; j++)
					adr_ctc_map2cut_y_now[j] = (uint32_t)gam_y_16[ctc_level_base[0] + j];
			} else if (idx == 9) {
				/* Past last EV: use level 8 entries directly */
				for (j = 0; j < 9; j++)
					adr_ctc_map2cut_y_now[j] = (uint32_t)gam_y_16[ctc_level_base[8] + j];
			} else {
				/* Interpolate CTC between level[idx-1] and level[idx] */
				ev_lo = adr_ev_list_now[idx - 1];
				ev_hi = adr_ev_list_now[idx];

				if (ev_lo == ev_hi) {
					for (j = 0; j < 9; j++)
						adr_ctc_map2cut_y_now[j] = (uint32_t)gam_y_16[ctc_level_base[idx] + j];
				} else {
					for (j = 0; j < 9; j++) {
						val_lo = (uint32_t)gam_y_16[ctc_level_base[idx - 1] + j];
						val_hi = (uint32_t)gam_y_16[ctc_level_base[idx] + j];
						adr_ctc_map2cut_y_now[j] = adr_ev_interpolate(
							ev_val, ev_lo, ev_hi, val_lo, val_hi);
					}
				}
			}
		}

	}

	/* === Phase 3.5: Copy param arrays to working arrays === */
	/* OEM copies param_adr_min_kneepoint_array_def → min_kneepoint_x,
	 *            param_adr_map_kneepoint_array → map_kneepoint_x,
	 *            param_adr_ctc_kneepoint_array → ctc_kneepoint_x */
	for (i = 0; i < 11; i++) {
		min_kneepoint_x[i] = param_adr_min_kneepoint_array_def[i];
		map_kneepoint_x[i] = param_adr_map_kneepoint_array[i];
		if (i < 9)
			ctc_kneepoint_x[i] = param_adr_ctc_kneepoint_array[i];
	}

	T31_ADR_LOG_CTRL(ev_val, idx);

	/* === Phase 4: FPGA struct pointers + Tiziano_adr_fpga (driver hook) === */
	T31_ADR_FPGA();

	/* === Phase 5: Temporal smoothing of map_kneepoint_y === */
	if (param_adr_tool_control_array[12] == 1) {
		rate = param_adr_tool_control_array[13];
		smooth_updated = 0;

		for (blk = 0; blk < 24; blk++) {
			/* Find max difference in this block between map_kneepoint_y and _pre */
			max_diff = 0;
			for (j = 0; j < 11; j++) {
				uint32_t cur = map_kneepoint_y[blk * 11 + j];
				uint32_t pre = map_kneepoint_y_pre[blk * 11 + j];
				uint32_t diff = (pre >= cur) ? (pre - cur) : (cur - pre);
				if ((int32_t)max_diff < (int32_t)diff)
					max_diff = diff;
			}

			/* Clamp rate and handle special cases */
			if (rate >= 0x201) {
				smooth_updated = 1;
				rate = 0x200;
			} else if (param_adr_tool_control_array[11] != 9 && rate == 0) {
				rate = 1;
				smooth_updated = 1;
			}

			if (rate < max_diff) {
				/* Blend: gradually move map_kneepoint_y toward _pre
				 * scale = (rate << 21) / max_diff
				 * vendor 0x4a924 / 0x4a944: signed div by 0x200000 */
				uint32_t scale = (rate << 21) / max_diff;
				for (j = 0; j < 11; j++) {
					uint32_t cur = map_kneepoint_y[blk * 11 + j];
					uint32_t pre = map_kneepoint_y_pre[blk * 11 + j];
					uint32_t clamp_max = 0xff5 + (uint32_t)j;
					uint32_t blended;

					if (pre >= cur) {
						blended = pre - (uint32_t)((int32_t)((pre - cur) * scale + 0x100000) / 0x200000);
					} else {
						blended = (uint32_t)((int32_t)((cur - pre) * scale + 0x100000) / 0x200000) + pre;
					}
					if (clamp_max < blended)
						blended = clamp_max;
					map_kneepoint_y[blk * 11 + j] = blended;
					map_kneepoint_y_pre[blk * 11 + j] = blended;
				}
			} else {
				/* Difference within rate: snap to current, clamp per-entry */
				for (j = 0; j < 11; j++) {
					uint32_t clamp_max = 0xff5 + (uint32_t)j;
					if (clamp_max < map_kneepoint_y[blk * 11 + j])
						map_kneepoint_y[blk * 11 + j] = clamp_max;
					map_kneepoint_y_pre[blk * 11 + j] = map_kneepoint_y[blk * 11 + j];
				}
			}
		}

		if (smooth_updated)
			param_adr_tool_control_array[13] = rate;
	}

	T31_ADR_LOG_FINAL();

	return 0;
}

#endif
