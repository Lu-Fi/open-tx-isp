/* Active T23 Tiziano AWB path reconstructed from tx-isp-t23-hlil.txt. */

#include "tx_isp_t23_tuning_ext.h"
#include "tx_isp_t23_awb_cluster.h"
#include "tx_isp_t23_awb_stock.h"

/* SetAwbCt: the colour temperature used in the manual WB mode (stock .bss
 * +31488), see t23x_awb_ct_select */
static uint32_t regtrace_t23_awb_custom_ct;

/*
 * Set/GetAwbClust and Set/GetAwbCtTrend objects (see t23x_awb_cluster_set):
 * the AWB run reads them under this mutex, the controls and the IQ bank
 * load write them under it.
 */
static DEFINE_MUTEX(t23x_awb_api_mutex);
/* the stock .bss of the cluster stage (never cleared, as in stock) */
static struct t23x_awb_cluster_state regtrace_t23_awb_cluster_state;
/* the cluster object of this run (picked like stock JZ_Isp_Awb) */
static uint32_t regtrace_t23_awb_cl_run[T23X_AWB_CLUSTER_WORDS];
/* stock .bss awb_gain_original: the history averaged rg / bg of the last
 * gain update (Tiziano_awb_fpga decides on it whether to run set_gain) */
static uint32_t regtrace_t23_awb_orig_rg;
static uint32_t regtrace_t23_awb_orig_bg;
/* stock awb_moa: set by SetWB (set_gain clears it) */

static uint32_t regtrace_t23_awb_absdiff(uint32_t a, uint32_t b)
{
    return a < b ? b - a : a - b;
}

/*
 * The cluster and trend objects from the active IQ bank, as stock
 * tiziano_awb_params_refresh (stream start, day/night switch): the IQ
 * status word and block become *_api_status[0] and *_api_para, the pending
 * user mark (status[1]) is cleared, and the live object follows the IQ block
 * unless its IQ status word is 2.  A SetAwbClust / SetAwbCtTrend before the
 * refresh is dropped, as in stock.  An unreadable bank leaves them as they
 * are.
 */
static void regtrace_t23_awb_api_load_bank(void)
{
    uint32_t cl_status, tr_status;
    uint32_t cl[T23X_AWB_CLUSTER_WORDS];
    uint32_t tr[T23X_AWB_TREND_WORDS];
    uint32_t *st;

    if (regtrace_t23_read_tuning_data(REGTRACE_T23_AWB_CLUSTER_STATUS_OFFSET,
                                      &cl_status, sizeof(cl_status)) ||
        regtrace_t23_read_tuning_data(REGTRACE_T23_AWB_CLUSTER_OFFSET, cl,
                                      sizeof(cl)) ||
        regtrace_t23_read_tuning_data(REGTRACE_T23_AWB_TREND_STATUS_OFFSET,
                                      &tr_status, sizeof(tr_status)) ||
        regtrace_t23_read_tuning_data(REGTRACE_T23_AWB_TREND_OFFSET, tr,
                                      sizeof(tr)))
        return;
    mutex_lock(&t23x_awb_api_mutex);
    st = (uint32_t *)(void *)awb_cluster_api_status;
    st[0] = cl_status;
    st[1] = 0;
    memcpy(awb_cluster_api_para, cl, sizeof(cl));
    if (cl_status != 2U)
        memcpy(_awb_cluster, cl, sizeof(cl));
    st = (uint32_t *)(void *)&awb_trend_api_status;
    st[0] = tr_status;
    st[1] = 0;
    memcpy(awb_trend_api_para, tr, sizeof(tr));
    if (tr_status != 2U)
        memcpy((void *)&_awb_trend, tr, sizeof(tr));
    mutex_unlock(&t23x_awb_api_mutex);
}

#define REGTRACE_T23_AWB_HLIL_COLS 15U
#define REGTRACE_T23_AWB_HLIL_ROWS 15U
#define REGTRACE_T23_AWB_HLIL_ZONES \
    (REGTRACE_T23_AWB_HLIL_COLS * REGTRACE_T23_AWB_HLIL_ROWS)

struct regtrace_t23_awb_hlil_snapshot {
    uint32_t r[REGTRACE_T23_AWB_HLIL_ZONES];
    uint32_t g[REGTRACE_T23_AWB_HLIL_ZONES];
    uint32_t b[REGTRACE_T23_AWB_HLIL_ZONES];
    uint32_t pixels[REGTRACE_T23_AWB_HLIL_ZONES];
    uint32_t bank;
    uint32_t snapshot;
};

struct regtrace_t23_awb_hlil_result {
    uint32_t rg;
    uint32_t bg;
    uint32_t ct;
    uint32_t active_zones;
    uint32_t light_source_zones;
    bool no_weight;
};

static struct regtrace_t23_awb_hlil_snapshot
    regtrace_t23_awb_hlil_snapshot_data;
