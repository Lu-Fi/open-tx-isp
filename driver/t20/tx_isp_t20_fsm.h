#ifndef TX_ISP_T20_FSM_H
#define TX_ISP_T20_FSM_H

/* Common header of every T20 Apical firmware FSM object. */
struct t20_fsm_hdr {
	void *isp;		/* 0x00: owning apical ISP context */
	int32_t state;		/* 0x04: current state */
	int32_t pending_irq;	/* 0x08: pending interrupt mask */
};

/*
 * The OEM fsm_clear routines issue one store per field (no merging of
 * adjacent byte/halfword stores into wider ones); keep that exactly.
 */
#define T20_NO_STORE_MERGE __attribute__((optimize("no-store-merging")))

static inline struct t20_fsm_hdr *t20_hdr(void *p)
{
	return (struct t20_fsm_hdr *)p;
}

/*
 * Sensor control block of the sensor FSM.  Layout and names: Ingenic SDK
 * isp/t20/apical-isp/sensor_drv.h (sensor_context_t, sensor_param_t,
 * sensor_control_t with SENSOR_EXP_NUMBER 1, SENSOR_CROP_OFFSET 0) and
 * apical_sbus.h (apical_sbus_t).  The firmware unit does not include the
 * SDK headers, so the layout is mirrored here and checked below.
 */
struct t20_sbus {
	uint32_t mask;
	uint8_t device;
	uint32_t control;
	void *p_control;
	uint32_t (*read_sample)(struct t20_sbus *bus, uint32_t addr,
				uint8_t sample_size);
	void (*write_sample)(struct t20_sbus *bus, uint32_t addr,
			     uint32_t sample, uint8_t sample_size);
};

struct t20_sensor_context {
	uint16_t again;
	uint16_t dgain;
	uint8_t n_context;
	uint8_t wdr_mode;
	uint16_t again_x2;
	uint16_t dgain_coarse;
	uint16_t dgain_fine;
	uint8_t column_buffer_gain_index;
};

struct t20_image_resolution {
	uint16_t width;
	uint16_t height;
};

struct t20_sensor_param {
	uint8_t mode;
	struct t20_image_resolution total;
	struct t20_image_resolution active;
	struct t20_sensor_context sensor_ctx;
	int32_t again_log2_max;
	int32_t dgain_log2_max;
	uint32_t integration_time_min;
	uint32_t integration_time_max;
	uint32_t integration_time_long_max;
	uint32_t integration_time_limit;
	uint16_t day_light_integration_time_max;
	uint8_t integration_time_apply_delay;
	uint8_t analog_gain_apply_delay;
	uint8_t digital_gain_apply_delay;
	int32_t xoffset;
	int32_t yoffset;
	int32_t anti_flicker_pos;
	uint32_t lines_per_second;
};

struct t20_sensor_control {
	struct t20_sbus sbus;
	struct t20_sensor_param param;
	void (*hw_reset_disable)(void);
	void (*hw_reset_enable)(void);
	int32_t (*alloc_analog_gain)(int32_t gain,
				     struct t20_sensor_context *ctx);
	int32_t (*alloc_digital_gain)(int32_t gain,
				      struct t20_sensor_context *ctx);
	void (*alloc_integration_time)(uint16_t *int_time,
				       struct t20_sensor_context *ctx);
	void (*set_integration_time)(struct t20_sbus *bus, uint16_t int_time,
				     struct t20_sensor_param *param);
	void (*start_changes)(struct t20_sbus *bus,
			      struct t20_sensor_context *ctx);
	void (*end_changes)(struct t20_sbus *bus,
			    struct t20_sensor_context *ctx);
	void (*set_analog_gain)(struct t20_sbus *bus, uint32_t again_reg_val,
				struct t20_sensor_context *ctx);
	void (*set_digital_gain)(struct t20_sbus *bus, uint32_t dgain_reg_val,
				 struct t20_sensor_context *ctx);
	uint16_t (*get_normal_fps)(struct t20_sensor_param *param);
	uint16_t (*read_black_pedestal)(struct t20_sbus *bus, int i,
					uint32_t gain);
	void (*set_mode)(struct t20_sbus *bus, uint8_t mode,
			 struct t20_sensor_param *param);
	void (*set_wdr_mode)(struct t20_sbus *bus, uint8_t wdr,
			     struct t20_sensor_param *param);
	uint8_t (*fps_control)(struct t20_sbus *bus, uint8_t fps,
			       struct t20_sensor_param *param);
	uint16_t (*get_id)(struct t20_sbus *bus);
	void (*disable_isp)(struct t20_sbus *bus);
	uint32_t (*get_lines_per_second)(struct t20_sbus *bus,
					 struct t20_sensor_param *param);
};

