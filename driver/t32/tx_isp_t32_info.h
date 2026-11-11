/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32: the /proc/jz/isp/isp-m0 show routines of the vendor module (Matt Davis'
 * recovered C, PR #15) as readable C: isp_info_show (0xb5f0c), which prints
 * the "ISP INFO" frame and dispatches the selected items through
 * isp_debug_info, and the item printers isp_info_system (0xb4b48),
 * isp_info_base (0xb4790), isp_info_sensor (0xb4120), isp_info_ae (0xb36e8),
 * isp_info_awb (0xb35c0), isp_info_gib (0xb3498), isp_info_bin (0xb3f44) and
 * isp_info_err (0xb405c).
 *
 * The format strings and the argument order are the vendor's (the /proc text
 * is visible to user space).  All OS and tisp calls are hooks
 * (struct t32_info_ops); the seq_file is the vendor object (private word at
 * +72, the ISP device at private+280).  The stack buffers the tisp getters
 * fill have the vendor's sizes and offsets.
 *
 * Checked by tests/t32_oracle (t32_info1_oracle: system/gib/awb/bin/err,
 * t32_info2_oracle: ae, t32_info3_oracle: sensor/base, t32_info4_oracle:
 * show): the full seq_printf stream (format text and the formatted line),
 * the getter calls and their arguments, every byte the vendor writes, and the
 * return values, on random device images and random getter output.
 *
 * Vendor behaviour kept on purpose (vendor-equal, named here, not fixed):
 *  - isp_info_show / _err / _base / _system read the ISP device without a
 *    NULL check: a seq_file whose private word is 0 or an error pointer
 *    gives a NULL base, and the next read faults (issue #305: the vendor
 *    oopses in isp_info_base when tisp_get_tuning returns NULL, the read
 *    of tuning + ch is not checked either);
 *  - isp_info_show calls isp_debug_info with every set bit 8..31 of the item
 *    word; isp_debug_info only has 16 table entries (out-of-range read of
 *    the table for the bits 16..31);
 *  - isp_info_ae prints the EV and AT lists with a stride of two words
 *    (15 of 30 entries) and the exposure lists as the first entry 16 times;
 *    "AeHLCEn : disable" is printed with two unused arguments;
 *  - isp_info_base prints the flip mode of the same channel three times
 *    (tisp_g_hv_flip(ch) for "CH0".."CH2") and prints count + 1
 *    antiflicker steps of a 480-byte buffer: a count >= 120 reads past it;
 *  - isp_info_sensor divides by the frame rate numerator and by VTS without a
 *    check (MIPS divu by 0: the recovered C gives ~0; t32_info_divu);
 *  - isp_info_system walks the sensor list as long as the count word says
 *    (no bound).
 */
#ifndef TX_ISP_T32_INFO_H
#define TX_ISP_T32_INFO_H

#include "tx_isp_t32_glue.h"

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

struct t32_info_ops {
	int (*seq_printf)(void *m, const char *fmt, ...);	/* private_seq_printf */
	int (*gib_get_blc)(u32 ch, void *blc);			/* tisp_gib_api_get_blc */
	int (*awb_global_statis)(u32 ch, void *st);		/* tisp_g_awb_global_statis_attr */
	int (*awb_attr)(u32 ch, void *attr);			/* tisp_g_awb_attr */
	int (*ae_exprinfo)(u32 ch, void *info);			/* tisp_g_ae_exprinfo_attr */
	int (*ae_scence)(u32 ch, void *attr);			/* tisp_g_ae_scence_attr */
	int (*ae_bv)(u8 ch, void *bv);				/* tisp_g_bv */
	int (*ae_at_list)(u32 ch, void *list);			/* tisp_g_at_list */
	int (*ae_ev_list)(u32 ch, void *list);			/* tisp_g_ev_list */
	int (*ae_explist)(u32 ch, void *list);			/* tisp_g_ae_explist */
	int (*ae_stable)(u8 ch, void *st);			/* tisp_get_ae_stable */
	int (*bin_path)(u8 ch, void *bin);			/* lib_tisp_get_bin_path */
	int (*day_or_night)(u8 ch);				/* tisp_day_or_night_g_ctrl */
	u8 *(*get_tuning)(void);				/* tisp_get_tuning */
	int (*hv_flip)(u32 ch, void *flip);			/* tisp_g_hv_flip */
	int (*antiflick)(u32 ch, void *af);			/* tisp_g_antiflick */
	int (*antiflicker_step)(u32 ch, void *steps, u16 *n);	/* tisp_get_antiflicker_step */
	int (*msca_osd_info)(u8 ch);				/* tisp_get_msca_osd_info */
	u32 (*isp_memopt)(void);				/* get_isp_memopt */
	int (*isp_cache_line)(void);				/* get_isp_cache_line */
	int (*debug_info)(u32 item, void *m, u32 ch);		/* isp_debug_info */
	long (*wait_completion_timeout)(void *done, u32 ms);	/* private_wait_for_completion_timeout */
};