static uint32_t regtrace_t23_awb_hlil_ratio[REGTRACE_T23_AWB_HLIL_ZONES * 2U];
static uint32_t regtrace_t23_awb_hlil_pixel_weight[REGTRACE_T23_AWB_HLIL_ZONES];
static uint32_t regtrace_t23_awb_hlil_mesh_weight[REGTRACE_T23_AWB_HLIL_ZONES];
static uint32_t regtrace_t23_awb_hlil_distance[REGTRACE_T23_AWB_HLIL_ZONES];
static uint32_t regtrace_t23_awb_hlil_distance_weight[REGTRACE_T23_AWB_HLIL_ZONES];
static struct t23x_awb_history regtrace_t23_awb_hist;
static bool regtrace_t23_awb_hlil_pending;
/* Set while a parameter-bank switch reloads the blocks; blocks new work. */
static bool regtrace_t23_awb_hlil_paused;
/* WB_GOL_STATIS r/b and WB_STATIS r/b ratios, Q8 (0x10000 / mf gain). */
static uint32_t regtrace_t23_awb_statis[4];
static DEFINE_SPINLOCK(regtrace_t23_awb_hlil_lock);

static void regtrace_t23_source_awb_hlil_work(struct work_struct *work);
static DECLARE_WORK(regtrace_t23_source_awb_hlil_work_item,
                    regtrace_t23_source_awb_hlil_work);

static uint32_t regtrace_t23_awb_hlil_mul_q(uint32_t left, uint32_t right)
{
    return (uint32_t)(((uint64_t)left * right) >>
                      REGTRACE_T23_AWB_HLIL_POINT_POSITION);
}

static uint32_t regtrace_t23_awb_hlil_div_q(uint64_t numerator,
                                            uint64_t denominator)
{
    if (!denominator)
        return 0;
    return (uint32_t)div64_u64(
        numerator << REGTRACE_T23_AWB_HLIL_POINT_POSITION, denominator);
}

static uint32_t regtrace_t23_awb_hlil_interpolate_q(uint32_t x_q,
                                                    uint32_t x0,
                                                    uint32_t x1,
                                                    uint32_t y0_q,
                                                    uint32_t y1_q)
{
    uint32_t q = REGTRACE_T23_AWB_HLIL_POINT_POSITION;
    uint32_t x0_q = x0 << q;
    uint32_t span_q = (x1 - x0) << q;
    uint32_t delta_x;
    uint64_t delta;

    if (!span_q || x_q <= x0_q)
        return y0_q;
    if (x_q >= (x1 << q))
        return y1_q;

    delta_x = x_q - x0_q;
    if (y1_q >= y0_q) {
        delta = (uint64_t)(y1_q - y0_q) * delta_x;
        return y0_q + (uint32_t)div64_u64(delta, span_q);
    }

    delta = (uint64_t)(y0_q - y1_q) * delta_x;
    return y0_q - (uint32_t)div64_u64(delta, span_q);
}

static uint32_t regtrace_t23_awb_hlil_mesh_q(const uint32_t *mesh,
                                             uint32_t rg_q,
                                             uint32_t bg_q)
{
    const uint32_t *rg_pos = regtrace_t23_awb_hlil_rg_positions;
    const uint32_t *bg_pos = regtrace_t23_awb_hlil_bg_positions;
    uint32_t q = REGTRACE_T23_AWB_HLIL_POINT_POSITION;
    uint32_t rg_index = 13U;
    uint32_t bg_index = 13U;
    uint32_t row0_q;
    uint32_t row1_q;
    uint32_t i;

    rg_q = max(rg_q, rg_pos[0] << q);
    rg_q = min(rg_q, rg_pos[14] << q);
    bg_q = max(bg_q, bg_pos[0] << q);
    bg_q = min(bg_q, bg_pos[14] << q);

    for (i = 0; i < 14U; ++i) {
        if (rg_q <= (rg_pos[i + 1U] << q)) {
            rg_index = i;
            break;
        }
    }
    for (i = 0; i < 14U; ++i) {
        if (bg_q <= (bg_pos[i + 1U] << q)) {
            bg_index = i;
            break;
        }
    }

    row0_q = regtrace_t23_awb_hlil_interpolate_q(
        rg_q, rg_pos[rg_index], rg_pos[rg_index + 1U],
        mesh[bg_index * 15U + rg_index] << q,
        mesh[bg_index * 15U + rg_index + 1U] << q);
    row1_q = regtrace_t23_awb_hlil_interpolate_q(
        rg_q, rg_pos[rg_index], rg_pos[rg_index + 1U],
        mesh[(bg_index + 1U) * 15U + rg_index] << q,
        mesh[(bg_index + 1U) * 15U + rg_index + 1U] << q);

    return regtrace_t23_awb_hlil_interpolate_q(
        bg_q, bg_pos[bg_index], bg_pos[bg_index + 1U], row0_q, row1_q);
}

static uint32_t regtrace_t23_awb_hlil_light_source_weight_q(uint32_t rg_q,
                                                            uint32_t bg_q)
{
    const uint32_t q = REGTRACE_T23_AWB_HLIL_POINT_POSITION;
    const uint32_t rounding = 1U << (q - 1U);
    uint32_t rg = (rg_q + rounding) >> q;
    uint32_t bg = (bg_q + rounding) >> q;
    uint32_t best_weight_q = 0;
    uint32_t source;

    for (source = 0; source < REGTRACE_T23_AWB_HLIL_LIGHT_SOURCE_COUNT;
         ++source) {
        uint32_t ref_rg = regtrace_t23_awb_hlil_light_sources[source * 2U];
        uint32_t ref_bg =
            regtrace_t23_awb_hlil_light_sources[source * 2U + 1U];
        uint32_t delta_rg;
        uint32_t delta_bg;
        uint32_t distance;
        uint32_t weight = 0;

        if (ref_rg)
            ref_rg--;
        if (ref_bg)
            ref_bg--;
        delta_rg = rg >= ref_rg ? rg - ref_rg : ref_rg - rg;
        delta_bg = bg >= ref_bg ? bg - ref_bg : ref_bg - bg;
        distance = delta_rg * delta_rg + delta_bg * delta_bg;

        if (distance < 0x332U) {
            weight = 1U;
            if (distance < 0x2a7U) {
                weight = 2U;
                if (distance < 0x267U) {
                    weight = 3U;
                    if (distance < 0x23cU) {
                        weight = 4U;
                        if (distance < 0x21cU) {
                            weight = 5U;
                            if (distance < 0x202U)
                                weight =
                                    regtrace_t23_awb_hlil_light_source_weight_lut[
                                        distance];
                        }
                    }
                }
            }
        }
        best_weight_q = max(best_weight_q, weight << q);
    }

    return best_weight_q;
}