struct t20_fsm_sensor {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[0x1c];
	struct t20_sensor_control ctrl;	/* 0x28 (context 0x34) */
	uint8_t pad_d0[4];
	int32_t field_d4;
	uint8_t field_d8;
	uint8_t pad_d9[5];
	uint8_t calibrate_count_de;
	uint8_t pad_df;
	uint32_t field_e0;
	uint8_t field_e4;
	uint8_t pad_e5;
	uint16_t field_e6;
	int32_t black_again_reg;	/* 0xe8: analog gain register value */
	int32_t black_again_log2;	/* 0xec: analog gain, log2 Q4 */
	uint8_t field_f0;
	uint8_t pad_f1;
	uint8_t field_f2;
};

/* One frame's exposure settings; the cmos FSM keeps a ring of 8. */
struct t20_exposure_set {
	int32_t exposure_log2;		/* 0x00 */
	int32_t again_log2;		/* 0x04 */
	int32_t dgain_log2;		/* 0x08 */
	int32_t word_0c;
	int32_t integration_time;	/* 0x10 */
	int32_t again_reg;		/* 0x14 */
	int32_t dgain_reg;		/* 0x18 */
	int32_t isp_dgain;		/* 0x1c */
	int32_t exposure_ratio;		/* 0x20: Q6, 0x40 = 1 */
	int32_t integration_time_long;	/* 0x24 */
};

/* What the frame interrupt hands to the sensor callbacks (OEM 0x1f2f4). */
struct t20_cmos_frame_update {
	int32_t integration_time;	/* 0x00 */
	int32_t again_reg;		/* 0x04 */
	int32_t dgain_reg;		/* 0x08 */
	int32_t isp_dgain;		/* 0x0c: written to register 0x1a0 */
	int32_t exposure_ratio;		/* 0x10: written to 0x24c / 0x400ac */
	int32_t integration_time_long;	/* 0x14 */
};

/* Frame-period counter used by _init_fps_cnt/_process_fps_cnt/cmos_get_fps. */
struct t20_fps_cnt {
	uint32_t last_timestamp;	/* 0x00 */
	uint32_t period_x16;		/* 0x04: running mean of the period, <<4 */
	uint8_t skip_next;		/* 0x08: do not average the next frame */
};

struct t20_fsm_cmos {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[4];
	uint8_t field_10;
	uint8_t pad_11[3];
	uint32_t field_14;
	uint32_t field_18;
	int32_t exposure_log2;			/* 0x1c */
	struct t20_exposure_set next;		/* 0x20: set being prepared */
	uint32_t field_48;
	struct t20_cmos_frame_update frame_update;	/* 0x4c */
	struct t20_exposure_set history[8];	/* 0x64 */
	uint32_t history_index;			/* 0x1a4: newest ring entry */
	struct t20_fps_cnt fps_cnt;		/* 0x1a8 */
	uint16_t antiflicker_freq;		/* 0x1b4: Hz << 8, 0 = off */
	uint8_t pad_1b6[6];
	uint32_t field_1bc;
	uint8_t pad_1c0[4];
	uint16_t field_1c4;
	uint16_t field_1c6;
	uint16_t field_1c8;
	uint16_t exposure_ratio;	/* 0x1ca: Q6, 0x40 = 1 */
	int32_t sensor_again_log2;	/* 0x1cc */
	int32_t sensor_dgain_log2;	/* 0x1d0 */
	int32_t isp_dgain_log2;		/* 0x1d4 */
	uint32_t field_1d8;
	uint8_t pad_1dc[8];
	uint32_t field_1e4;
};

