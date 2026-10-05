/*
 * Calibration tables for the harness: the SDK default static/dynamic
 * tables, optionally overwritten from a <sensor>.bin tuning file exactly
 * like tx-isp-load-parameters.c does (day set active, night kept aside).
 * calib_switch_set() replays the day/night bank switch of
 * tx-isp-core-tuning.c through apical_api_calibration().
 */
#include <apical-isp/apical_calibrations_id.h>
#include "apical_command_api.h"
#include "rt.h"

extern uint32_t get_static_calibrations(ApicalCalibrations *);
extern uint32_t get_dynamic_calibrations(ApicalCalibrations *);

static ApicalCalibrations calib_tmp;
static LookupTable calib_bank[2][_CALIBRATION_TOTAL_SIZE];
static unsigned char calib_file[256 << 10];
static unsigned char calib_fw[128 << 10];
static int calib_have_bank;

static unsigned int calib_crc32(const unsigned int *p, unsigned int len)
{
	static const unsigned int t[8] = {
		0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
		0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
	};
	unsigned int crc = 0;
	while (len--) { crc ^= *p++; crc ^= t[crc & 7]; }
	return crc;
}

int calib_load(const char *path)
{
	long n, tot = 0;
	int fd, i;
	unsigned char *cur, *fw = calib_fw;
	unsigned int size, crc;

	get_dynamic_calibrations(&calib_tmp);
	get_static_calibrations(&calib_tmp);
	if (!path)
		return 0;
	fd = rt_open(path, 0);
	if (fd < 0) { rt_printf("calib: cannot open %s\n", path); return -1; }
	while ((n = rt_read(fd, calib_file + tot, sizeof(calib_file) - tot)) > 0) tot += n;
	rt_close(fd);
	if (strncmp((char *)calib_file, "1.38", 8) || strncmp((char *)calib_file + 8, "header0", 8)) {
		rt_printf("calib: bad version/header\n");
		return -1;
	}
	size = *(unsigned int *)(calib_file + 16);
	crc = *(unsigned int *)(calib_file + 20);
	cur = calib_file + 24;
	if (crc != calib_crc32((unsigned int *)cur, size / 4)) { rt_printf("calib: crc\n"); return -1; }
	for (i = 0; i < _CALIBRATION_TOTAL_SIZE; i++) {
		LookupTable *c = calib_tmp.calibrations[i], *t;
		unsigned int sz;
		if (!c || !c->ptr)
			continue;
		t = (LookupTable *)cur;
		calib_bank[0][i].ptr = cur + sizeof(LookupTable);
		calib_bank[0][i].rows = t->rows;
		calib_bank[0][i].cols = t->cols;
		calib_bank[0][i].width = t->width;
		sz = t->rows * t->cols * t->width;
		cur += sz + sizeof(LookupTable);
		c->ptr = fw;
		c->rows = t->rows; c->cols = t->cols; c->width = t->width;
		memcpy(fw, calib_bank[0][i].ptr, sz);
		fw += sz;
		t = (LookupTable *)cur;
		calib_bank[1][i].ptr = cur + sizeof(LookupTable);
		calib_bank[1][i].rows = t->rows;
		calib_bank[1][i].cols = t->cols;
		calib_bank[1][i].width = t->width;
		cur += sz + sizeof(LookupTable);
	}
	calib_have_bank = 1;
	rt_printf("calib: %s loaded, %u bytes of tables\n", path, (unsigned)(fw - calib_fw));
	return 0;
}

#define BANK(n) { CALIBRATION_##n, _CALIBRATION_##n }
static const struct { unsigned api, idx; } calib_bank_ids[] = {
	BANK(NP_LUT_MEAN), BANK(EVTOLUX_PROBABILITY_ENABLE), BANK(AE_EXPOSURE_AVG_COEF),
	BANK(IRIDIX_AVG_COEF), BANK(AF_MIN_TABLE), BANK(AF_MAX_TABLE),
	BANK(AF_WINDOW_RESIZE_TABLE), BANK(EXP_RATIO_TABLE), BANK(CCM_ONE_GAIN_THRESHOLD),
	BANK(FLASH_RG), BANK(FLASH_BG), BANK(AE_BALANCED_LINEAR), BANK(AE_BALANCED_WDR),
	BANK(AE_CORRECTION_FS_HDR), BANK(AE_CORRECTION_LINEAR), BANK(AE_EXPOSURE_CORRECTION),
	BANK(DEMOSAIC_LINEAR), BANK(DEMOSAIC_NP_OFFSET_FS_HDR), BANK(DEMOSAIC_NP_OFFSET_LINEAR),
	BANK(DP_SLOPE_FS_HDR), BANK(DP_SLOPE_LINEAR), BANK(DP_THRESHOLD_FS_HDR), BANK(DP_THRESHOLD_LINEAR),
	BANK(EVTOLUX_EV_LUT_FS_HDR), BANK(EVTOLUX_EV_LUT_LINEAR), BANK(EVTOLUX_LUX_LUT),
	BANK(IRIDIX_BLACK_PRC), BANK(IRIDIX_EV_LIM_FULL_STR), BANK(IRIDIX_EV_LIM_NO_STR_FS_HDR),
	BANK(IRIDIX_EV_LIM_NO_STR_LINEAR), BANK(IRIDIX_GAIN_MAX), BANK(IRIDIX_MIN_MAX_STR),
	BANK(IRIDIX_STRENGTH_MAXIMUM_LINEAR), BANK(IRIDIX_STRENGTH_MAXIMUM_WDR), BANK(IRIDIX_STRENGTH_TABLE),
	BANK(MESH_SHADING_STRENGTH), BANK(NOISE_PROFILE_LINEAR), BANK(RGB2YUV_CONVERSION),
	BANK(SATURATION_STRENGTH_LINEAR), BANK(SHARP_ALT_D_FS_HDR), BANK(SHARP_ALT_D_LINEAR),
	BANK(SHARP_ALT_UD_FS_HDR), BANK(SHARP_ALT_UD_LINEAR), BANK(SHARPEN_DS1_LINEAR),
	BANK(SHARPEN_DS1_WDR), BANK(SHARPEN_FR_LINEAR), BANK(SHARPEN_FR_WDR),
	BANK(SINTER_STRENGTH1_FS_HDR), BANK(SINTER_STRENGTH1_LINEAR), BANK(SINTER_STRENGTH_FS_HDR),
	BANK(SINTER_STRENGTH_LINEAR), BANK(SINTER_THRESH1_FS_HDR), BANK(SINTER_THRESH1_LINEAR),
	BANK(SINTER_THRESH4_FS_HDR), BANK(SINTER_THRESH4_LINEAR), BANK(STITCHING_ERROR_THRESH),
	BANK(TEMPER_STRENGTH),
};

void calib_switch_set(int night)
{
	unsigned i;
	if (!calib_have_bank)
		return;
	for (i = 0; i < sizeof(calib_bank_ids) / sizeof(calib_bank_ids[0]); i++) {
		LookupTable *t = &calib_bank[night][calib_bank_ids[i].idx];
		int32_t ret = 0;
		char ln[96];
		if (!t->ptr)
			continue;
		apical_api_calibration(calib_bank_ids[i].api, COMMAND_SET, t->ptr,
				       t->rows * t->cols * t->width, &ret);
		snprintf(ln, sizeof(ln), "CALSET %u %u -> %d\n", calib_bank_ids[i].api, night, ret);
		rt_puts(ln);
	}
}