/*
 * Zone arrays of one AWB run, named as the stock .bss they mirror
 * (zone_rgbg, zone_pix_cnt, zone_pix_wgh, rgbg_wght, rgbg_dis, rgbg_d_wght)
 * so the emulator audit can compare them zone by zone.
 */
static uint32_t regtrace_t23_awb_hlil_zone_pix[REGTRACE_T23_AWB_HLIL_ZONES];

/* stock fix_point_mult3_32: two truncating products */
static uint32_t regtrace_t23_awb_hlil_mul3_q(uint32_t a, uint32_t b,
                                             uint32_t c)
{
    return regtrace_t23_awb_hlil_mul_q(regtrace_t23_awb_hlil_mul_q(a, b), c);
}

/*
 * The cluster part of stock Tiziano_Awb_Ct_Detect.  Every run fills the
 * rg/bg histogram of the 225 zones as stock does (it is state of the next
 * cluster run); with the cluster object enabled (ClusterEn == 1) the zone
 * weights are then replaced by the cluster weights (0x14e8c..0x15628).
 * Stock handles every zone, also the ones below the pixel threshold (their
 * ratios read 0 and are clamped to the first position); so does this.
 */
static void regtrace_t23_awb_cluster_stage(const uint32_t *zr,
                                           const uint32_t *zb, uint32_t *zw)
{
    t23x_awb_cluster_count(&regtrace_t23_awb_cluster_state, zr, zb,
                           regtrace_t23_awb_hlil_rg_positions,
                           regtrace_t23_awb_hlil_bg_positions,
                           REGTRACE_T23_AWB_HLIL_POINT_POSITION);
    if (regtrace_t23_awb_cl_run[0] != 1U)
        return;
    (void)t23x_awb_cluster_weights(&regtrace_t23_awb_cluster_state,
                                   regtrace_t23_awb_cl_run, zr, zb, zw,
                                   regtrace_t23_awb_hlil_rg_positions,
                                   regtrace_t23_awb_hlil_bg_positions,
                                   REGTRACE_T23_AWB_HLIL_POINT_POSITION);
}

/* "nothing to weigh": stock Tiziano_Awb_Ct_Detect returns rg = bg = 256,
 * ct = 5000 and flags it (the flag clears the ramp targets in set_gain's
 * caller). */
static void regtrace_t23_awb_hlil_no_weight(
    struct regtrace_t23_awb_hlil_result *result, uint32_t active)
{
    result->rg = 0x100U;
    result->bg = 0x100U;
    result->ct = 5000U;
    result->active_zones = active;
    result->no_weight = true;
}

/*
 * Tiziano_awb_fpga (zone ratios, pixel threshold) and Tiziano_Awb_Ct_Detect
 * (pixel weights, mesh and light source weights, cluster stage, weighted
 * mean, distance refinement, colour temperature) of the stock module, with
 * the stock fix_point_* arithmetic (every product truncates, see
 * regtrace_t23_awb_hlil_mul_q / _mul3_q).
 */
static int regtrace_t23_awb_hlil_calculate(
    const struct regtrace_t23_awb_hlil_snapshot *snapshot,
    struct regtrace_t23_awb_hlil_result *result)
{
    const uint32_t q = REGTRACE_T23_AWB_HLIL_POINT_POSITION;
    const uint32_t rounding = 1U << (q - 1U);
    const uint32_t total = REGTRACE_T23_AWB_HLIL_ZONES;
    const uint32_t bg_offset = REGTRACE_T23_AWB_HLIL_ZONES;
    const uint32_t min_pixels = regtrace_t23_source_awb_hlil_min_pixels;
    uint32_t *zpix = regtrace_t23_awb_hlil_zone_pix;
    uint32_t *zr = regtrace_t23_awb_hlil_ratio;
    uint32_t *zb = regtrace_t23_awb_hlil_ratio + bg_offset;
    uint32_t *pixw = regtrace_t23_awb_hlil_pixel_weight;
    uint32_t *wght = regtrace_t23_awb_hlil_mesh_weight;
    uint32_t *dis = regtrace_t23_awb_hlil_distance;
    uint32_t *dwght = regtrace_t23_awb_hlil_distance_weight;
    const uint32_t *zone_w = regtrace_t23_awb_hlil_zone_weight_mesh;
    const uint32_t *rg_pos = regtrace_t23_awb_hlil_rg_positions;
    const uint32_t *bg_pos = regtrace_t23_awb_hlil_bg_positions;
    uint32_t max_pixels = 0;
    uint32_t coefficient_rg_q;
    uint32_t coefficient_bg_q;
    uint64_t total_mesh_weight = 0;
    uint64_t sum_rg = 0;
    uint64_t sum_bg = 0;
    uint64_t sum_weight = 0;
    uint32_t average_rg_q;
    uint32_t average_bg_q;
    uint32_t active_zones = 0;
    bool indoor = regtrace_t23_ae_ev_q10() >=
                  (REGTRACE_T23_AWB_HLIL_INDOOR_EV << q);
    const uint32_t *rgbg_weight_mesh = indoor ?
        regtrace_t23_awb_hlil_indoor_ct_weight_mesh :
        regtrace_t23_awb_hlil_outdoor_ct_weight_mesh;
    uint32_t i;