#define T32_INFO_SEQ_PRIV	72U	/* seq_file: private (the proc data) */
#define T32_INFO_PRIV_ISP	280U	/* proc data: ISP device */
#define T32_INFO_SNS_BASE	324U	/* ISP device: sensor info [4] of 104 bytes */
#define T32_INFO_SNS_BYTES	104U
#define T32_INFO_SNS_NAME	376U	/* ISP device: first sensor's attribute pointer (sensor info +52) */
#define T32_INFO_VIN_STATE	300U	/* ISP device: state word per channel, >= 4 = running */
#define T32_INFO_DBG_DONE	848U	/* ISP device: debug done word [ch * 3 + 0..2] */
#define T32_INFO_ERR_DEBUG	896U	/* ISP device: error counters [4] each */
#define T32_INFO_ERR_OVERFLOW	912U
#define T32_INFO_ERR_BREAKFRM	928U
#define T32_INFO_FRM_STATE	945U	/* u8, 4 = frame timing valid */
#define T32_INFO_BYPASS		1148U	/* [ch] */
#define T32_INFO_WDR_EN		1180U	/* [ch] */
#define T32_INFO_SNS_NUM	1196U
#define T32_INFO_CACHE_MODE	1200U
#define T32_INFO_CACHE_LINE	1204U
#define T32_INFO_DBG_TYPE	744U	/* isp_info_show: debug type, items, interval, vinum */
#define T32_INFO_DBG_ITEMS	748U
#define T32_INFO_DBG_INTERVAL	752U
#define T32_INFO_DBG_VINUM	756U
#define T32_INFO_DBG_REQ	1048U	/* [ch]: debug request word */
#define T32_INFO_DBG_DONE_CMP	1064U	/* [ch] of 12 bytes: completion */

/* the ISP device of a seq_file: 0 when the private word is 0 or an error pointer */
static inline u8 *t32_info_isp(const u8 *m)
{
	u32 priv = t32_g_rd32(m, T32_INFO_SEQ_PRIV);

	if (!priv || priv >= (u32)-4095)
		return NULL;
	return T32_PTR(t32_g_rd32(T32_PTR(priv), T32_INFO_PRIV_ISP));
}

/* MIPS divu of the recovered C: a divide by 0 gives ~0 */
static inline u32 t32_info_divu(u32 a, u32 b)
{
	return b ? a / b : ~0U;
}

/* ---- isp_info_gib (0xb3498) ---- */
static __maybe_unused int t32_isp_info_gib(const struct t32_info_ops *os, void *m, u32 ch)
{
	u16 blc[12];	/* tisp_gib_api_get_blc: [1..5] long R/GR/GB/B/IR, [7..11] short */

	os->seq_printf(m, "********* GIB Info *********\n");
	os->gib_get_blc(ch, blc);
	os->seq_printf(m, "GIB R  : %d\n", blc[1]);
	os->seq_printf(m, "GIB GR : %d\n", blc[2]);
	os->seq_printf(m, "GIB GB : %d\n", blc[3]);
	os->seq_printf(m, "GIB B  : %d\n", blc[4]);
	os->seq_printf(m, "GIB IR : %d\n", blc[5]);
	os->seq_printf(m, "GIB Short R  : %d\n", blc[7]);
	os->seq_printf(m, "GIB Short GR : %d\n", blc[8]);
	os->seq_printf(m, "GIB Short GB : %d\n", blc[9]);
	os->seq_printf(m, "GIB Short B  : %d\n", blc[10]);
	os->seq_printf(m, "GIB Short IR : %d\n", blc[11]);
	return 0;
}

