/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 per-frame dispatchers of the vendor tisp core: the colour temperature
 * fan-out (tisp_ct_update), the short-frame gain / exposure fan-out
 * (tisp_short_tgain_update, tisp_short_ev_update), and the lateral chroma
 * aberration register refresh without interpolation (tisp_lcac_no_refresh).
 *
 * A block is skipped when its bypass bit is set in the top bypass register
 * (0x40, shadow index 2 in the d03a0 = 1 mode): CCM 0x40, LSC 0x4, BCSH
 * 0x2000, CLM 0x4000; GIB 0x2, DPC 0x1, LSC 0x4 for the short frame.  The
 * block calls and the register access go through struct t32_dispatch_ops.
 * A 64-bit colour temperature / gain is the second argument of every block
 * call (o32: a2:a3), as in the vendor.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_dispatch_oracle).
 */
#ifndef TX_ISP_T32_DISPATCH_H
#define TX_ISP_T32_DISPATCH_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#ifndef T32_HOST_U64
#define T32_HOST_U64
typedef uint64_t u64;
#endif
#ifndef T32_HOST_S16
#define T32_HOST_S16
typedef int16_t s16;
#endif
#endif

#define T32_TOP_BYPASS_REG	0x40
#define T32_BYP_DPC		0x1
#define T32_BYP_GIB		0x2
#define T32_BYP_LSC		0x4
#define T32_BYP_CCM		0x40
#define T32_BYP_BCSH		0x2000
#define T32_BYP_CLM		0x4000

struct t32_dispatch_ops {
	u32 (*reg_read)(u32 ch, u32 reg);		/* tisp_reg_read, direct address */
	void (*reg_write)(u32 ch, u32 reg, u32 val);	/* tisp_reg_write, direct address */
	void (*ccm_ct_update)(u32 ch, u64 ct);
	void (*lsc_ct_update)(u32 ch, u64 ct);
	void (*bcsh_ct_update)(u32 ch, u64 ct);
	void (*clm_ct_update)(u32 ch, u64 ct);
	void (*gib_again_s_update)(u32 ch, u32 gain);	/* the low word only */
	void (*dpc_s_gain_update)(u32 ch, u64 gain);
	void (*lsc_tgain_update_s)(u32 ch, u64 gain);
	void (*wdr_short_ev_update)(u32 ch, u64 ev);
};

/* tisp_ct_update: force = 1 with keep = 0 runs every block, else the bypass bits decide */
static inline int t32_ct_update(const struct t32_dispatch_ops *k, u32 ch, u64 ct, u32 force, u32 keep)
{
	u32 byp;

	if (force == 1 && keep == 0) {
		k->ccm_ct_update(ch, ct);
		k->lsc_ct_update(ch, ct);
		k->bcsh_ct_update(ch, ct);
		k->clm_ct_update(ch, ct);
		return 0;
	}
	byp = k->reg_read(ch, T32_TOP_BYPASS_REG);
	if (!(byp & T32_BYP_CCM))
		k->ccm_ct_update(ch, ct);
	if (!(byp & T32_BYP_LSC))
		k->lsc_ct_update(ch, ct);
	if (!(byp & T32_BYP_BCSH))
		k->bcsh_ct_update(ch, ct);
	if (!(byp & T32_BYP_CLM))
		k->clm_ct_update(ch, ct);
	return 0;
}

/* tisp_short_tgain_update */
static inline int t32_short_tgain_update(const struct t32_dispatch_ops *k, u32 ch, u64 gain)
{
	u32 byp = k->reg_read(ch, T32_TOP_BYPASS_REG);

	if (!(byp & T32_BYP_GIB))
		k->gib_again_s_update(ch, (u32)gain);
	if (!(byp & T32_BYP_DPC))
		k->dpc_s_gain_update(ch, gain);
	if (!(byp & T32_BYP_LSC))
		k->lsc_tgain_update_s(ch, gain);
	return 0;
}

/* tisp_short_ev_update: only with WDR on (channel state byte +36 = 1) */
static inline int t32_short_ev_update(const struct t32_dispatch_ops *k, u32 ch, u8 wdr_on, u64 ev)
{
	if (wdr_on == 1)
		k->wdr_short_ev_update(ch, ev);
	return 0;
}

/*
 * tisp_lcac_no_refresh: six LCAC registers 0x757c.. 0x7590 from the s16
 * parameters at +8234.. +8256 of the channel's LCAC parameter block.
 */
static inline s16 t32_lcac_p(const u8 *p, u32 off)
{
	return *(const s16 *)(p + off);
}

static inline void t32_lcac_no_refresh(const struct t32_dispatch_ops *k, u32 ch, const u8 *p)
{
	k->reg_write(ch, 0x757c, ((u32)t32_lcac_p(p, 8240) & 0x1ff) << 8 | ((u32)t32_lcac_p(p, 8234) & 1));
	k->reg_write(ch, 0x7580, ((u32)t32_lcac_p(p, 8244) & 0x1ff) << 16 | ((u32)t32_lcac_p(p, 8242) & 0x1ff));
	k->reg_write(ch, 0x7584, ((u32)t32_lcac_p(p, 8248) & 0x1ff) << 16 | ((u32)t32_lcac_p(p, 8246) & 0x1ff));
	k->reg_write(ch, 0x7588, ((u32)t32_lcac_p(p, 8236) & 0xfff) << 16 | ((u32)t32_lcac_p(p, 8250) & 0x1ff));
	k->reg_write(ch, 0x758c, ((u32)t32_lcac_p(p, 8254) & 0x1f) << 16 | ((u32)t32_lcac_p(p, 8238) & 0xfff));
	k->reg_write(ch, 0x7590, ((u32)t32_lcac_p(p, 8252) & 0x1f) << 16 | ((u32)t32_lcac_p(p, 8256) & 0xfff));
}

#endif /* TX_ISP_T32_DISPATCH_H */