    coefficient_rg_q = regtrace_t23_awb_hlil_mul_q(
        0x100U << q, regtrace_t23_awb_hlil_wb_static[0]);
    coefficient_bg_q = regtrace_t23_awb_hlil_mul_q(
        0x100U << q, regtrace_t23_awb_hlil_wb_static[1]);

    /* Tiziano_awb_fpga: ratios; no green or at most the IQ pixel threshold
     * zeroes ratios and pixel count */
    for (i = 0; i < total; ++i) {
        uint32_t green = snapshot->g[i];
        uint32_t pixels = snapshot->pixels[i];
        uint32_t rg_q = 0;
        uint32_t bg_q = 0;

        if (green) {
            uint32_t raw_rg_q = (uint32_t)div64_u64(
                (uint64_t)snapshot->r[i] << q, green);
            uint32_t raw_bg_q = (uint32_t)div64_u64(
                (uint64_t)snapshot->b[i] << q, green);

            rg_q = regtrace_t23_awb_hlil_mul_q(raw_rg_q, coefficient_rg_q);
            bg_q = regtrace_t23_awb_hlil_mul_q(raw_bg_q, coefficient_bg_q);
        } else {
            pixels = 0;
        }
        /* source_awb_hlil_min_pixels != 0 replaces the IQ threshold by
         * "fewer than N pixels" (debug); 0 is stock */
        if (min_pixels ? pixels < min_pixels :
            pixels <= REGTRACE_T23_AWB_HLIL_PIXEL_THRESHOLD) {
            rg_q = 0;
            bg_q = 0;
            pixels = 0;
        }
        zpix[i] = pixels;
        zr[i] = rg_q;
        zb[i] = bg_q;
        max_pixels = max(max_pixels, pixels);
    }

    /* Tiziano_Awb_Ct_Detect: no zone with a pixel at all leaves at the
     * first pixel weight, before the clamp, the mesh weights and the
     * cluster histogram (the zone arrays keep their last contents) */
    if (!max_pixels) {
        pixw[0] = 0;
        regtrace_t23_awb_hlil_no_weight(result, 0);
        return 0;
    }

    /* ratios clamped to the position range, pixel weights, mesh weights */
    for (i = 0; i < total; ++i) {
        zr[i] = min(max(zr[i], rg_pos[0] << q), rg_pos[14] << q);
        zb[i] = min(max(zb[i], bg_pos[0] << q), bg_pos[14] << q);
        pixw[i] = regtrace_t23_awb_hlil_div_q(zpix[i], max_pixels);
        wght[i] = regtrace_t23_awb_hlil_mesh_q(rgbg_weight_mesh, zr[i],
                                               zb[i]);
        if (indoor) {
            uint32_t light_weight =
                regtrace_t23_awb_hlil_light_source_weight_q(zr[i], zb[i]);

            if (light_weight > wght[i]) {
                wght[i] = light_weight;
                if (pixw[i])
                    result->light_source_zones++;
            }
        }
        if (pixw[i])
            active_zones++;
    }
    regtrace_t23_awb_cluster_stage(zr, zb, wght);
    for (i = 0; i < total; ++i)
        total_mesh_weight += wght[i];
    if (!total_mesh_weight) {
        regtrace_t23_awb_hlil_no_weight(result, active_zones);
        return 0;
    }

    /* weighted mean: mesh weight * pixel weight * zone weight (IQ) */
    for (i = 0; i < total; ++i) {
        uint32_t weight = regtrace_t23_awb_hlil_mul3_q(
            wght[i], pixw[i], zone_w[i] << q);

        sum_rg += regtrace_t23_awb_hlil_mul_q(zr[i], weight);
        sum_bg += regtrace_t23_awb_hlil_mul_q(zb[i], weight);
        sum_weight += weight;
    }
    if (!sum_weight) {
        regtrace_t23_awb_hlil_no_weight(result, active_zones);
        return 0;
    }

    average_rg_q = regtrace_t23_awb_hlil_div_q(sum_rg, sum_weight);
    average_bg_q = regtrace_t23_awb_hlil_div_q(sum_bg, sum_weight);