/* ---- isp_info_awb (0xb35c0) ---- */
static __maybe_unused int t32_isp_info_awb(const struct t32_info_ops *os, void *m, u32 ch)
{
	u32 attr[19];	/* tisp_g_awb_attr: [1]/[2] weighted R/B gain, [4] CT, [17]/[18] start R/B gain */
	u32 st[5];	/* tisp_g_awb_global_statis_attr: [0]/[1] weighted R/G B/G, [2]/[3] global */

	os->seq_printf(m, "********* AWB Info *********\n");
	os->awb_global_statis(ch, st);
	os->awb_attr(ch, attr);
	os->seq_printf(m, "AWBGlobal R/G : %d\n", st[2]);
	os->seq_printf(m, "AWBGlobal B/G : %d\n", st[3]);
	os->seq_printf(m, "AWBWeighted R/G : %d\n", st[0]);
	os->seq_printf(m, "AWBWeighted B/G : %d\n", st[1]);
	os->seq_printf(m, "AWBWeightedRgain : %d\n", attr[1]);
	os->seq_printf(m, "AWBWeightedBgain : %d\n", attr[2]);
	os->seq_printf(m, "AWBStartRgain : %d\n", attr[17]);
	os->seq_printf(m, "AWBStartBgain : %d\n", attr[18]);
	os->seq_printf(m, "AWBColorTemperature : %d\n", attr[4]);
	return 0;
}

/* ---- isp_info_ae (0xb36e8) ---- */
/* tisp_g_ae_exprinfo_attr: 55 words, printed in order */
static const char *const t32_info_ae_expr_fmt[55] = {
	"AeIntegrationTimeUnit : %d\n", "AeMode : %d\n", "AeIntegrationTimeMode : %d\n",
	"AeAGainManualMode : %d\n", "AeDGainManualMode : %d\n", "AeIspDGainManualMode : %d\n",
	"AeIntegrationTime : %d\n", "AeAGain : %d\n", "AeDGain : %d\n", "AeIspDGain : %d\n",
	"AeMinIntegrationTimeMode : %d\n", "AeMinAGainMode : %d\n", "AeMinDgainMode : %d\n",
	"AeMinIspDGainMode : %d\n", "AeMaxIntegrationTimeMode : %d\n", "AeMaxAGainMode : %d\n",
	"AeMaxDgainMode : %d\n", "AeMaxIspDGainMode : %d\n", "AeMinIntegrationTime : %d\n",
	"AeMinAGain : %d\n", "AeMinDgain : %d\n", "AeMinIspDGain : %d\n",
	"AeMaxIntegrationTime : %d\n", "AeMaxAGain : %d\n", "AeMaxDgain : %d\n",
	"AeMaxIspDGain : %d\n", "AeShortMode : %d\n", "AeShortIntegrationTimeMode : %d\n",
	"AeShortAGainManualMode : %d\n", "AeShortDGainManualMode : %d\n",
	"AeShortIspDGainManualMode : %d\n", "AeShortIntegrationTime : %d\n", "AeShortAGain : %d\n",
	"AeShortDGain : %d\n", "AeShortIspDGain : %d\n", "AeShortMinIntegrationTimeMode : %d\n",
	"AeShortMinAGainMode : %d\n", "AeShortMinDgainMode : %d\n", "AeShortMinIspDGainMode : %d\n",
	"AeShortMaxIntegrationTimeMode : %d\n", "AeShortMaxAGainMode : %d\n",
	"AeShortMaxDgainMode : %d\n", "AeShortMaxIspDGainMode : %d\n",
	"AeShortMinIntegrationTime : %d\n", "AeShortMinAGain : %d\n", "AeShortMinDgain : %d\n",
	"AeShortMinIspDGain : %d\n", "AeShortMaxIntegrationTime : %d\n", "AeShortMaxAGain : %d\n",
	"AeShortMaxDgain : %d\n", "AeShortMaxIspDGain : %d\n", "TotalGainDb : %d\n",
	"TotalGainDbShort : %d\n", "ExposureValue : %d\n", "EVLog2 : %d\n",
};

