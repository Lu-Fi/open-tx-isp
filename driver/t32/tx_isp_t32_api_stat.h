/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 statistics configuration setter: readable C of the vendor's
 * tisp_s_statis_config_attr.
 *
 * buf (72 B) = three enable-led blocks, kept per channel in the attribute
 * block (*t32_data_f0904 + ch * 72 + 1572, same layout; a disabled block
 * keeps only its enable word 0 there):
 *   +0  AE   { u32 en, .., u32 hist bins at +20 }      (24 B)
 *   +24 AWB  { u32 en, location from +28 }             (20 B)
 *   +44 AF   { u32 en, u32 +48, u32 +52, u8 +56, +57, u8 +60, u16 +62 .. +70 } (28 B)
 * Enabled blocks go to the statistics code: AE hist bins (and the GSM, which
 * gets a 1052 B scratch buffer of which only the bins at +1044 are set),
 * AWB location, and AF as a 24 B attribute block of which the vendor fills
 * +8 .. +23 (the rest is uninitialised stack in the vendor, 0 here).
 * The vendor does not check the scratch allocation (a NULL from vmalloc is
 * written to): ours mirrors that.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_apistat_oracle).
 */
#ifndef TX_ISP_T32_API_STAT_H
#define TX_ISP_T32_API_STAT_H

#include "tx_isp_t32_tp.h"

struct t32_api_stat_ops {
	void *(*vmalloc)(u32 size);
	void (*vfree)(void *p);
	int (*ae_set_hist_bin)(u32 ch, const u8 *bins);
	int (*gsm_api_set)(u32 ch, const u8 *scratch);
	int (*awb_set_statis_location)(u32 ch, const u8 *loc);
	int (*af_api_set_attr)(u32 ch, const u8 *af);
};

#define T32_STAT_ATTR		1572	/* + ch * 72 in the attribute block */
#define T32_STAT_GSM_SCRATCH	1052
#define T32_STAT_GSM_BINS	1044

static inline u32 t32_as_rd32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline void t32_as_cpy(u8 *d, const u8 *s, unsigned int n)
{
	while (n--)
		*d++ = *s++;
}

static inline void t32_as_zero32(u8 *p)
{
	p[0] = p[1] = p[2] = p[3] = 0;
}

/* tisp_s_statis_config_attr */
static inline int t32_s_statis_config_attr(const struct t32_api_stat_ops *op, u8 *attr, u32 ch, const u8 *buf)
{
	u8 *a = attr + ch * 72 + T32_STAT_ATTR;
	u8 *scratch = op->vmalloc(T32_STAT_GSM_SCRATCH);

	if (t32_as_rd32(buf) == 1) {
		t32_as_cpy(a, buf, 24);
		op->ae_set_hist_bin(ch, buf + 20);
		t32_as_cpy(scratch + T32_STAT_GSM_BINS, buf + 20, 4);
		op->gsm_api_set(ch, scratch);
	} else {
		t32_as_zero32(a);
	}
	if (t32_as_rd32(buf + 24) == 1) {
		t32_as_cpy(a + 24, buf + 24, 20);
		op->awb_set_statis_location(ch, buf + 28);
	} else {
		t32_as_zero32(a + 24);
	}
	if (t32_as_rd32(buf + 44) == 1) {
		u8 af[24] = { 0 };

		t32_as_cpy(a + 44, buf + 44, 28);
		af[8] = buf[44];
		af[9] = buf[60];
		t32_as_cpy(af + 10, buf + 62, 8);	/* u16 +62, +64, +66, +68 */
		af[18] = buf[48];
		af[19] = buf[52];
		af[20] = buf[56];
		af[21] = buf[57];
		t32_as_cpy(af + 22, buf + 70, 2);
		op->af_api_set_attr(ch, af);
	} else {
		t32_as_zero32(a + 44);
	}
	op->vfree(scratch);
	return 0;
}

#endif