    if (regtrace_t23_awb_hlil_distance_parameters[0] == 1U) {
        uint32_t distance_sum = 0;
        uint64_t refined_rg = 0;
        uint64_t refined_bg = 0;
        uint64_t refined_weight = 0;
        uint32_t normalized_distance;

        for (i = 0; i < total; ++i) {
            uint32_t delta_rg = average_rg_q >= zr[i] ?
                average_rg_q - zr[i] : zr[i] - average_rg_q;
            uint32_t delta_bg = average_bg_q >= zb[i] ?
                average_bg_q - zb[i] : zb[i] - average_bg_q;

            dis[i] = regtrace_t23_awb_hlil_mul_q(delta_rg, delta_rg) +
                     regtrace_t23_awb_hlil_mul_q(delta_bg, delta_bg);
            if (!wght[i] || !pixw[i] || !zone_w[i])
                continue;
            distance_sum += regtrace_t23_awb_hlil_mul_q(
                regtrace_t23_awb_hlil_mul3_q(dis[i], wght[i], pixw[i]),
                zone_w[i] << q);
        }

        /* stock hands the low word of the summed weight to the division */
        normalized_distance = regtrace_t23_awb_hlil_div_q(
            distance_sum, (uint32_t)sum_weight);
        if (normalized_distance) {
            uint32_t distance_hi =
                regtrace_t23_awb_hlil_distance_parameters[1];
            uint32_t distance_lo =
                regtrace_t23_awb_hlil_distance_parameters[2];

            for (i = 0; i < total; ++i) {
                uint32_t relative_distance =
                    regtrace_t23_awb_hlil_div_q(dis[i], normalized_distance);
                uint32_t weight;

                if (distance_hi < relative_distance) {
                    dwght[i] = 0;
                } else if (relative_distance < distance_lo) {
                    dwght[i] = wght[i];
                } else {
                    /* hi == lo cannot be divided (stock reads garbage);
                     * zero ramp then */
                    uint32_t ramp = distance_hi > distance_lo ?
                        regtrace_t23_awb_hlil_div_q(
                            relative_distance - distance_lo,
                            distance_hi - distance_lo) : 0;

                    dwght[i] = regtrace_t23_awb_hlil_mul_q(
                        wght[i], (1U << q) - ramp);
                }
                weight = regtrace_t23_awb_hlil_mul3_q(
                    dwght[i], pixw[i], zone_w[i] << q);
                refined_rg += regtrace_t23_awb_hlil_mul_q(zr[i], weight);
                refined_bg += regtrace_t23_awb_hlil_mul_q(zb[i], weight);
                refined_weight += weight;
            }
            if (refined_weight) {
                average_rg_q = regtrace_t23_awb_hlil_div_q(
                    refined_rg, refined_weight);
                average_bg_q = regtrace_t23_awb_hlil_div_q(
                    refined_bg, refined_weight);
            }
        }
    }

    average_rg_q = max(average_rg_q, rg_pos[0] << q);
    average_rg_q = min(average_rg_q, rg_pos[14] << q);
    average_bg_q = max(average_bg_q, bg_pos[0] << q);
    average_bg_q = min(average_bg_q, bg_pos[14] << q);

    result->rg = (average_rg_q + rounding) >> q;
    result->bg = (average_bg_q + rounding) >> q;
    result->active_zones = active_zones;
    {
        uint32_t inverse_ct_q = regtrace_t23_awb_hlil_mesh_q(
            regtrace_t23_awb_hlil_color_temperature_mesh,
            average_rg_q, average_bg_q);

        /* fix_point_div_32(1000000 << q, inverse), rounded, back to Q0 */
        result->ct = inverse_ct_q ?
            (regtrace_t23_awb_hlil_div_q(1000000U << q, inverse_ct_q) +
             rounding) >> q : 5000U;
    }
    return 0;
}

static uint32_t regtrace_t23_awb_hlil_ratio_to_mf(uint32_t ratio)
{
    uint32_t q = REGTRACE_T23_AWB_HLIL_POINT_POSITION;
    uint32_t rounding = 1U << (q - 1U);
    uint64_t reciprocal;

    if (!ratio)
        return 0x100U;
    reciprocal = div64_u64(1ULL << (q + 16U), ratio);
    return (uint32_t)((reciprocal + rounding) >> q);
}

static void regtrace_t23_source_awb_hlil_work_body(struct work_struct *work);

static void regtrace_t23_source_awb_hlil_work(struct work_struct *work)
{
    regtrace_t23_text_check("awb_hlil_work-enter", 0);
    regtrace_t23_source_awb_hlil_work_body(work);
    regtrace_t23_text_check("awb_hlil_work-exit", 0);
}