/* vendor stack frame of isp_info_ae: the getter buffers in vendor order */
struct t32_info_ae_buf {
	u32 explist[81];	/* tisp_g_ae_explist: [0] enable, [2]/[3]/[4] us/again/dgain (first entries) */
	u32 expr[55];		/* tisp_g_ae_exprinfo_attr */
	u32 ev[32];		/* tisp_g_ev_list: [0] enable, [2..31] list */
	u32 at[32];		/* tisp_g_at_list: [0] enable, [2..31] list */
	u32 scene[10];		/* tisp_g_ae_scence_attr: [0] HLC mode, byte 4 HLC strength, [2] BLC mode,
				 * byte 12 BLC strength, [4]/[5] target comp/strength, [6]/[7] start EV,
				 * [8] luma */
	u32 stable[3];		/* tisp_get_ae_stable: stable, target, mean */
	u32 bv;			/* tisp_g_bv */
};

/* " %d" of every second list word (vendor stride 8 bytes over 120 bytes), or "Disable", and "\n" */
static inline void t32_info_ae_list(const struct t32_info_ops *os, void *m, const char *name, const u32 *list)
{
	u32 i;

	os->seq_printf(m, name);
	if (list[0] == 0) {
		os->seq_printf(m, "Disable");
	} else {
		for (i = 0; i < 30; i += 2)
			os->seq_printf(m, " %d", list[2 + i], list[3 + i]);
	}
	os->seq_printf(m, "\n");
}

/* one exposure list line: the first entry 16 times (vendor), then "\n" */
static inline void t32_info_ae_expo(const struct t32_info_ops *os, void *m, const char *name, u32 v)
{
	u32 i;

	os->seq_printf(m, name);
	for (i = 0; i < 16; i++)
		os->seq_printf(m, " %d", v);
}

static __maybe_unused int t32_isp_info_ae(const struct t32_info_ops *os, void *m, u32 ch)
{
	struct t32_info_ae_buf b;
	const u8 *scene = (const u8 *)b.scene;
	u32 i;

	os->seq_printf(m, "********* Ae Info *********\n");
	os->ae_exprinfo(ch, b.expr);
	for (i = 0; i < 55; i++)
		os->seq_printf(m, t32_info_ae_expr_fmt[i], b.expr[i]);
	os->ae_scence(ch, b.scene);
	os->ae_bv((u8)ch, &b.bv);
	os->ae_at_list(ch, b.at);
	os->ae_ev_list(ch, b.ev);
	os->ae_explist(ch, b.explist);
	os->ae_stable((u8)ch, b.stable);
	os->seq_printf(m, "AeStable : %d\n", b.stable[0]);
	os->seq_printf(m, "AeTarget : %d\n", b.stable[1]);
	os->seq_printf(m, "AeMean : %d\n", b.stable[2]);
	os->seq_printf(m, "AeLuma : %d\n", b.scene[8]);

	/* HLC: strength byte 4 with mode word 0 == 3; BLC: strength byte 12 with mode word 2 == 3 */
	if (!scene[4] && !scene[12]) {
		os->seq_printf(m, "AeHLCEn : disable\n");
		os->seq_printf(m, "AeBLCEn : disable\n");
	} else if (b.scene[0] == 3 && !scene[12]) {
		os->seq_printf(m, "AeHLCEn : enable\n");
		os->seq_printf(m, "AeHLCStrength : %d\n", scene[4]);
		os->seq_printf(m, "AeBLCEn : disable\n");
	} else if (b.scene[2] == 3 && !scene[4]) {
		os->seq_printf(m, "AeHLCEn : disable\n", b.scene[4], b.scene[5]);
		os->seq_printf(m, "AeBLCEn : enable\n");
		os->seq_printf(m, "AeBLCStrength : %d\n", scene[12]);
	}
	os->seq_printf(m, "AeTargetComp : %d, Strength : %d\n", b.scene[4], b.scene[5]);
	os->seq_printf(m, "AeStartEV\xe3\x80\x81" "EvValue : %d\n", b.scene[6], b.scene[7]);
	os->seq_printf(m, "AeBV : %d\n", b.bv);
	t32_info_ae_list(os, m, "AeEvList : ", b.ev);
	t32_info_ae_list(os, m, "AeAtList : ", b.at);
	if (b.explist[0] == 0) {
		os->seq_printf(m, "AeExpoList : Disable");
		return 0;
	}
	t32_info_ae_expo(os, m, "AeExpoList - us :", b.explist[2]);
	os->seq_printf(m, "\n");
	t32_info_ae_expo(os, m, "AeExpoList - again :", b.explist[3]);
	os->seq_printf(m, "\n");
	t32_info_ae_expo(os, m, "AeExpoList - dgain :", b.explist[4]);
	os->seq_printf(m, "\n");
	return 0;
}

