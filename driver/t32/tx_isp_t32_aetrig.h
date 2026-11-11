/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 AE / AWB small entry points: readable C of the vendor's
 * tisp_ae_par_sensor_trig, tisp_ae_long_set_sensor_integration_time,
 * tisp_ae_short_set_sensor_integration_time, tisp_ae_algo_waitdone,
 * tisp_ae_algorithm and tisp_awb_algo_handle.
 *
 * Vendor objects per channel: t32_data_e1514 (u8 "sensor trigger" flag),
 * t32_data_e1588 (AE parameter block; +576 / +592 long / short integration
 * time, +1285 trigger byte), t32_data_e15a8 (AE context: word 0 the state
 * block, word 1 the tuning block, +2008 the "algorithm done" completion),
 * t32_data_e15b8 (algorithm running flag), t32_data_e15d8 (AE output
 * block, +10852 copy of tuning u16 +8568) and t32_data_dd2d4 (sensor
 * callbacks, 232 B per channel: +168 / +172 set long / short integration
 * time).
 *
 * The callees (completion wait, tisp_ae_weight_mean, tisp_ae_tune,
 * tisp_s_awb_attr) are hooks; the sensor callbacks are called through the
 * channel's table as in the vendor.  Checked against Matt Davis' recovered C
 * (PR #15 @ 2e2e06bc) by tests/t32_oracle (t32_aetrig_oracle).
 *
 * Vendor behaviour kept / named: the sensor callbacks get a 3-word info
 * block of which the vendor sets only word 2 (the channel; words 0 and 1
 * are stack left-overs there, 0 in ours); the AWB attribute block of
 * tisp_awb_algo_handle (17 words) has words 6 .. 15 uninitialised in the
 * vendor (0 in ours).  tisp_ae_par_sensor_trig has no result (v0 left-over).
 */
#ifndef TX_ISP_T32_AETRIG_H
#define TX_ISP_T32_AETRIG_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
#endif
#ifndef T32_HOST_U16
#define T32_HOST_U16
typedef uint16_t u16;
#endif
#endif

#define T32_AET_PAR_LONG_IT	576
#define T32_AET_PAR_SHORT_IT	592
#define T32_AET_PAR_TRIG	1285
#define T32_AET_SNS_STRIDE	232
#define T32_AET_SNS_SET_LONG	168
#define T32_AET_SNS_SET_SHORT	172
#define T32_AET_DONE		2008	/* completion in the AE context */
#define T32_AET_DONE_TIMEOUT	200
#define T32_AET_ETIME		(-62)
#define T32_AET_ST_MODE		1684
#define T32_AET_ST_WIN_W	2940
#define T32_AET_ST_WIN_H	2942
#define T32_AET_TUN_VAL		8568
#define T32_AET_OUT_VAL		10852
#define T32_AET_AWB_ATTR_WORDS	17

struct t32_aetrig_ops {
	u32 (*wait_done)(void *completion, u32 timeout);
	/* tisp_ae_weight_mean: mean, the two zone ratios (sum) and the two weighted sums (wsum) */
	void (*weight_mean)(u32 ch, u32 *mean, u32 *sum, u32 win_w, u32 win_h, u32 *wsum);
	u32 (*tune)(u32 ch, u32 mean, u32 *sum, u32 *wsum);			/* tisp_ae_tune */
	u32 (*s_awb_attr)(u32 a0, u32 *attr);					/* tisp_s_awb_attr */
};

typedef u32 (*t32_aetrig_sns_fn)(u32 time, u32 *info);

struct t32_aetrig_dev {
	const struct t32_aetrig_ops *ops;
	const u8 *sensor_trig;		/* t32_data_e1514 */
	const u32 *par;			/* t32_data_e1588: AE parameter blocks (32-bit addresses) */
	const u32 *ctx;			/* t32_data_e15a8 */
	const u32 *alg_run;		/* t32_data_e15b8 */
	const u32 *out;			/* t32_data_e15d8 */
	const u8 *sensor;		/* t32_data_dd2d4 */
};

static inline u8 *t32_aet_ptr(u32 addr) { return (u8 *)(uintptr_t)addr; }
static inline u32 t32_aet_rd32(const u8 *p) { u32 v; memcpy(&v, p, 4); return v; }
static inline u16 t32_aet_rd16(const u8 *p) { u16 v; memcpy(&v, p, 2); return v; }

/* tisp_ae_par_sensor_trig: mark the parameter block when the sensor trigger is set */
static inline void t32_ae_par_sensor_trig(struct t32_aetrig_dev *d, u32 ch)
{
	if (d->sensor_trig[ch] == 1)
		t32_aet_ptr(d->par[ch])[T32_AET_PAR_TRIG] = 1;
}

static inline u32 t32_ae_set_it(struct t32_aetrig_dev *d, u32 ch, u32 par_off, u32 fn_off)
{
	u32 info[3] = { 0, 0, ch };
	const u8 *sns = d->sensor + T32_AET_SNS_STRIDE * ch;
	t32_aetrig_sns_fn fn = (t32_aetrig_sns_fn)(uintptr_t)t32_aet_rd32(sns + fn_off);

	fn(t32_aet_rd32(t32_aet_ptr(d->par[ch]) + par_off) & 0xffff, info);
	return 0;
}

/* tisp_ae_long_set_sensor_integration_time */
static inline u32 t32_ae_long_set_sensor_integration_time(struct t32_aetrig_dev *d, u32 ch)
{
	return t32_ae_set_it(d, ch & 0xff, T32_AET_PAR_LONG_IT, T32_AET_SNS_SET_LONG);
}

/* tisp_ae_short_set_sensor_integration_time */
static inline u32 t32_ae_short_set_sensor_integration_time(struct t32_aetrig_dev *d, u32 ch)
{
	return t32_ae_set_it(d, ch & 0xff, T32_AET_PAR_SHORT_IT, T32_AET_SNS_SET_SHORT);
}

/* tisp_ae_algo_waitdone: wait for the running algorithm (200 jiffies), -ETIME on timeout */
static inline int t32_ae_algo_waitdone(struct t32_aetrig_dev *d, u32 ch)
{
	if (d->alg_run[ch] != 1)
		return 0;
	return d->ops->wait_done(t32_aet_ptr(d->ctx[ch]) + T32_AET_DONE, T32_AET_DONE_TIMEOUT) ? 0 : T32_AET_ETIME;
}

/*
 * tisp_ae_algorithm: weighted mean of the zones, then the AE tune step.  The
 * weighted sums weight_mean writes (its 6th argument, vendor sp + 32) go to
 * tisp_ae_tune as its 4th argument (openimp #333).
 */
static inline u32 t32_ae_algorithm(struct t32_aetrig_dev *d, u32 ch)
{
	const u8 *ctx = t32_aet_ptr(d->ctx[ch & 0xff]);
	const u8 *st = t32_aet_ptr(t32_aet_rd32(ctx));
	const u8 *tun = t32_aet_ptr(t32_aet_rd32(ctx + 4));
	u32 mean = 0, sum[2] = { 0, 0 }, wsum[2] = { 0, 0 }, win_w = 1, win_h = 1;
	u32 val = t32_aet_rd16(tun + T32_AET_TUN_VAL);

	ch &= 0xff;
	memcpy(t32_aet_ptr(d->out[ch]) + T32_AET_OUT_VAL, &val, 4);
	if (t32_aet_rd32(st + T32_AET_ST_MODE) == 1) {
		win_w = t32_aet_rd16(st + T32_AET_ST_WIN_W);
		win_h = t32_aet_rd16(st + T32_AET_ST_WIN_H);
	}
	d->ops->weight_mean(ch, &mean, sum, win_w, win_h, wsum);
	return d->ops->tune(ch, mean, sum, wsum);
}

/* tisp_awb_algo_handle: pass a custom AWB result (message words 3 .. 5) as attribute mode 1 */
static inline u32 t32_awb_algo_handle(struct t32_aetrig_dev *d, u32 a0, const u32 *msg)
{
	u32 attr[T32_AET_AWB_ATTR_WORDS] = { 0 };

	attr[0] = 1;
	attr[1] = msg[3];
	attr[2] = msg[4];
	attr[4] = msg[5];
	return d->ops->s_awb_attr(a0, attr);
}

#endif