static void regtrace_t23_source_awb_hlil_work_body(struct work_struct *work)
{
    struct regtrace_t23_awb_hlil_result result;
    unsigned long flags;
    uint32_t auto_mf_rg;
    uint32_t auto_mf_bg;
    uint32_t mf_rg;
    uint32_t mf_bg;
    uint32_t wb_mode;
    uint32_t gain_rg;
    uint32_t gain_bg;
    uint32_t trend_ct;
    uint32_t trend[T23X_AWB_TREND_WORDS];
    uint32_t tolerance_en;
    uint32_t tolerance_th;
    int ret;

    (void)work;
    /* the cluster / trend objects of this run, picked like stock
     * (JZ_Isp_Awb, JZ_Isp_Awb_Awbg2reg); the update tolerance reads the
     * live cluster object (Tiziano_awb_fpga gets &_awb_cluster) */
    mutex_lock(&t23x_awb_api_mutex);
    memcpy(regtrace_t23_awb_cl_run,
           t23x_awb_obj_pick((const uint32_t *)(const void *)_awb_cluster,
                             (const uint32_t *)(const void *)awb_cluster_api_para,
                             (const uint32_t *)(const void *)awb_cluster_api_status),
           sizeof(regtrace_t23_awb_cl_run));
    tolerance_en = ((const uint32_t *)(const void *)_awb_cluster)[8];
    tolerance_th = ((const uint32_t *)(const void *)_awb_cluster)[9];
    memcpy(trend,
           t23x_awb_obj_pick((const uint32_t *)(const void *)&_awb_trend,
                             (const uint32_t *)(const void *)awb_trend_api_para,
                             (const uint32_t *)(const void *)&awb_trend_api_status),
           sizeof(trend));
    mutex_unlock(&t23x_awb_api_mutex);
    memset(&result, 0, sizeof(result));
    ret = regtrace_t23_awb_hlil_calculate(
        &regtrace_t23_awb_hlil_snapshot_data, &result);
    regtrace_t23_source_awb_hlil_runs++;
    regtrace_t23_source_awb_hlil_status = ret ? 1U : 0U;

    if (!ret) {
        uint32_t *mf = (uint32_t *)(void *)_awb_mf_para;
        uint32_t raw_mf_rg = regtrace_t23_awb_hlil_ratio_to_mf(result.rg);
        uint32_t raw_mf_bg = regtrace_t23_awb_hlil_ratio_to_mf(result.bg);
        uint32_t steps;
        bool first;
        bool update;
        bool moa;

        regtrace_t23_awb_statis[0] = raw_mf_rg ? 0x10000U / raw_mf_rg : 0x100U;
        regtrace_t23_awb_statis[1] = raw_mf_bg ? 0x10000U / raw_mf_bg : 0x100U;
        /* the history, the change test, the gain ramp: Tiziano_awb_fpga */
        steps = t23x_awb_history_push(&regtrace_t23_awb_hist, &result.rg,
                                      &result.bg, &result.ct,
                                      REGTRACE_T23_AWB_HLIL_HISTORY_WINDOW,
                                      &first);
        moa = ACCESS_ONCE(awb_moa) != 0;
        wb_mode = ACCESS_ONCE(regtrace_t23_source_wb_mode);
        update = t23x_awb_need_update(first, result.rg, result.bg,
                                      regtrace_t23_awb_orig_rg,
                                      regtrace_t23_awb_orig_bg,
                                      wb_mode != 0U || moa, mf);
        if (update) {
            regtrace_t23_awb_orig_rg = result.rg;
            regtrace_t23_awb_orig_bg = result.bg;
            t23x_awb_ramp(mf, regtrace_t23_awb_hlil_ratio_to_mf(result.rg),
                          regtrace_t23_awb_hlil_ratio_to_mf(result.bg), steps,
                          result.no_weight,
                          regtrace_t23_awb_hlil_tolerance_default,
                          tolerance_en, tolerance_th);
        }
        auto_mf_rg = mf[4];
        auto_mf_bg = mf[5];
        regtrace_t23_source_awb_last_mf_rg = auto_mf_rg;
        regtrace_t23_source_awb_last_mf_bg = auto_mf_bg;
        regtrace_t23_awb_statis[2] = auto_mf_rg ? 0x10000U / auto_mf_rg : 0x100U;
        regtrace_t23_awb_statis[3] = auto_mf_bg ? 0x10000U / auto_mf_bg : 0x100U;

        /* the colour temperature the gains are written at (stock sets
         * the custom SetAwbCt value only after Tiziano_awb_fpga) */
        trend_ct = result.ct;
        result.ct = t23x_awb_ct_select(wb_mode, result.ct,
                                       &regtrace_t23_awb_custom_ct);
        mf_rg = auto_mf_rg;
        mf_bg = auto_mf_bg;
        if (wb_mode == 9U) {
            mf_rg = ((ACCESS_ONCE(regtrace_t23_source_wb_mode_rg) + 0x40U) *
                     auto_mf_rg) >> 6;
            mf_bg = ((ACCESS_ONCE(regtrace_t23_source_wb_mode_bg) + 0x40U) *
                     auto_mf_bg) >> 6;
        } else if (wb_mode != 0U) {
            mf_rg = ACCESS_ONCE(regtrace_t23_source_wb_mode_rg);
            mf_bg = ACCESS_ONCE(regtrace_t23_source_wb_mode_bg);
        }
        gain_rg = (mf_rg * regtrace_t23_awb_hlil_wb_static[0] +
                   (1U << (REGTRACE_T23_AWB_HLIL_POINT_POSITION - 1U))) >>
                  REGTRACE_T23_AWB_HLIL_POINT_POSITION;
        gain_bg = (mf_bg * regtrace_t23_awb_hlil_wb_static[1] +
                   (1U << (REGTRACE_T23_AWB_HLIL_POINT_POSITION - 1U))) >>
                  REGTRACE_T23_AWB_HLIL_POINT_POSITION;
        /* Awbg2reg: Q2 gain, the colour temperature trend, the limit */
        gain_rg = t23x_awb_gain_trend(trend, trend_ct, gain_rg, 0);
        gain_bg = t23x_awb_gain_trend(trend, trend_ct, gain_bg, 1);
        gain_rg = min((gain_rg *
                       ACCESS_ONCE(regtrace_t23_source_awb_profile_rbias) +
                       (REGTRACE_T23_AWB_PROFILE_BIAS_UNITY >> 1)) >> 10,
                      0x3fffU);
        gain_bg = min((gain_bg *
                       ACCESS_ONCE(regtrace_t23_source_awb_profile_bbias) +
                       (REGTRACE_T23_AWB_PROFILE_BIAS_UNITY >> 1)) >> 10,
                      0x3fffU);

        regtrace_t23_source_awb_hlil_rg = result.rg;
        regtrace_t23_source_awb_hlil_bg = result.bg;
        regtrace_t23_source_awb_hlil_ct = result.ct;
        if (regtrace_t23_source_ccm_events &&
            regtrace_t23_source_ccm_tuning_init &&
            result.ct >= 1500U && result.ct <= 10000U) {
            uint32_t delta = regtrace_t23_source_ccm_last_ct >= result.ct ?
                regtrace_t23_source_ccm_last_ct - result.ct :
                result.ct - regtrace_t23_source_ccm_last_ct;

            if (delta > 0x20U)
                regtrace_t23_source_ccm_commit(
                    result.ct, regtrace_t23_ae_ev_q10());
        }
        if (regtrace_t23_source_lsc_events &&
            regtrace_t23_source_lsc_tuning_init &&
            regtrace_t23_source_lsc_tables_initialized &&
            result.ct >= ((uint32_t *)(void *)lsc_ct_points)[0] &&
            result.ct <= 10000U) {
            uint32_t delta = regtrace_t23_source_lsc_last_ct >= result.ct ?
                regtrace_t23_source_lsc_last_ct - result.ct :
                result.ct - regtrace_t23_source_lsc_last_ct;

            if (delta > 0x20U) {
                tisp_lsc_ct_update(result.ct);
                if (!tisp_lsc_write_lut_datas()) {
                    regtrace_t23_source_lsc_last_ct = result.ct;
                    regtrace_t23_source_lsc_event_count++;
                }
            }
        }
        if (regtrace_t23_source_bcsh_events &&
            result.ct >= 2000U && result.ct <= 10000U) {
            uint32_t delta = regtrace_t23_source_bcsh_last_ct >= result.ct ?
                regtrace_t23_source_bcsh_last_ct - result.ct :
                result.ct - regtrace_t23_source_bcsh_last_ct;

            if (delta > 0x20U) {
                tisp_bcsh_ct_update(0, result.ct);
                regtrace_t23_source_bcsh_last_ct = result.ct;
                regtrace_t23_source_bcsh_ct_events++;
            }
        }
        if (regtrace_t23_source_clm_events &&
            regtrace_t23_source_clm_tuning_init &&
            result.ct >= 1000U && result.ct <= 10000U)
            tisp_clm_ct_update(0, result.ct);
        /* set_gain: stock writes the gains whenever the change test above
         * passed (and the AWB is not frozen); the optional dead band is
         * beyond stock (module parameter, 0 = off) */
        if (update && !ACCESS_ONCE(awb_frz) &&
            (!regtrace_t23_source_awb_hlil_deadband ||
             regtrace_t23_awb_absdiff(gain_rg,
                 regtrace_t23_source_awb_last_rgain) >
                 regtrace_t23_source_awb_hlil_deadband ||
             regtrace_t23_awb_absdiff(gain_bg,
                 regtrace_t23_source_awb_last_bgain) >
                 regtrace_t23_source_awb_hlil_deadband)) {
            regtrace_t23_source_awb_apply_gains(gain_rg, gain_bg);
            regtrace_t23_source_awb_hlil_updates++;
        }
        if (update)
            ACCESS_ONCE(awb_moa) = 0;

        if (regtrace_t23_source_awb_hlil_runs <= 8U ||
            !(regtrace_t23_source_awb_hlil_runs &
              (regtrace_t23_source_awb_hlil_runs - 1U)))
            printk(KERN_INFO
                   "tx_isp_t23_recovered: source AWB HLIL run=%u snapshot=%u bank=%u zones=%u lights=%u ev=%u target=%u/%u ct=%u mode=%u mf=%u/%u gain=0x%x/0x%x updates=%u\n",
                   regtrace_t23_source_awb_hlil_runs,
                   regtrace_t23_awb_hlil_snapshot_data.snapshot,
                   regtrace_t23_awb_hlil_snapshot_data.bank,
                   result.active_zones, result.light_source_zones,
                   regtrace_t23_ae_ev_q10(),
                   result.rg, result.bg, result.ct, wb_mode,
                   mf_rg, mf_bg, gain_rg, gain_bg,
                   regtrace_t23_source_awb_hlil_updates);
    } else if (ret == -ENODATA) {
        /*
         * No zone inside the CT weight mesh: expected while the AE is
         * still converging after a stream start, and under IR (night)
         * light. Not an error; note it only if it lasts.
         */
        if (regtrace_t23_source_awb_hlil_runs >= 64U &&
            !(regtrace_t23_source_awb_hlil_runs &
              (regtrace_t23_source_awb_hlil_runs - 1U)))
            printk(KERN_INFO
                   "tx_isp_t23_recovered: source AWB HLIL run=%u snapshot=%u: no usable zone yet, gains held\n",
                   regtrace_t23_source_awb_hlil_runs,
                   regtrace_t23_awb_hlil_snapshot_data.snapshot);
    } else if (regtrace_t23_source_awb_hlil_runs <= 8U) {
        printk(KERN_WARNING
               "tx_isp_t23_recovered: source AWB HLIL run=%u snapshot=%u failed ret=%d\n",
               regtrace_t23_source_awb_hlil_runs,
               regtrace_t23_awb_hlil_snapshot_data.snapshot, ret);
    }

    spin_lock_irqsave(&regtrace_t23_awb_hlil_lock, flags);
    regtrace_t23_awb_hlil_pending = false;
    spin_unlock_irqrestore(&regtrace_t23_awb_hlil_lock, flags);
}