/* ---- isp_info_bin (0xb3f44) ---- */
struct t32_info_bin {		/* lib_tisp_get_bin_path */
	u32 mode;
	char running[128];
	char path[4][128];	/* linear day / night, wdr day / night (the last one is 384 bytes in the frame) */
	char pad[256];
	u8 status[4];
};

static __maybe_unused int t32_isp_info_bin(const struct t32_info_ops *os, void *m, u32 ch)
{
	static const char *const fmt[4] = {
		"linear day   : status = %d , path = %s\n",
		"linear night : status = %d , path = %s\n",
		"wdr day      : status = %d , path = %s\n",
		"wdr night    : status = %d , path = %s\n",
	};
	struct t32_info_bin b;
	u32 i;

	os->seq_printf(m, "********* ISP bin Info *********\n");
	os->bin_path((u8)ch, &b);
	os->seq_printf(m, "bin mode:%d\n", b.mode);
	os->seq_printf(m, "running bin path:%s\n", b.running);
	os->seq_printf(m, "********* default bin *********\n");
	for (i = 0; i < 4; i++)
		if (b.status[i])
			os->seq_printf(m, fmt[i], b.status[i], b.path[i]);
	return 0;
}

/* ---- isp_info_err (0xb405c) ---- */
static __maybe_unused int t32_isp_info_err(const struct t32_info_ops *os, void *m, u32 ch)
{
	const u8 *e = t32_info_isp(m) + 4 * ch;
	u32 v = t32_g_rd32(e, T32_INFO_ERR_DEBUG);

	if (!v)
		return 0;
	os->seq_printf(m, "isp%d debug : %d\n", ch, v);
	v = t32_g_rd32(e, T32_INFO_ERR_OVERFLOW);
	if (v)
		os->seq_printf(m, "isp%d debug overflow : %d\n", ch, v);
	v = t32_g_rd32(e, T32_INFO_ERR_BREAKFRM);
	if (v)
		os->seq_printf(m, "isp%d debug breakfrm : %d\n", ch, v);
	return 0;
}

/* ---- isp_info_sensor (0xb4120) ---- */
/*
 * media bus code -> name, the vendor's switch: YUV 0x5000..3 (1X16) and 0x5100..3 (2X8), Bayer
 * 0x5200..0x520b (BGGR GBRG GRBG RGGB repeating), RGB-IR 0x5300..0x532f (three blocks of 16 with the
 * same names), everything else "The format of isp input is RGB or others!".
 */
static inline const char *t32_info_bayer_name(u32 code)
{
	static const char *const yuv16[4] = { "UYVY8_1X16", "VYUY8_1X16", "YUYV8_1X16", "YVYU8_1X16" };
	static const char *const yuv8[4] = { "UYVY8_2X8", "VYUY8_2X8", "YUYV8_2X8", "YVYU8_2X8" };
	static const char *const bayer[4] = { "BGGR", "GBRG", "GRBG", "RGGB" };
	static const char *const rgbir[16] = {
		"RGGB", "RGGB", "RGGB", "BIGR", "GRBI", "GBRI", "IRBG", "IBRG",
		"RGGI", "BGGI", "GRIG", "GBIG", "GIRG", "SGIBG", "IGGR", "IGGB",
	};

	if (code >= 0x5000 && code <= 0x5003)
		return yuv16[code - 0x5000];
	if (code >= 0x5100 && code <= 0x5103)
		return yuv8[code - 0x5100];
	if (code >= 0x5200 && code <= 0x520b)
		return bayer[(code - 0x5200) & 3];
	if (code >= 0x5300 && code <= 0x532f)
		return rgbir[(code - 0x5300) & 15];
	return "The format of isp input is RGB or others!";
}