struct t20_fsm_ae {
	struct t20_fsm_hdr hdr;
	uint32_t word_0c;
	union {
		union {
		uint8_t clear_10[0x18];		/* zeroed by AE_fsm_clear */
		struct {
			int32_t target_log2;	/* 0x10: ae_calculate_target */
			uint32_t exposure_log2;	/* 0x14 */
		};
	};
		struct {
			uint8_t pad_10[8];
			uint32_t exposure_correction_lo;	/* 0x18 */
			uint32_t exposure_correction_hi;	/* 0x1c */
		};
	};
	uint32_t field_28;
	uint32_t field_2c;
	uint32_t hist[256];		/* 0x30: full histogram */
	uint32_t hist_total;		/* 0x430: sum of all bins */
	uint32_t roi;			/* 0x434: AE ROI, 4 x u8 */
	uint8_t hist_request;		/* 0x438: histogram dump requested */
};

struct t20_fsm_awb {
	struct t20_fsm_hdr hdr;
	uint32_t word_0c;
	uint16_t field_10;
	uint8_t field_12;
	uint8_t pad_13[13];
	uint32_t field_20;
	uint32_t field_24;
	uint8_t pad_28[8];
	uint32_t field_30;
	uint32_t field_34;
	uint32_t field_38;
	uint32_t field_3c;
	uint8_t pad_40[1824];
	int32_t wb_log2[4];		/* 0x760: output gains, log2 */
	uint16_t field_770;
	uint16_t field_772;
	uint16_t field_774;
	uint16_t field_776;
	uint32_t field_778;
	uint8_t field_77c;
	uint8_t pad_77d;
	uint8_t field_77e;
	uint8_t pad_77f[5];
	uint32_t field_784;
	uint32_t field_788;
	uint16_t field_78c;
	uint16_t field_78e;
	uint16_t field_790;
	uint16_t field_792;
	uint32_t roi;			/* 0x794: AWB ROI, 4 x u8 */
	uint32_t field_798;
	uint32_t field_79c;
};

struct t20_fsm_color_matrix {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[0x50];
	uint8_t field_5c;
	uint8_t field_5d;
	uint8_t field_5e;
	uint8_t field_5f;
	uint8_t field_60;
	uint8_t field_61;
	uint8_t pad_62[0x4c];
	uint8_t field_ae;
};

struct t20_fsm_iridix {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[4];
	uint16_t field_10;
	uint8_t pad_12[2];
	uint32_t field_14;
	uint32_t field_18;
};

struct t20_fsm_sharpening {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[20];
	uint16_t field_20;
	uint8_t field_22;
	uint8_t pad_23[41];
	uint8_t field_4c;
	uint8_t field_4d;
	uint8_t field_4e;
	uint8_t pad_4f;
	uint16_t field_50;
	uint16_t field_52;
	uint16_t field_54;
	uint16_t field_56;
	uint32_t field_58;
	uint32_t field_5c;
	uint8_t field_60;
	uint8_t pad_61;
	uint16_t field_62;
	uint16_t field_64;
	uint8_t field_66;
};

struct t20_fsm_flash {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[5];
	uint8_t field_11;
};

struct t20_fsm_general {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[12];
	uint32_t field_18;
	uint16_t field_1c;
	uint16_t field_1e;
	uint16_t field_20;
	uint16_t field_22;
	uint8_t pad_24[24];
	uint8_t wdr_mode;		/* 0x3c (context 0x1524) */
	uint8_t field_3d;
	uint8_t field_3e;
	uint8_t field_3f;
	uint8_t field_40;
	uint8_t field_41;
};

struct t20_fsm_dis {
	struct t20_fsm_hdr hdr;
	uint32_t word_0c;
	uint32_t field_10;
	uint32_t field_14;
	uint32_t field_18;
	uint32_t field_1c;
	uint32_t field_20;
	uint32_t field_24;
	uint32_t field_28;
	uint8_t pad_2c[0x1814];
	uint32_t field_1840;
};

struct t20_fsm_crop {
	struct t20_fsm_hdr hdr;
};

