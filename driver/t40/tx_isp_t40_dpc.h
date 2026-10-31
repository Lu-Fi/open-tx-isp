/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T40 DPC (defective pixel correction, TOP bit 4) in plain C.
 *
 * Rebuilt from the flow of the OEM tisp_dpc_* functions (tiziano-dpc.c in the
 * sc830ai tx-isp-t40.ko): the tuning bank holds a 1074-byte DPC section; the
 * static control fields go to 0x7000..0x7080 ("noref" registers), 23 gain
 * tables of 11 knots are interpolated at the current total gain (16.16 log2)
 * and packed into the threshold registers ("ref" registers), then 0x709c
 * latches the block.  The gain refresh repacks only when the gain moved by
 * at least gain_thres (256 = 1/256 of a gain step).
 *
 * This replaces the literal mips2c chain (tx_isp_t40_dpc_lit.inc up to
 * open-tx-isp next 6d63667b).  tests/t40/dpc_host compares both on the host
 * (register write sequence and interpolated state, all tuning bins, day and
 * night banks, gain sweeps).  Only the main channel exists on T40 here; the
 * OEM second-sensor bank (0x7100, tisp_dpc_sec_*) is not used.
 */
#ifndef TX_ISP_T40_DPC_H
#define TX_ISP_T40_DPC_H

#include "tx_isp_t40_intp.h"

#define T40_DPC_PAR_OFFSET 11960U	/* DPC section inside a tuning bank */
#define T40_DPC_PAR_BYTES  1074U
#define T40_DPC_TABLES     23U
#define T40_DPC_BASE       0x7000U	/* main channel (OEM: (ch + 0x70) << 8) */

struct t40_dpc_state {
	uint8_t par[T40_DPC_PAR_BYTES] __attribute__((aligned(4)));	/* bank copy */
	uint8_t cur[2 * T40_DPC_TABLES] __attribute__((aligned(4)));	/* values at the current gain */
	const uint8_t *ctl;		/* 4 control bytes: linear par+0, WDR par+564 */
	uint16_t ctl_word;		/* u16 at ctl+4 */
	const uint8_t *misc;		/* 6 control bytes: linear par+438, WDR par+978 */
	const uint8_t *tab[T40_DPC_TABLES];	/* 11-knot gain tables (u16, tab[6] u8) */
	uint32_t wdr;
	uint32_t gain_old;		/* 0xffffffff: nothing packed yet */
	uint32_t gain_thres;
};

/* T40C2: state of the C chains lives outside .bss/.data (see T40-LAYOUT in tx_isp_t40_recovered.c) */
static struct t40_dpc_state t40_dpc T40C2 = { .gain_old = 0xffffffffU, .gain_thres = 256 };
static uint32_t t40_dpc_gain_old1 T40C2 = 0xffffffffU;	/* OEM dpc_gain_old[1]: second-sensor unit (0x7100) is not driven */

/* Table offsets in the DPC section (OEM tisp_dpc_wdr_en), linear and WDR.
 * Table 6 is an 8-bit table (11 bytes + pad), all others are 11 x u16. */
static const uint16_t t40_dpc_tab_lin[T40_DPC_TABLES] = {
	74, 96, 30, 52, 118, 140, 162, 174, 196, 218, 240, 262,
	284, 306, 328, 350, 372, 394, 416, 444, 466, 488, 510,
};
static const uint16_t t40_dpc_tab_wdr[T40_DPC_TABLES] = {
	614, 636, 570, 592, 658, 680, 702, 714, 736, 758, 780, 802,
	824, 846, 868, 890, 912, 934, 956, 986, 1008, 1030, 1052,
};
#define T40_DPC_TAB_U8 6U

static inline uint32_t t40_dpc_le16(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8);
}

