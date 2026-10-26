/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 AF statistics (T40-AF-STATS): unpack of the AF DMA ring buffer and the AF metrics, in plain
 * C, rebuilt from the flow of OEM tisp_af_get_statistics / tisp_af_alogrithm / tisp_af_get_metric
 * (T40 SDK 1.3.1 libt40-firmware).  Pure functions (no kernel state): tests/t40/af_stats_host
 * builds them on the host.  The caller supplies fix_point_mult2_32 (OEM 0x162d4).
 */
#ifndef TX_ISP_T40_AF_H
#define TX_ISP_T40_AF_H

#define T40_AF_ZONES      15U
#define T40_AF_STAT_BYTES (6U * T40_AF_ZONES * T40_AF_ZONES * 4U)   /* 5400 */
#define T40_AF_RAW_BYTES  0x1000U
#define T40_AF_ROW_STRIDE 256U
#define T40_AF_BANK_OUTER 0x18674U

static inline uint32_t t40_af_ext(uint32_t v, unsigned int pos, unsigned int len)
{
    return (v >> pos) & ((1U << len) - 1U);
}

/* OEM tisp_af_get_statistics: unpack one 15 x 15 ring buffer, out = u32[6][15][15] */
static uint8_t t40_af_unpack(const uint8_t *raw, uint32_t *out)
{
    uint32_t r, c, i, f[6], w0, w1, w2, w3, w4;
    const uint32_t n = T40_AF_ZONES * T40_AF_ZONES;

    for (r = 0; r < T40_AF_ZONES; r++) {
        for (c = 0; c < T40_AF_ZONES; c++) {
            const uint32_t *w = (const uint32_t *)(raw + r * T40_AF_ROW_STRIDE + 16U * c + 4U * (c / 4U));

            w0 = w[0]; w1 = w[1]; w2 = w[2]; w3 = w[3]; w4 = w[4];
            switch (c & 3U) {
            case 0:
                f[0] = t40_af_ext(w0, 0, 23);
                f[1] = ((w1 << 9) & 0x7ffe00U) | (w0 >> 23);
                f[2] = ((w2 << 18) & 0x7c0000U) | (w1 >> 14);
                f[3] = t40_af_ext(w2, 5, 23);
                f[4] = ((w3 << 4) & 0xfffff0U) | (w2 >> 28);
                f[5] = ((w4 << 12) & 0xffffU) | (w3 >> 20);
                break;
            case 1:
                f[0] = t40_af_ext(w0, 8, 23);
                f[1] = ((w1 << 1) & 0x7ffffeU) | (w0 >> 31);
                f[2] = ((w2 << 10) & 0x7ffc00U) | (w1 >> 22);
                f[3] = ((w3 << 19) & 0x780000U) | (w2 >> 13);
                f[4] = t40_af_ext(w3, 4, 24);
                f[5] = ((w4 << 4) & 0xffffU) | (w3 >> 28);
                break;
            case 2:
                f[0] = ((w1 << 16) & 0x7f0000U) | (w0 >> 16);
                f[1] = t40_af_ext(w1, 7, 23);
                f[2] = ((w2 << 2) & 0x7ffffcU) | (w1 >> 30);
                f[3] = ((w3 << 11) & 0x7ff800U) | (w2 >> 21);
                f[4] = ((w4 << 20) & 0xf00000U) | (w3 >> 12);
                f[5] = t40_af_ext(w4, 4, 16);
                break;
            default:
                f[0] = ((w1 << 8) & 0x7fff00U) | (w0 >> 24);
                f[1] = ((w2 << 17) & 0x7e0000U) | (w1 >> 15);
                f[2] = t40_af_ext(w2, 6, 23);
                f[3] = ((w3 << 3) & 0x7ffff8U) | (w2 >> 29);
                f[4] = ((w4 << 12) & 0xfff000U) | (w3 >> 20);
                f[5] = t40_af_ext(w4, 12, 16);
                break;
            }
            for (i = 0; i < 6; i++)
                out[i * n + r * T40_AF_ZONES + c] = f[i];
        }
    }
    /* 15 columns: (15 - 1) % 4 == 2 -> the frame byte is the top byte of the last zone's fifth word */
    return raw[T40_AF_ROW_STRIDE - 1U];
}

/* the newest completed ring buffer: its frame counter is ahead of (or equal to) all others
 * (mod 256); -1 if none qualifies */
static int t40_af_pick_newest(const uint8_t cnt[4])
{
    int b, x;

    for (b = 0; b < 4; b++) {
        for (x = 0; x < 4; x++)
            if ((uint8_t)(cnt[b] - cnt[x]) >= 128U)
                break;
        if (x == 4)
            return b;
    }
    return -1;
}

/*
 * OEM tisp_af_alogrithm (linear half) + tisp_af_get_metric with the vendor default shift 0.
 * st = u32[6][15][15] (Fir0, Fir1, Iir0, Iir1, ...), wt = u8[15][15] zone weights,
 * prm = u16 p0, p1, p2, p3, fixed-point bits.  Returns the weight sum (0: no metric, outputs 0).
 */
static uint32_t t40_af_metrics_calc(const uint32_t *st, const uint8_t *wt, const uint16_t *prm,
                                    uint32_t *metric, uint32_t *alt, uint32_t *wl, uint32_t *wh)
{
    const uint32_t n = T40_AF_ZONES * T40_AF_ZONES;
    const uint32_t *fir0 = st, *fir1 = st + n, *iir0 = st + 2U * n, *iir1 = st + 3U * n;
    uint32_t p0 = prm[0], p1 = prm[1], p2 = prm[2], p3 = prm[3], bits = prm[4] & 31U;
    uint32_t one = 1U << bits, half = one / 2U;
    uint32_t z, a, b, w, sa = 0, sb = 0, sw = 0, ma, mb;

    for (z = 0; z < n; z++) {
        a = (uint32_t)fix_point_mult2_32(bits, iir0[z] << bits, p0) +
            (uint32_t)fix_point_mult2_32(bits, fir0[z] << bits, one - p0);
        b = (uint32_t)fix_point_mult2_32(bits, iir1[z] << bits, p1) +
            (uint32_t)fix_point_mult2_32(bits, fir1[z] << bits, one - p1);
        a = (a + half) >> bits;
        b = (b + half) >> bits;
        w = wt[z];
        sa += a * w;
        sb += b * w;
        sw += w;
    }
    *metric = *alt = *wl = *wh = 0;
    if (!sw)
        return 0;   /* all zone weights 0: the OEM divides by zero here */
    ma = sa / sw;
    mb = sb / sw;
    *metric = n * (((uint32_t)fix_point_mult2_32(bits, ma << bits, p2) +
                    (uint32_t)fix_point_mult2_32(bits, mb << bits, p3)) >> bits);
    *alt = ((uint32_t)fix_point_mult2_32(bits, (sa >> 3) << bits, p2) +
            (uint32_t)fix_point_mult2_32(bits, (sb >> 3) << bits, p3)) >> bits;
    *wl = n * ma;
    *wh = n * mb;
    return sw;
}

#endif
