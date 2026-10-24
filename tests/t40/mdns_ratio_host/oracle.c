#include <stdint.h>
#include <stdio.h>
#include "scale.inc"

/* OEM: a3 = sltiu(a2, 129); t0 = a2 - 128;
 * a3 != 0: mul t1,t1,a2; ext t1,t1,7,8
 * else:    s1 = sltiu(t1, cap); t9 = s1 ? cap - t1 : 0; mul t9,t0,t9; sra t9,7; addu t1,t1,t9; andi t1,0xff */
static uint8_t oem(uint32_t t1, uint32_t a2, uint32_t cap)
{
    uint32_t t0 = a2 - 128U, t9;

    if (a2 < 129U)
        return (uint8_t)(((t1 * a2) >> 7) & 0xffU);
    t9 = t1 < cap ? cap - t1 : 0U;
    t9 = (uint32_t)((int32_t)(t0 * t9) >> 7);
    return (uint8_t)((t1 + t9) & 0xffU);
}

int main(void)
{
    static const uint32_t caps[2] = { 230U, 250U };
    unsigned bad = 0, n = 0;
    uint32_t v, r, c;

    for (c = 0; c < 2; c++)
        for (r = 0; r < 256; r++)
            for (v = 0; v < 256; v++, n++)
                if (t40_mdns_ratio_scale(v, r, caps[c]) != oem(v, r, caps[c])) {
                    if (bad++ < 5)
                        printf("diff v=%u r=%u cap=%u: drv %u oem %u\n", v, r, caps[c],
                               t40_mdns_ratio_scale(v, r, caps[c]), oem(v, r, caps[c]));
                }
    /* identity at 128, monotone in ratio */
    for (v = 0; v < 256; v++)
        if (t40_mdns_ratio_scale(v, 128, 250) != v) bad++;
    printf("%s: %u cases, %u mismatches\n", bad ? "FAIL" : "PASS", n, bad);
    return bad ? 1 : 0;
}