struct t20_fsm_matrix_yuv {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[0x94];
	uint16_t field_a0;
	uint8_t strength[3];		/* 0xa2: 0x80 = neutral */
	uint8_t pad_a5[3];
	uint8_t field_a8;
	uint8_t pad_a9[0x19];
	uint8_t field_c2;
	uint8_t pad_c3[0x19];
	uint8_t field_dc;
};

/* Coefficient/gain view of the matrix_yuv FSM written by matrix_yuv_initialize. */
struct __attribute__((packed, aligned(2))) t20_matrix_yuv_regs {
	uint8_t pad_00[0x28];
	uint16_t coef_28[12];
	uint16_t coef_40[12];
	uint16_t coef_58[12];
	uint8_t pad_70[0x18];
	uint16_t coef_88[12];
	uint16_t field_a0;
	uint8_t strength[3];		/* 0xa2 */
	uint8_t field_a5;
	uint8_t field_a6;
	uint8_t field_a7;
	uint8_t pad_a8[2];
	uint16_t coef_aa[12];
	uint8_t pad_c2[2];
	uint16_t coef_c4[12];
	uint8_t pad_dc[2];
	uint16_t coef_de[12];
	uint16_t field_f6;
};

struct t20_fsm_defect_pixel {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[6];
	uint16_t threshold;	/* 0x12 */
	uint8_t field_14;
};

struct t20_fsm_noise_reduction {
	struct t20_fsm_hdr hdr;		/* body: struct tx_isp_t20_nr_state */
};

struct t20_fw_outer;

/*
 * The Apical ISP context ("fsm manager", 0x2fb8 bytes): every FSM object
 * at the place where the OEM apical_isp_init() (0x269d0) sets it up.  Each
 * FSM's hdr.isp points back at this context.
 */
