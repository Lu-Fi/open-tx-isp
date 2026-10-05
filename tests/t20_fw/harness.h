#ifndef T20FW_HARNESS_H
#define T20FW_HARNESS_H
#include <stdint.h>
typedef int bool;

/* firmware unit (tx_isp_t20_firmware.c via fw_unit.c) */
int32_t apical_init(void);
int32_t apical_process(void);
int32_t apical_cmd_process(void);
int32_t apical_command(uint32_t cmd, uint32_t sub, uint32_t val, uint32_t type, uint32_t *data);
int32_t apical_api_calibration(uint32_t id, uint32_t dir, void *buf, uint32_t size, int32_t *ret);
int32_t apical_sbus_i2c_init(void *sbus);
void t20fw_set_knobs(int oem, int trace);
void t20fw_awb_gains(uint32_t *g);
extern unsigned char stab[60];

/* harness */
void *harness_stack_lo(void);
void *harness_stack_hi(void);
char *strstr(const char *h, const char *n);
void *memmove(void *d, const void *s, unsigned n);
#define strstr_h strstr
#define memmove_h memmove
static inline uint64_t math_exp2_host(int32_t x)
{
	/* 2^(x/65536) in Q16, piecewise linear in the fraction: harness only */
	uint32_t ip = (uint32_t)x >> 16, fr = (uint32_t)x & 0xffff;
	uint64_t m = 0x10000u + fr;
	return ip >= 40 ? (uint64_t)-1 : (m << ip);
}

/* calib.c */
int calib_load(const char *path);
void calib_switch_set(int night);

/* apical_command_api.h values (kept local to avoid the SDK header chain) */
#define TSYSTEM_H 1
#define TALGORITHMS_H 3
#define SYSTEM_EXPOSURE_DARK_TARGET_H 0x1e
#define SYSTEM_MAX_SENSOR_ANALOG_GAIN_H 0x25
#define AE_MODE_ID_H 0x56
#define AE_COMPENSATION_ID_H 0x5b
#define AWB_MODE_ID_H 0x60
#define AWB_AUTO_H 0x32
#define AWB_MANUAL_H 0x33
#define SYSTEM_AWB_RED_GAIN_H 0x39
#define SYSTEM_AWB_BLUE_GAIN_H 0x3a
#define ANTIFLICKER_MODE_ID_H 0x68
#endif