static void regtrace_t23_source_awb_hlil_capture(const uint32_t *words,
                                                 uint32_t bank,
                                                 uint32_t snapshot)
{
    unsigned long flags;
    unsigned int zone;
    unsigned int populated = 0;

    if (!regtrace_t23_source_awb_hlil)
        return;
    /* the first snapshot after a stream start is used at once */
    if (regtrace_t23_source_awb_hlil_interval && snapshot != 1U &&
        snapshot % regtrace_t23_source_awb_hlil_interval)
        return;

    spin_lock_irqsave(&regtrace_t23_awb_hlil_lock, flags);
    if (regtrace_t23_awb_hlil_pending || regtrace_t23_awb_hlil_paused) {
        regtrace_t23_source_awb_hlil_drops++;
        spin_unlock_irqrestore(&regtrace_t23_awb_hlil_lock, flags);
        return;
    }

    for (zone = 0; zone < REGTRACE_T23_AWB_HLIL_ZONES;
         ++zone, words += 4) {
        uint32_t w0 = words[0];
        uint32_t w1 = words[1];
        uint32_t w2 = words[2];
        uint32_t w3 = words[3];

        regtrace_t23_awb_hlil_snapshot_data.r[zone] = w0 & 0x1fffffU;
        regtrace_t23_awb_hlil_snapshot_data.g[zone] =
            ((w1 & 0x3ffU) << 11) | (w0 >> 21);
        regtrace_t23_awb_hlil_snapshot_data.b[zone] =
            (w1 & 0x7ffffc00U) >> 10;
        regtrace_t23_awb_hlil_snapshot_data.pixels[zone] =
            ((w3 & 1U) << 12) | (w2 >> 20);
        if (regtrace_t23_awb_hlil_snapshot_data.pixels[zone])
            populated++;
    }
    /*
     * The first statistics interrupt after a stream start can come before
     * a complete statistics frame (seen 17 ms after STREAMON with one of
     * 225 zones written).  Do not use such a partial bank at once; the
     * next scheduled snapshot follows a few frames later.
     */
    if (snapshot == 1U &&
        populated < REGTRACE_T23_AWB_HLIL_ZONES / 2U) {
        regtrace_t23_source_awb_hlil_drops++;
        spin_unlock_irqrestore(&regtrace_t23_awb_hlil_lock, flags);
        return;
    }
    regtrace_t23_awb_hlil_snapshot_data.bank = bank;
    regtrace_t23_awb_hlil_snapshot_data.snapshot = snapshot;
    regtrace_t23_awb_hlil_pending = true;
    spin_unlock_irqrestore(&regtrace_t23_awb_hlil_lock, flags);
    schedule_work(&regtrace_t23_source_awb_hlil_work_item);
}