#define T20_CTX_FSM(type, name, size) \
	union { struct type name; uint8_t name##_space[size]; }

struct t20_isp_ctx {
	uint32_t word_00;
	struct t20_fw_outer *fw;				/* 0x0004 */
	uint32_t word_08;
	T20_CTX_FSM(t20_fsm_sensor, sensor, 0xf4);		/* 0x000c */
	T20_CTX_FSM(t20_fsm_cmos, cmos, 0x1e8);			/* 0x0100 */
	T20_CTX_FSM(t20_fsm_defect_pixel, defect_pixel, 0x20);	/* 0x02e8 */
	T20_CTX_FSM(t20_fsm_ae, ae, 0x440);			/* 0x0308 */
	T20_CTX_FSM(t20_fsm_awb, awb, 0x7a0);			/* 0x0748 */
	T20_CTX_FSM(t20_fsm_color_matrix, color_matrix, 0xec);	/* 0x0ee8 */
	T20_CTX_FSM(t20_fsm_iridix, iridix, 0x1c);		/* 0x0fd4 */
	T20_CTX_FSM(t20_fsm_noise_reduction, noise_reduction, 0x24); /* 0x0ff0 */
	T20_CTX_FSM(t20_fsm_sharpening, sharpening, 0x468);	/* 0x1014 */
	T20_CTX_FSM(t20_fsm_flash, flash, 0x24);		/* 0x147c */
	T20_CTX_FSM(t20_fsm_crop, crop, 0x48);			/* 0x14a0 */
	T20_CTX_FSM(t20_fsm_general, general, 0x44);		/* 0x14e8 */
	T20_CTX_FSM(t20_fsm_dis, dis, 0x1848);			/* 0x152c */
	T20_CTX_FSM(t20_fsm_matrix_yuv, matrix_yuv, 0x130);	/* 0x2d74 */
	uint8_t event_queue[0x10];				/* 0x2ea4: loop buffer */
	uint8_t event_queue_data[0x100];			/* 0x2eb4 */
	uint8_t pad_2fb4[4];
};

/*
 * The firmware object (apical_get_fw(), 0x2fd0 bytes with one ISP): a
 * 0x18-byte head and the ISP contexts; isp[n].fw points back at it.
 */
struct t20_fw_outer {
	uint32_t isp_index;		/* 0x00: context the command API uses */
	uint32_t irq_locked;		/* 0x04: caller already holds the IRQ lock */
	uint32_t word_08;
	void *event_sem;		/* 0x0c */
	uint8_t pad_10[2];
	uint8_t hflip;			/* 0x12 */
	uint8_t pad_13;
	uint8_t vflip;			/* 0x14 */
	uint8_t pad_15[3];
	struct t20_isp_ctx isp[1];	/* 0x18 */
};
_Static_assert(__builtin_offsetof(struct t20_fw_outer, isp) == 0x18, "t20_fw_outer.isp");
_Static_assert(sizeof(struct t20_fw_outer) == 0x2fd0, "t20_fw_outer size");

#define T20_CTX_AT(member, offset) \
	_Static_assert(__builtin_offsetof(struct t20_isp_ctx, member) == (offset), \
		       "t20_isp_ctx." #member)
T20_CTX_AT(sensor, 0x0c);
T20_CTX_AT(sensor.ctrl.sbus, 0x34);
T20_CTX_AT(sensor.ctrl.param, 0x4c);
T20_CTX_AT(sensor.ctrl.param.sensor_ctx, 0x56);
T20_CTX_AT(sensor.ctrl.param.integration_time_max, 0x70);
T20_CTX_AT(sensor.ctrl.param.integration_time_apply_delay, 0x7e);
T20_CTX_AT(sensor.ctrl.alloc_analog_gain, 0x9c);
T20_CTX_AT(sensor.ctrl.set_integration_time, 0xa8);
T20_CTX_AT(sensor.ctrl.get_normal_fps, 0xbc);
T20_CTX_AT(sensor.black_again_reg, 0xf4);
T20_CTX_AT(cmos, 0x100);
T20_CTX_AT(cmos.frame_update, 0x14c);
T20_CTX_AT(cmos.history, 0x164);
T20_CTX_AT(cmos.history_index, 0x2a4);
T20_CTX_AT(cmos.fps_cnt, 0x2a8);
T20_CTX_AT(cmos.antiflicker_freq, 0x2b4);
T20_CTX_AT(cmos.sensor_again_log2, 0x2cc);
T20_CTX_AT(defect_pixel, 0x2e8);
T20_CTX_AT(ae, 0x308);
T20_CTX_AT(ae.hist, 0x338);
T20_CTX_AT(ae.hist_total, 0x738);
T20_CTX_AT(ae.roi, 0x73c);
T20_CTX_AT(ae.hist_request, 0x740);
T20_CTX_AT(awb, 0x748);
T20_CTX_AT(awb.wb_log2, 0xea8);
T20_CTX_AT(awb.roi, 0xedc);
T20_CTX_AT(color_matrix, 0xee8);
T20_CTX_AT(iridix, 0xfd4);
T20_CTX_AT(noise_reduction, 0xff0);
T20_CTX_AT(sharpening, 0x1014);
T20_CTX_AT(flash, 0x147c);
T20_CTX_AT(crop, 0x14a0);
T20_CTX_AT(general, 0x14e8);
T20_CTX_AT(general.wdr_mode, 0x1524);
T20_CTX_AT(dis, 0x152c);
T20_CTX_AT(matrix_yuv, 0x2d74);
T20_CTX_AT(event_queue, 0x2ea4);
T20_CTX_AT(event_queue_data, 0x2eb4);
_Static_assert(sizeof(struct t20_isp_ctx) == 0x2fb8, "t20_isp_ctx size");

/* Ring entry "frame" steps after the newest one (negative = older). */
void *cmos_get_frame_exposure_set(void *cmos, int32_t frame);

static inline const struct t20_exposure_set *
t20_cmos_frame_set(struct t20_fsm_cmos *cmos, int32_t frame)
{
	return cmos_get_frame_exposure_set(cmos, frame);
}

/* A sibling FSM of the same context, reached from an FSM pointer. */
#define t20_fsm_sibling(fsm, self, other)				\
	((void *)((uint8_t *)(fsm) +					\
		  (__builtin_offsetof(struct t20_isp_ctx, other) -	\
		   __builtin_offsetof(struct t20_isp_ctx, self))))

/* Table of 32-bit entries with a bound, used by get_exp. */
struct t20_exp_lut {
	uint32_t *entries;	/* 0x00 */
	uint32_t count;		/* 0x04 */
};

#endif
