/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T23 MDNS reference hold (module parameter mdns_ref_hold, default 0).
 *
 * The 3D denoiser blends the previous frame (the reference plane at
 * 0x7820/0x7828) with the current one by a weight; the reference weight is
 * min(f_min, b_max) / 255 of the words 0x799c (luma) and 0x7a6c (chroma).
 * The driver recomputes these words from the gain tables on every gain step.
 * A user of the reference plane (a streamer drawing a test picture into it)
 * needs the weight at its maximum for as long as it draws, without racing
 * the gain-step rewrite.  While the hold is set the writers emit these
 * words instead of the computed ones.  Value 0 = no change, zero cost.
 *
 *   bit 0  0x799c / 0x7a6c  f_max = f_min = b_max = 0xff, b_min = 0
 *   bit 1  0x7998 / 0x7a68  fake and mv weight = 0xff (other fields kept)
 *   bit 2  0x79a0 / 0x7a70  r_max = r_min = 0xff (increase kept)
 */
#ifndef TX_ISP_T23_MDNS_HOLD_H
#define TX_ISP_T23_MDNS_HOLD_H

#define T23_MDNS_HOLD_WEI    0x1u
#define T23_MDNS_HOLD_FAKEMV 0x2u
#define T23_MDNS_HOLD_R      0x4u
#define T23_MDNS_HOLD_MASK   0x7u

/* reg: 0x799c, 0x7a6c, 0x7998, 0x7a68, 0x79a0, 0x7a70; others pass through. */
static inline unsigned int t23_mdns_hold_word(unsigned int hold, unsigned int reg,
                                              unsigned int computed)
{
    switch (reg) {
    case 0x799c:
    case 0x7a6c:
        return (hold & T23_MDNS_HOLD_WEI) ? 0x00ffffffu : computed;
    case 0x7998:
    case 0x7a68:
        return (hold & T23_MDNS_HOLD_FAKEMV) ?
               (computed & 0xff0000ffu) | 0x00ffff00u : computed;
    case 0x79a0:
    case 0x7a70:
        return (hold & T23_MDNS_HOLD_R) ? (computed & 0xffff0000u) | 0xffffu : computed;
    default:
        return computed;
    }
}

#endif