#define T32_INFO_SNS_W		0U	/* sensor info: width, height, mbus code, attr, ops, fps */
#define T32_INFO_SNS_H		4U
#define T32_INFO_SNS_CODE	8U
#define T32_INFO_SNS_ATTR	52U	/* -> +0 name, +256 expo fs, +276 HTS (u16), +278 VTS (u16) */
#define T32_INFO_SNS_PRIV	56U	/* -> +104 DFBOOT */
#define T32_INFO_SNS_FPS	68U	/* num << 16 | den */

static __maybe_unused int t32_isp_info_sensor(const struct t32_info_ops *os, void *m, u32 ch)
{
	u32 priv = t32_g_rd32(m, T32_INFO_SEQ_PRIV), isp_w = 0, s_w = 0;
	const u8 *isp, *s, *attr;
	u32 fps, vts;

	/* unlike the other printers this one checks the proc data (not the ISP device) */
	if (priv && priv < (u32)-4095) {
		isp_w = t32_g_rd32(T32_PTR(priv), T32_INFO_PRIV_ISP);
		s_w = isp_w + T32_INFO_SNS_BASE + T32_INFO_SNS_BYTES * ch;
	}
	isp = T32_PTR(isp_w);
	s = T32_PTR(s_w);
	os->seq_printf(m, "********* Sensor Info *********\n");
	if (!s_w) {
		os->seq_printf(m, "This Vin( %d ) Don't Wrok\n", ch);
		return -1;
	}
	if ((int)t32_g_rd32(isp, (ch + 74) * 4 + 4) < 4) {
		os->seq_printf(m, "This ISP( %d ) Don't Wrok\n", ch);
		return -1;
	}
	attr = T32_PTR(t32_g_rd32(s, T32_INFO_SNS_ATTR));
	fps = t32_g_rd32(s, T32_INFO_SNS_FPS);
	os->seq_printf(m, "Sensor Name : %s\n", (const char *)T32_PTR(t32_g_rd32(attr, 0)));
	os->seq_printf(m, "Sensor OUTPUT Width : %d\n", t32_g_rd32(s, T32_INFO_SNS_W));
	os->seq_printf(m, "Sensor OUTPUT Height : %d\n", t32_g_rd32(s, T32_INFO_SNS_H));
	os->seq_printf(m, "Sensor OUTPUT FPS : %d / %d\n", fps >> 16, fps & 0xffff);
	os->seq_printf(m, "Sensor OUTPUT Bayer : %s\n", t32_info_bayer_name(t32_g_rd32(s, T32_INFO_SNS_CODE)));
	os->seq_printf(m, "Sensor DFBOOT : %d\n", t32_g_rd32(T32_PTR(t32_g_rd32(s, T32_INFO_SNS_PRIV)), 104));
	os->seq_printf(m, "Sensor HTS : %d\n", t32_g_rd16(attr, 276));
	os->seq_printf(m, "Sensor VTS : %d\n", t32_g_rd16(attr, 278));
	attr = T32_PTR(t32_g_rd32(s, T32_INFO_SNS_ATTR));
	vts = t32_g_rd16(attr, 278);
	os->seq_printf(m, "Sensor one line expo time : %dus\n",
		       t32_info_divu(t32_info_divu(1000000, fps >> 16) * (fps & 0xffff), vts));
	os->seq_printf(m, "Expo Fs : %d\n", t32_g_rd32(T32_PTR(t32_g_rd32(s, T32_INFO_SNS_ATTR)), 256));
	return 0;
}

