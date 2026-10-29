/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 pipeline top (tiziano-tstp): register image of tisp_tstp_reg_cfg.
 *
 * Hand translation of the OEM tisp_tstp_reg_cfg (libt40-firmware 0x6cca0, 259 instructions).
 * The parameter block is the 154-byte main_tstp / sec_tstp buffer (vmalloc'd and zeroed by
 * tisp_tstp_{main,sec}_init, filled by tisp_tstp_param_array_set); it is read with unaligned
 * little-endian words (lwl/lwr) and halfwords.  Registers: (vinum + 0x600) << 8 + offset,
 * 31 writes in the OEM order.  Shared with the host check tests/t40/tstp_host.
 */
#ifndef TX_ISP_T40_TSTP_H
#define TX_ISP_T40_TSTP_H

#define T40_TSTP_PARAM_SIZE 154
#define T40_TSTP_NREGS      31

static inline uint32_t t40_tstp_h(const uint8_t *p, unsigned int o)
{
    return (uint32_t)p[o] | ((uint32_t)p[o + 1] << 8);
}

static inline uint32_t t40_tstp_w(const uint8_t *p, unsigned int o)
{
    return t40_tstp_h(p, o) | (t40_tstp_h(p, o + 2) << 16);
}

/* Fill reg[]/val[] (T40_TSTP_NREGS entries, OEM order); returns the count. */
static inline unsigned int t40_tstp_regs(uint32_t vinum, const uint8_t *p, uint32_t *reg, uint32_t *val)
{
    uint32_t b = (vinum + 1536) << 8;           /* 0x6ccd0 addiu a0,a0,1536; sll s1,a0,8 */
    unsigned int n = 0, i;

#define T40_TSTP_PUT(off, v) do { reg[n] = b + (off); val[n] = (v); n++; } while (0)
    T40_TSTP_PUT(0, t40_tstp_w(p, 0));                                  /* 0x6cccc lwl/lwr 0 */
    T40_TSTP_PUT(4, (t40_tstp_h(p, 4) & 0xf) | ((t40_tstp_h(p, 6) << 4) & 0xff) |
                    ((t40_tstp_h(p, 8) << 8) & 0xf00) | ((t40_tstp_h(p, 10) << 12) & 0x1000) |
                    ((t40_tstp_h(p, 12) << 16) & 0x70000) | ((t40_tstp_h(p, 14) << 20) & 0x700000) |
                    ((t40_tstp_h(p, 16) << 24) & 0x1000000));         /* 0x6ccec..0x6cd68 */
    T40_TSTP_PUT(8, p[18] | (t40_tstp_h(p, 24) << 24) | ((t40_tstp_h(p, 20) << 8) & 0xffff) |
                    ((t40_tstp_h(p, 22) << 16) & 0xff0000));          /* 0x6cd6c..0x6cda0 */
    T40_TSTP_PUT(12, p[26] | (t40_tstp_h(p, 32) << 24) | ((t40_tstp_h(p, 28) << 8) & 0xffff) |
                     ((t40_tstp_h(p, 30) << 16) & 0xff0000));         /* 0x6cda4..0x6cdd8 */
    T40_TSTP_PUT(16, t40_tstp_w(p, 34));                                /* 0x6cddc lwl 37 / lwr 34 */
    T40_TSTP_PUT(20, (t40_tstp_h(p, 38) & 0xf) | ((t40_tstp_h(p, 40) << 8) & 0xf00) |
                     ((t40_tstp_h(p, 42) << 12) & 0x1000) | ((t40_tstp_h(p, 44) << 16) & 0x70000) |
                     ((t40_tstp_h(p, 46) << 20) & 0x700000));         /* 0x6cdec..0x6ce38 */
    T40_TSTP_PUT(32, t40_tstp_w(p, 48));                                /* 0x6ce3c */
    T40_TSTP_PUT(36, (t40_tstp_h(p, 52) & 0xf) | ((t40_tstp_h(p, 54) << 8) & 0xf00) |
                     ((t40_tstp_h(p, 56) << 12) & 0x1000) | ((t40_tstp_h(p, 58) << 16) & 0x70000) |
                     ((t40_tstp_h(p, 60) << 20) & 0x700000));         /* 0x6ce4c..0x6ce98 */
    T40_TSTP_PUT(40, (t40_tstp_h(p, 62) & 1) | ((t40_tstp_h(p, 64) << 4) & 0x30) |
                     ((t40_tstp_h(p, 66) << 8) & 0x300));             /* 0x6ce9c..0x6cec8 */
    T40_TSTP_PUT(48, (t40_tstp_h(p, 68) & 1) | ((t40_tstp_h(p, 70) << 4) & 0x10) |
                     ((t40_tstp_h(p, 72) << 8) & 0x100));             /* 0x6cecc..0x6cef8 */
    T40_TSTP_PUT(64, (t40_tstp_h(p, 74) & 7) | ((t40_tstp_h(p, 76) << 4) & 0x30)); /* 0x6cefc..0x6cf18 */
    for (i = 0; i < 16; i++)                                            /* 0x6cf1c..0x6d018 */
        T40_TSTP_PUT(80 + 4 * i, t40_tstp_w(p, 78 + 4 * i));
    T40_TSTP_PUT(144, t40_tstp_h(p, 142) & 3);                          /* 0x6d01c */
    T40_TSTP_PUT(160, t40_tstp_w(p, 144));                              /* 0x6d02c */
    T40_TSTP_PUT(164, t40_tstp_w(p, 148));                              /* 0x6d03c */
    T40_TSTP_PUT(168, t40_tstp_h(p, 152) & 3);                          /* 0x6d04c, jr.hb tail call */
#undef T40_TSTP_PUT
    return n;
}

#endif /* TX_ISP_T40_TSTP_H */
