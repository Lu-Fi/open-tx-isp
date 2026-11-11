/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32: two /proc show routines of the vendor module (Matt Davis' recovered C,
 * PR #15) as readable C: sinfo_show (0x1208), the 15 files of
 * /proc/<root>/sensor<idx>/ (see tx_isp_t32_sinfo_proc.h), and
 * isp_vic_frd_show (0x183c), the VIC error counters of /proc/jz/isp/isp-w02.
 *
 * The format strings and the argument order are the vendor's.  The OS calls
 * are hooks (struct t32_pshow_ops); the vendor objects are plain memory with
 * 32-bit fields (seq_file private at +72).
 *
 * Checked by tests/t32_oracle (t32_pshow_oracle): the seq_printf /
 * isp_printf stream, lock / unlock and vic_debug_info calls, every byte the
 * vendor writes (the per-sensor error sums), the return values.
 *
 * Vendor behaviour kept on purpose (named here, not fixed):
 *  - sinfo_show reads the slot and the sensor through the proc data without a
 *    check (a NULL proc data faults);
 *  - the "%u" frame rate files divide by the low half of the word, with 0
 *    taken as 1;
 *  - isp_vic_frd_show passes the error sum as a fifth, unused argument of the
 *    " %d, %d, %d, %d" line.
 */
#ifndef TX_ISP_T32_PSHOW_H
#define TX_ISP_T32_PSHOW_H

#include "tx_isp_t32_glue.h"

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

struct t32_pshow_ops {
	int (*seq_printf)(void *m, const char *fmt, ...);
	int (*isp_printf)(int level, const char *fmt, ...);
	void (*mutex_lock)(void *lock);
	void (*mutex_unlock)(void *lock);
	int (*vic_debug_info)(void *m);		/* lib_tisp_vic_debug_info */
};

#define T32_PSHOW_SEQ_PRIV	72U

/* ---- sinfo_show (0x1208) ---- */
/* proc data = { slot, file index }; slot: byte 0 in use, +4 sensor driver (+28 name), +12 chip id (u16),
 * +16 sensor subdev; subdev: +280 sensor info (+2 chip id, +24 -> +248 ...), +828.. values, +880 -> { name, addr } */
#define T32_SINFO_SLOT_DRV	4U
#define T32_SINFO_SLOT_CHIPID	12U
#define T32_SINFO_SLOT_SD	16U
#define T32_SINFO_SD_INFO	280U
#define T32_SINFO_SD_I2C	880U

/* the three frame rate files: num << 16 | den, printed as num / den (den 0 = 1); 0 = nothing */
static inline int t32_sinfo_fps(const struct t32_pshow_ops *os, void *m, const u8 *sd, u32 off)
{
	u32 v, den;

	if (!sd)
		return 0;
	v = t32_g_rd32(sd, off);
	if (!v)
		return 0;
	den = v & 0xffff;
	if (!den)
		den = 1;
	return os->seq_printf(m, "%u\n", (v >> 16) / den);
}

/* a "%d" word of the subdev, nothing when it is 0 */
static inline void t32_sinfo_nz(const struct t32_pshow_ops *os, void *m, const u8 *sd, u32 off)
{
	u32 v;

	if (!sd)
		return;
	v = t32_g_rd32(sd, off);
	if (v)
		os->seq_printf(m, "%d\n", v);
}

/* a "%d" word of the subdev, or the fallback text without a sensor */
static inline void t32_sinfo_or(const struct t32_pshow_ops *os, void *m, const u8 *sd, u32 off, const char *none)
{
	if (sd)
		os->seq_printf(m, "%d\n", t32_g_rd32(sd, off));
	else
		os->seq_printf(m, none);
}

static __maybe_unused int t32_sinfo_show(const struct t32_pshow_ops *os, void *m, void *lock)
{
	const u8 *pd = T32_PTR(t32_g_rd32(m, T32_PSHOW_SEQ_PRIV));
	const u8 *slot = T32_PTR(t32_g_rd32(pd, 0));
	const u8 *sd, *i2c = NULL, *info = NULL, *p;
	u32 v;

	os->mutex_lock(lock);
	if (!slot[0])
		goto out;
	sd = T32_PTR(t32_g_rd32(slot, T32_SINFO_SLOT_SD));
	if (sd) {
		i2c = T32_PTR(t32_g_rd32(sd, T32_SINFO_SD_I2C));
		info = T32_PTR(t32_g_rd32(sd, T32_SINFO_SD_INFO));
	}
	switch (t32_g_rd32(pd, 4)) {
	case 0:		/* name: the i2c device's, else the driver's */
		if (i2c && t32_g_rd32(i2c, 0)) {
			os->seq_printf(m, "%s\n", (const char *)T32_PTR(t32_g_rd32(i2c, 0)));
		} else {
			p = T32_PTR(t32_g_rd32(slot, T32_SINFO_SLOT_DRV));
			if (p)
				os->seq_printf(m, "%s\n", (const char *)T32_PTR(t32_g_rd32(p, 28)));
		}
		break;
	case 1:		/* i2c address */
		if (i2c)
			os->seq_printf(m, "0x%x\n", t32_g_rd32(i2c, 4));
		break;
	case 2:		/* chip id */
		os->seq_printf(m, "0x%x\n", info ? t32_g_rd16(info, 2) : t32_g_rd16(slot, T32_SINFO_SLOT_CHIPID));
		break;
	case 3:
		v = 0;
		if (info) {
			p = T32_PTR(t32_g_rd32(info, 24));
			if (p)
				v = t32_g_rd32(p, 248);
		}
		os->seq_printf(m, "%d\n", v);
		break;
	case 4:
		t32_sinfo_nz(os, m, sd, 828);
		break;
	case 5:
		t32_sinfo_nz(os, m, sd, 832);
		break;
	case 6:
		t32_sinfo_fps(os, m, sd, 896);
		break;
	case 7:
		os->seq_printf(m, "%s\n", sd ? "active" : "loaded");
		break;
	case 8:
		t32_sinfo_fps(os, m, sd, 900);
		break;
	case 9:
		t32_sinfo_fps(os, m, sd, 904);
		break;
	case 10:
		t32_sinfo_or(os, m, sd, 412, "1\n");
		break;
	case 11:
		t32_sinfo_or(os, m, sd, 416, "0\n");
		break;
	case 12:
		t32_sinfo_or(os, m, sd, 408, "0\n");
		break;
	case 13:
		t32_sinfo_or(os, m, sd, 384, "-1\n");
		break;
	case 14:
		t32_sinfo_or(os, m, sd, 388, "-1\n");
		break;
	default:
		break;
	}
out:
	os->mutex_unlock(lock);
	return 0;
}

/* ---- isp_vic_frd_show (0x183c) ---- */
#define T32_VICFRD_PRIV_DEV	280U
#define T32_VICFRD_CNT		904U	/* vic device: 4 counters per sensor, 16 bytes apart */
#define T32_VICFRD_SUM		1032U	/* vic device: error sum per sensor, 16 bytes apart */
#define T32_VICFRD_ERRS		13U	/* vic_err: 13 counters per sensor */

static __maybe_unused int t32_isp_vic_frd_show(const struct t32_pshow_ops *os, void *m, const u32 *vic_err)
{
	u32 priv = t32_g_rd32(m, T32_PSHOW_SEQ_PRIV), dev_w, i, j, sum;
	u8 *dev;
	const u32 *e;

	if (!priv || priv >= (u32)-4095)
		goto bad;
	dev_w = t32_g_rd32(T32_PTR(priv), T32_VICFRD_PRIV_DEV);
	if (!dev_w || dev_w >= (u32)-4095)
		goto bad;
	dev = T32_PTR(dev_w);
	os->seq_printf(m, "****************** VIC INFO **********************\n");
	for (i = 0; i < 4; i++) {
		u8 *c = dev + T32_VICFRD_CNT + 16 * i;

		e = vic_err + T32_VICFRD_ERRS * i;
		t32_g_wr32(dev, T32_VICFRD_SUM + 16 * i, 0);
		for (sum = 0, j = 0; j < T32_VICFRD_ERRS; j++)
			sum += e[j];
		t32_g_wr32(dev, T32_VICFRD_SUM + 16 * i, sum);
		os->seq_printf(m, "Sensor num: %d\n", i);
		os->seq_printf(m, " %d, %d, %d, %d\n", t32_g_rd32(c, 0), t32_g_rd32(c, 4), t32_g_rd32(c, 8),
			       t32_g_rd32(c, 12), sum);
		os->seq_printf(m, "%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n", e[0], e[1], e[2], e[3],
			       e[4], e[5], e[6], e[7], e[8], e[9], e[10], e[11], e[12]);
	}
	os->vic_debug_info(m);
	return 0;
bad:
	os->isp_printf(2, "The parameter is invalid!\n");
	return 0;
}

#endif