/* ---- isp_info_base (0xb4790) ---- */
static __maybe_unused int t32_isp_info_base(const struct t32_info_ops *os, void *m, u32 ch)
{
	u8 *isp = t32_info_isp(m);
	const u8 *tun;
	u8 buf[960];		/* tisp_g_hv_flip, then (cleared) tisp_get_antiflicker_step */
	u32 af[2];		/* tisp_g_antiflick: mode word, freq byte */
	u16 nsteps = 0;
	const char *s;
	u32 i, freq;
	int r;

	os->seq_printf(m, "**************** Base Info ****************\n", ch);
	if ((int)t32_g_rd32(isp, 4 * ch + T32_INFO_VIN_STATE) < 4) {
		os->seq_printf(m, "This ISP( %d ) Don't Wrok\n", ch);
		return -1;
	}
	r = os->day_or_night((u8)ch);
	os->seq_printf(m, "ISP Runing Mode : %s\n", r == 0 ? "day" : r == 1 ? "night" : "custom");
	tun = os->get_tuning() + ch;
	os->seq_printf(m, "Saturation : %d\n", tun[0]);
	os->seq_printf(m, "Sharpness : %d\n", tun[8]);
	os->seq_printf(m, "Contrast : %d\n", tun[4]);
	os->seq_printf(m, "Brightness : %d\n", tun[12]);
	os->seq_printf(m, "Hue : %d\n", tun[16]);
	os->seq_printf(m, "Wdr En : %d\n", t32_g_rd32(isp, 4 * ch + T32_INFO_WDR_EN));
	os->seq_printf(m, "Bypass : %d\n", t32_g_rd32(isp, 4 * ch + T32_INFO_BYPASS));
	for (i = 0; i < 3; i++) {
		os->hv_flip(ch, buf);
		if ((buf[0] & 0x14) == 0x14)
			s = "mirror and flip";
		else if (buf[0] & 0x04)
			s = "mirror";
		else if (buf[0] & 0x10)
			s = "flip";
		else
			s = "nomal";
		os->seq_printf(m, "ISP FLIP MODE CH%d : %s\n", i, s);
	}
	os->antiflick(ch, af);
	freq = ((const u8 *)af)[4];
	if (af[0] == 0) {
		s = "disable";
		freq = 0;
	} else if (af[0] == 2) {
		s = "auto";
	} else {
		s = "normal";
	}
	os->seq_printf(m, "Antiflicker : %s freq: %d\n", s, freq);
	memset(buf, 0, 480);
	if (os->antiflicker_step(ch, buf, &nsteps) == 0) {
		os->seq_printf(m, "Antiflicker nodes: %d: step : ", nsteps + 1);
		for (i = 0; (int)i <= (int)nsteps; i++)
			os->seq_printf(m, "%d. ", t32_g_rd32(buf, 4 * i));
		os->seq_printf(m, "\n");
	}
	for (i = 0; i < 3; i++)
		os->seq_printf(m, "ISP debug CH%d Done: %d\n", i,
			       t32_g_rd32(isp, 12 * ch + T32_INFO_DBG_DONE + 4 * i));
	if (os->msca_osd_info((u8)ch))
		os->seq_printf(m, "ISP OSD DEBUG: %d\n", os->msca_osd_info((u8)ch));
	return 0;
}

/* ---- isp_info_system (0xb4b48) ---- */
static __maybe_unused int t32_isp_info_system(const struct t32_info_ops *os, void *m, u32 ch)
{
	const u8 *isp = t32_info_isp(m);
	u32 i, t_end, t_start, t_valid, us;
	int cl;

	os->seq_printf(m, "**************** ISP Info ****************\n", ch);
	os->seq_printf(m, "Software Version : %s\n", "H20260618a");
	os->seq_printf(m, "ISP Memopt: 0x%08x\n", os->isp_memopt());
	cl = os->isp_cache_line();
	if (cl)
		os->seq_printf(m, "ISP Cache Line: %d\n", os->isp_cache_line());
	else if (t32_g_rd32(isp, T32_INFO_CACHE_MODE) == 1)
		os->seq_printf(m, "ISP Cache Line: %d\n", t32_g_rd32(isp, T32_INFO_CACHE_LINE));
	os->seq_printf(m, "Sensor Number : %d\n", t32_g_rd32(isp, T32_INFO_SNS_NUM));
	for (i = 0; i < t32_g_rd32(isp, T32_INFO_SNS_NUM); i++)
		os->seq_printf(m, "Sensor%d Name : %s\n", i, (const char *)T32_PTR(t32_g_rd32(
			       T32_PTR(t32_g_rd32(isp, T32_INFO_SNS_NAME + T32_INFO_SNS_BYTES * i)), 0)));
	if (isp[T32_INFO_FRM_STATE] != 4)
		return 0;
	/* frame timing: seconds / microseconds pairs (wrapping 32-bit microseconds as the vendor) */
	us = 1000000;
	t_start = t32_g_rd32(isp, 952) * us + t32_g_rd32(isp, 956);
	t_valid = t32_g_rd32(isp, 984) * us + t32_g_rd32(isp, 988);
	t_end = t32_g_rd32(isp, 1016) * us + t32_g_rd32(isp, 1020);
	os->seq_printf(m, "ISP ==>> frame_interival:%ldus, valid:%ldus, vb:%ldus\n",
		       (long)(t_end - t_start), (long)(t_valid - t_start), (long)(t_end - t_valid));
	return 0;
}

