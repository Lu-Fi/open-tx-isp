/* Host test of driver/t40/tx_isp_t40_af.h (T40-AF-STATS): the unpack of the AF DMA ring buffer
 * against an independent bit-stream packer (15 x 15 zones, 256-byte rows, 136 bits per zone:
 * Fir0 23, Fir1 23, Iir0 23, Iir1 23, YSum 24, HighLumaCnt 16, 4 pad bits; row trailer byte = the
 * frame counter), the newest-buffer pick and the AF metrics on known inputs.
 *   cc -O1 -Wall -o t main.c && ./t */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* OEM 1.3.1 fix_point_mult2_32 (0x162d4), as in the driver */
static int32_t fix_point_mult2_32(int32_t shift, int32_t left, int32_t right)
{
    uint32_t q = (uint32_t)shift & 31U;
    uint32_t mask = 0xffffffffU >> ((0U - q) & 31U);
    uint32_t x = (uint32_t)left, y = (uint32_t)right;
    uint32_t xi = x >> q, yi = y >> q, xf = x & mask, yf = y & mask;

    return (int32_t)(((xi * yi) << q) + xf * yi + xi * yf + ((xf * yf) >> q));
}

#include "../../../driver/t40/tx_isp_t40_af.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void put_bits(uint8_t *row, unsigned int bit, uint32_t v, unsigned int len)
{
    unsigned int i;

    for (i = 0; i < len; i++, bit++)
        if (v >> i & 1U)
            row[bit / 8] |= (uint8_t)(1U << (bit % 8));
}

static const unsigned int width[6] = { 23, 23, 23, 23, 24, 16 };

int main(void)
{
    static uint8_t raw[T40_AF_RAW_BYTES];
    static uint32_t want[6][15][15], got[6 * 15 * 15];
    uint32_t r, c, i, metric, alt, wl, wh, sw;
    uint8_t fn, wt[225];
    uint16_t prm[5];
    unsigned int seed = 12345;

    memset(raw, 0, sizeof(raw));
    for (r = 0; r < 15; r++) {
        uint8_t *row = raw + r * T40_AF_ROW_STRIDE;

        for (c = 0; c < 15; c++) {
            unsigned int bit = c * 136U;

            for (i = 0; i < 6; i++) {
                seed = seed * 1103515245U + 12345U;
                want[i][r][c] = (seed >> 3) & ((1U << width[i]) - 1U);
                put_bits(row, bit, want[i][r][c], width[i]);
                bit += width[i];
            }
        }
        row[T40_AF_ROW_STRIDE - 1U] = 0xb7;   /* frame counter */
    }
    fn = t40_af_unpack(raw, got);
    CHECK(fn == 0xb7, "frame byte %u", fn);
    for (i = 0; i < 6; i++)
        for (r = 0; r < 15; r++)
            for (c = 0; c < 15; c++)
                CHECK(got[i * 225 + r * 15 + c] == want[i][r][c], "field %u zone %u,%u: %u != %u",
                      i, r, c, got[i * 225 + r * 15 + c], want[i][r][c]);

    {   /* newest buffer by the 8-bit frame counter, wrap included */
        const uint8_t a[4] = { 184, 181, 182, 183 }, b[4] = { 254, 255, 0, 1 }, z[4] = { 0, 0, 0, 0 };

        CHECK(t40_af_pick_newest(a) == 0, "pick a %d", t40_af_pick_newest(a));
        CHECK(t40_af_pick_newest(b) == 3, "pick b %d", t40_af_pick_newest(b));
        CHECK(t40_af_pick_newest(z) == 0, "pick z %d", t40_af_pick_newest(z));
    }

    {   /* metrics: flat FIR = IIR = v, weight 1, p2 + p3 = one -> metric = 225 * v */
        const uint32_t v = 3000;

        for (i = 0; i < 4 * 225; i++)
            got[i] = v;
        memset(wt, 1, sizeof(wt));
        prm[0] = 100; prm[1] = 200; prm[2] = 128; prm[3] = 128; prm[4] = 8;
        sw = t40_af_metrics_calc(got, wt, prm, &metric, &alt, &wl, &wh);
        CHECK(sw == 225, "weight sum %u", sw);
        CHECK(metric == 225U * v && wl == 225U * v && wh == 225U * v, "metric %u wl %u wh %u", metric, wl, wh);
        CHECK(alt == (225U * v) / 8U, "alt %u", alt);
        memset(wt, 0, sizeof(wt));
        sw = t40_af_metrics_calc(got, wt, prm, &metric, &alt, &wl, &wh);
        CHECK(sw == 0 && metric == 0 && alt == 0, "zero weights: sw %u metric %u", sw, metric);
    }
    if (fails) {
        printf("af_stats_host: %d check(s) failed\n", fails);
        return 1;
    }
    printf("af_stats_host: unpack 1350 fields, pick, metrics OK\n");
    return 0;
}