static inline uint32_t t40_dpc_le32(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* tisp_dpc_wdr_en: select the linear or WDR half of the section */
static void t40_dpc_select(struct t40_dpc_state *d, uint32_t wdr)
{
	const uint16_t *off = wdr ? t40_dpc_tab_wdr : t40_dpc_tab_lin;
	unsigned int i;

	d->wdr = wdr;
	d->ctl = d->par + (wdr ? 564 : 0);
	d->ctl_word = (uint16_t)t40_dpc_le16(d->ctl + 4);
	d->misc = d->par + (wdr ? 978 : 438);
	for (i = 0; i < T40_DPC_TABLES; i++)
		d->tab[i] = d->par + off[i];
}

/* tisp_dpc_intp: every table at total gain (16.16 log2); value i at cur + 2 * i */
static void t40_dpc_interpolate(struct t40_dpc_state *d, uint32_t gain)
{
	unsigned int i;

	for (i = 0; i < T40_DPC_TABLES; i++) {
		if (i == T40_DPC_TAB_U8) {
			d->cur[2 * i] = t40_intp_u8_at(gain, d->tab[i]);
		} else {
			uint32_t v = t40_intp_u16_at(gain, d->tab[i]);

			d->cur[2 * i] = v;
			d->cur[2 * i + 1] = v >> 8;
		}
	}
}

/* tisp_dpc_noref_reg_cfg: static control fields and the 8 words at par+532 */
static void t40_dpc_write_static(const struct t40_dpc_state *d)
{
	const uint8_t *p = d->par, *c = d->ctl, *m = d->misc;
	unsigned int i;

	system_reg_write(T40_DPC_BASE + 0x00, ((uint32_t)d->ctl_word << 16) | c[0] |
			 ((uint32_t)c[2] << 8) | ((uint32_t)c[3] << 12));
	system_reg_write(T40_DPC_BASE + 0x04, c[1] | ((uint32_t)p[21] << 8) |
			 ((uint32_t)p[22] << 16) | ((uint32_t)p[23] << 24));
	system_reg_write(T40_DPC_BASE + 0x08, p[6] | ((uint32_t)p[7] << 8) |
			 ((uint32_t)p[8] << 16) | ((uint32_t)p[12] << 24));
	system_reg_write(T40_DPC_BASE + 0x10, p[16] | ((uint32_t)p[17] << 8) |
			 ((uint32_t)p[18] << 16) | ((uint32_t)p[19] << 20) | ((uint32_t)p[20] << 24));
	system_reg_write(T40_DPC_BASE + 0x14, p[10] | ((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24));
	system_reg_write(T40_DPC_BASE + 0x18, p[11] | ((uint32_t)p[13] << 16));
	system_reg_write(T40_DPC_BASE + 0x5c, m[3] | ((uint32_t)m[4] << 8) | ((uint32_t)m[5] << 16));
	for (i = 0; i < 8; i++)
		system_reg_write(T40_DPC_BASE + 0x64 + 4 * i, t40_dpc_le32(p + 532 + 4 * i));
}

/* Neighbour-check enable mask for check mode 0..4 (both halves identical) */
static uint32_t t40_dpc_neighbour_mask(uint32_t mode)
{
	uint32_t a = mode >= 1, b = mode >= 2, c = mode >= 3, e = mode >= 4;
	uint32_t v = a | (b << 1) | (c << 2) | (e << 3) | (b << 4) | (c << 5) |
		     (e << 6) | (c << 8) | (e << 9) | (e << 12);

	return v | (v << 16);
}

/* Threshold pair minus the black level: OEM u32 arithmetic, so an underflow
 * in the low half spills into the high half like on the hardware path. */
static inline uint32_t t40_dpc_pair_minus(uint32_t lo, uint32_t hi, uint32_t black)
{
	return (lo - black) | ((hi - black) << 16);
}

/* tisp_dpc_ref_reg_cfg: gain-dependent thresholds from cur[] */
static void t40_dpc_write_gain(const struct t40_dpc_state *d)
{
	const uint8_t *o = d->cur, *p = d->par, *m = d->misc;
	uint32_t blk0 = t40_dpc_le16(p + 24), blk1 = t40_dpc_le16(p + 26), blk2 = t40_dpc_le16(p + 28);
	unsigned int i;

	system_reg_write(T40_DPC_BASE + 0x38, t40_dpc_le32(o + 8));
	system_reg_write(T40_DPC_BASE + 0x1c, t40_dpc_pair_minus(t40_dpc_le16(o + 8), t40_dpc_le16(o + 10), blk0));
	system_reg_write(T40_DPC_BASE + 0x0c, t40_dpc_neighbour_mask(o[12]));
	/* the second pair is stored high/low swapped */
	system_reg_write(T40_DPC_BASE + 0x3c, t40_dpc_le16(o + 16) | (t40_dpc_le16(o + 14) << 16));
	system_reg_write(T40_DPC_BASE + 0x20, t40_dpc_pair_minus(t40_dpc_le16(o + 16), t40_dpc_le16(o + 14), blk1));
	for (i = 0; i < 5; i++) {
		const uint8_t *q = o + 18 + 4 * i;

		system_reg_write(T40_DPC_BASE + 0x40 + 4 * i, t40_dpc_le32(q));
		system_reg_write(T40_DPC_BASE + 0x24 + 4 * i,
				 t40_dpc_pair_minus(t40_dpc_le16(q), t40_dpc_le16(q + 2), i < 4 ? blk1 : blk2));
	}
	system_reg_write(T40_DPC_BASE + 0x54, m[0] | (t40_dpc_le16(o + 38) << 16));
	system_reg_write(T40_DPC_BASE + 0x58, m[1] | ((uint32_t)m[2] << 8) | (t40_dpc_le16(o + 40) << 16));
	system_reg_write(T40_DPC_BASE + 0x60, t40_dpc_le32(o + 42));
	system_reg_write(T40_DPC_BASE + 0x84, t40_dpc_le16(o + 4) | (t40_dpc_le16(o + 6) << 12) | ((uint32_t)p[9] << 24));
	system_reg_write(T40_DPC_BASE + 0xa0, t40_dpc_le32(o + 0));
}

static inline void t40_dpc_latch(void)
{
	system_reg_write(T40_DPC_BASE + 0x9c, 1);
}

/* tisp_dpc_all_reg_refresh / tisp_dpc_intp_reg_refresh */
static void t40_dpc_pack(struct t40_dpc_state *d, uint32_t gain, bool with_static)
{
	t40_dpc_interpolate(d, gain);
	if (with_static)
		t40_dpc_write_static(d);
	t40_dpc_write_gain(d);
	t40_dpc_latch();
}

/* tisp_dpc_par_refresh: the first call packs everything; later calls repack
 * the gain registers only when the gain moved by at least thres. */
static void t40_dpc_gain_step(struct t40_dpc_state *d, uint32_t gain, uint32_t thres)
{
	uint32_t old = d->gain_old;

	if (old == 0xffffffffU) {
		d->gain_old = gain;
		t40_dpc_pack(d, gain, true);
		return;
	}
	if ((gain < old ? old - gain : gain - old) < thres)
		return;
	d->gain_old = gain;
	t40_dpc_pack(d, gain, false);
}

/* tisp_dpc_refresh: OEM also repacks everything at the remembered gain */
static void t40_dpc_gain_refresh(struct t40_dpc_state *d, uint32_t gain)
{
	t40_dpc_gain_step(d, gain, d->gain_thres);
	t40_dpc_pack(d, d->gain_old, true);
}

/* tisp_dpc_params_refresh: copy the section of the active bank */
static int t40_dpc_load_bank(struct t40_dpc_state *d)
{
	const uint8_t *bank = (const uint8_t *)(uintptr_t)tparamsN[0];

	if (!bank)
		return -EINVAL;
	memcpy(d->par, bank + T40_DPC_PAR_OFFSET, T40_DPC_PAR_BYTES);
	return 0;
}

/* ---- driver hooks ---- */

static bool regtrace_t40_dpc_on = true;
module_param_named(t40_dpc_on, regtrace_t40_dpc_on, bool, 0644);
MODULE_PARM_DESC(t40_dpc_on, "1 (with t40_gates_oem_offset=1) runs the DPC chain and clears TOP bit 4 after a successful init (default 1)");
static bool regtrace_t40_dpc_active T40C2;

/* OEM tisp_dpc_main_init */
static int t40_dpc_init(void)
{
	struct t40_dpc_state *d = &t40_dpc;
	int ret = t40_dpc_load_bank(d);

	if (!ret) {
		t40_dpc_select(d, d->wdr);
		d->gain_old = 0xffffffffU;
		t40_dpc_gain_refresh(d, 0x10000);
		t40_dpc_gain_step(d, 0x10000, 0x10000);
	}
	regtrace_t40_dpc_active = !ret;
	if (ret)	/* KERN_INFO lines of this driver are dropped (t40_printk) */
		printk(KERN_ERR "tx_isp_t40_recovered: dpc init failed (%d), TOP bit 4 stays bypassed\n", ret);
	return ret;
}

/* OEM tisp_day_or_night_s_ctrl -> tisp_dpc_dn_params_refresh */
static int t40_dpc_dn_refresh(void)
{
	struct t40_dpc_state *d = &t40_dpc;

	if (!regtrace_t40_dpc_active)
		return 0;
	if (t40_dpc_load_bank(d))
		return 0;	/* OEM ignores the copy result */
	t40_dpc_select(d, d->wdr);
	t40_dpc_gain_refresh(d, d->gain_old);
	return 0;
}

/* OEM tisp_main_long_tgain_update -> tisp_dpc_refresh(sensor, tgain) */
static void t40_dpc_refresh(uint32_t tgain)
{
	if (!regtrace_t40_dpc_active)
		return;
	t40_dpc_gain_refresh(&t40_dpc, tgain);
}

static void t40_dpc_cleanup(void)
{
	if (!regtrace_t40_dpc_active)
		return;
	regtrace_t40_dpc_active = false;
	t40_dpc.gain_old = 0xffffffffU;
}

#endif