/* ---- isp_info_show (0xb5f0c) ---- */
static __maybe_unused int t32_isp_info_show(const struct t32_info_ops *os, void *m)
{
	u8 *isp = t32_info_isp(m);
	u32 vin = t32_g_rd32(isp, T32_INFO_DBG_VINUM), ch, bit;
	int r;

	if (vin >= 4) {
		os->seq_printf(m, "vinum overflow: %d\n", vin);
		return 0;
	}
	if ((int)t32_g_rd32(isp, T32_INFO_VIN_STATE + 4 * vin) < 4) {
		os->seq_printf(m, "This ISP( %d ) Don't Wrok\n", vin);
		return 0;
	}
	if (t32_g_rd32(isp, T32_INFO_DBG_TYPE) - 1 < 2) {
		t32_g_wr32(isp, T32_INFO_DBG_REQ + 4 * vin, 0x80000000U);
		if (!os->wait_completion_timeout(isp + T32_INFO_DBG_DONE_CMP + 12 * vin, 2000))
			os->seq_printf(m, "wait timeout ...\n");
	}
	os->seq_printf(m, "********************* ISP INFO Start *********************\n");
	if (t32_g_rd32(isp, T32_INFO_DBG_ITEMS) & 1) {
		r = os->debug_info(0, m, 0);
		if (r)
			return r;
	}
	ch = t32_g_rd32(isp, T32_INFO_DBG_VINUM);
	os->seq_printf(m, "**************** ISP( %d ) ****************\n", ch);
	if ((int)t32_g_rd32(isp, (ch + 74) * 4 + 4) < 4) {
		os->seq_printf(m, "This ISP( %d ) Don't Wrok\n", ch);
	} else {
		/* the item word is read again before every test (isp_debug_info may change it) */
		if (t32_g_rd32(isp, T32_INFO_DBG_ITEMS) & 2)
			os->debug_info(1, m, ch);
		if (t32_g_rd32(isp, T32_INFO_DBG_ITEMS) & 4)
			os->debug_info(2, m, ch);
		for (bit = 8; bit < 32; bit++)
			if (t32_g_rd32(isp, T32_INFO_DBG_ITEMS) & (1U << bit))
				os->debug_info(bit, m, ch);
		if (t32_g_rd32(isp, T32_INFO_DBG_ITEMS) & 0x80)
			os->debug_info(7, m, ch);
		if (t32_g_rd32(isp, T32_INFO_DBG_ITEMS) & 8)
			os->debug_info(3, m, ch);
	}
	os->seq_printf(m, "********* Debug Parameters *********\n");
	os->seq_printf(m, "Vinum:%d\n", t32_g_rd32(isp, T32_INFO_DBG_VINUM));
	os->seq_printf(m, "Debug Type:%d\n", t32_g_rd32(isp, T32_INFO_DBG_TYPE));
	os->seq_printf(m, "Items:0x%08x\n", t32_g_rd32(isp, T32_INFO_DBG_ITEMS));
	os->seq_printf(m, "Interval:%d\n", t32_g_rd32(isp, T32_INFO_DBG_INTERVAL));
	os->seq_printf(m, "********************* ISP INFO End *********************\n");
	return 0;
}

#endif