static void regtrace_t23_source_awb_hlil_reset(void)
{
    unsigned long flags;

    cancel_work_sync(&regtrace_t23_source_awb_hlil_work_item);
    spin_lock_irqsave(&regtrace_t23_awb_hlil_lock, flags);
    regtrace_t23_awb_hlil_pending = false;
    /* a stream restart keeps the white balance history (see
     * regtrace_t23_source_awb_kept); only the first start begins empty */
    if (!regtrace_t23_source_awb_kept) {
        regtrace_t23_awb_hist.primed = false;
        regtrace_t23_awb_orig_rg = 0;
        regtrace_t23_awb_orig_bg = 0;
    }
    spin_unlock_irqrestore(&regtrace_t23_awb_hlil_lock, flags);
    regtrace_t23_source_awb_hlil_runs = 0;
    regtrace_t23_source_awb_hlil_updates = 0;
    regtrace_t23_source_awb_hlil_drops = 0;
    regtrace_t23_source_awb_hlil_status = 0;
    if (!regtrace_t23_source_awb_kept) {
        regtrace_t23_source_awb_hlil_rg = 0;
        regtrace_t23_source_awb_hlil_bg = 0;
        regtrace_t23_source_awb_hlil_ct = REGTRACE_T23_AWB_HLIL_STARTUP_CT;
    }
    regtrace_t23_source_ccm_event_count = 0;
    regtrace_t23_source_ccm_last_ct = regtrace_t23_source_ccm_runtime_ct;
    regtrace_t23_source_lsc_event_count = 0;
    regtrace_t23_source_lsc_last_ct = regtrace_t23_source_lsc_runtime_ct;
    regtrace_t23_source_lsc_update_pending = false;
    regtrace_t23_source_bcsh_ct_events = 0;
    regtrace_t23_source_bcsh_last_ct = 3187U;
    printk(KERN_WARNING
           "tx_isp_t23_recovered: source AWB HLIL reset ev=%u ct=%u q=%u mf=%u/%u interval=%u enabled=%u\n",
           REGTRACE_T23_AWB_HLIL_STARTUP_EV,
           REGTRACE_T23_AWB_HLIL_STARTUP_CT,
           REGTRACE_T23_AWB_HLIL_POINT_POSITION,
           regtrace_t23_awb_hlil_mf_parameters[4],
           regtrace_t23_awb_hlil_mf_parameters[5],
           regtrace_t23_source_awb_hlil_interval,
           regtrace_t23_source_awb_hlil ? 1U : 0U);
}
