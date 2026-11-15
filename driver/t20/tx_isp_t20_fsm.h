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

/*
 * Control block behind t20_sbus.p_control as the SPI/I2C sample
 * functions use it: a non-zero second word means the interrupts are
 * already masked by the caller.
 */
struct t20_sbus_lock {
	uint32_t field_0;
	uint32_t irq_masked;
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
	uint8_t pad_d9;
	uint16_t field_da;		/* awb_normalise: 0x1000 - value */
	uint8_t pad_dc[2];
	uint8_t calibrate_count_de;
	uint8_t pad_df;
	uint32_t field_e0;
	uint8_t field_e4;
	uint8_t pad_e5;
	uint16_t field_e6;
	int32_t black_again_reg;	/* 0xe8: analog gain register value */
	int32_t black_again_log2;	/* 0xec: analog gain, log2 Q4 */
	uint8_t field_f0;
	uint8_t field_f1;		/* apical_fw_init writes 0x1e */
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
	uint32_t word_0c;
	union {
		uint8_t field_10;
		uint8_t ratio_mode;	/* 0x10: 1 = fixed ratio, 0 = antiflicker allowed */
	};
	uint8_t pad_11[3];
	union {
		uint32_t field_14;
		uint32_t fixed_ratio;	/* 0x14: Q8, used when ratio_mode is 1 */
	};
	uint32_t field_18;
	int32_t exposure_log2;			/* 0x1c */
	struct t20_exposure_set next;		/* 0x20: set being prepared */
	union {
		uint32_t field_48;
		int32_t partition_mode;	/* 0x48: 0x2a = integration time first */
	};
	struct t20_cmos_frame_update frame_update;	/* 0x4c */
	struct t20_exposure_set history[8];	/* 0x64 */
	uint32_t history_index;			/* 0x1a4: newest ring entry */
	struct t20_fps_cnt fps_cnt;		/* 0x1a8 */
	uint16_t antiflicker_freq;		/* 0x1b4: Hz << 8, 0 = off */
	uint8_t pad_1b6[6];
	union {
		uint32_t field_1bc;
		int32_t ratio_request;	/* 0x1bc: cmos_set_exposure_target, <= 0x400 */
	};
	union {
		uint32_t field_1c0;
		uint32_t integration_time_prev;	/* 0x1c0: lines, before the last alloc */
	};
	union {
		uint16_t field_1c4;
		uint16_t integration_time;	/* 0x1c4: lines */
	};
	uint16_t field_1c6;
	uint16_t field_1c8;
	uint16_t exposure_ratio;	/* 0x1ca: Q6, 0x40 = 1 */
	int32_t sensor_again_log2;	/* 0x1cc */
	int32_t sensor_dgain_log2;	/* 0x1d0 */
	int32_t isp_dgain_log2;		/* 0x1d4 */
	uint32_t field_1d8;
	uint32_t field_1dc;
	uint8_t pad_1e0[4];
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
	uint8_t pad_13[5];
	union {
		uint16_t field_18;
		uint16_t red_gain;	/* 0x18: Q8 */
	};
	union {
		uint16_t field_1a;
		uint16_t blue_gain;	/* 0x1a: Q8 */
	};
	uint8_t light_source;		/* 0x1c */
	uint8_t pad_1d[3];
	uint32_t field_20;
	uint32_t field_24;
	uint8_t pad_28[8];
	uint32_t field_30;
	uint32_t field_34;
	uint32_t field_38;
	uint32_t field_3c;
	uint32_t field_40;
	uint8_t pad_44[1820];
	int32_t wb_log2[4];		/* 0x760: output gains, log2 */
	union {
		struct {
			uint16_t field_770;
			uint16_t field_772;
			uint16_t field_774;
			uint16_t field_776;
		};
		uint16_t static_gain[4];	/* 0x770: static WB gains */
	};
	union {
		uint32_t field_778;
		int32_t color_temp;	/* 0x778: colour temperature, K */
	};
	uint8_t field_77c;		/* last reported light source */
	uint8_t light_source_cand;	/* 0x77d: candidate light source */
	uint8_t field_77e;		/* hold counter */
	uint8_t pad_77f;
	int32_t api_mode;		/* 0x780: last AWB_MODE API value */
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
	uint8_t pad_0c[5];
	uint8_t field_11;
	uint8_t saturation;		/* 0x12: 0x80 at init */
	uint8_t pad_13[0x13];
	uint16_t out_matrix[9];		/* 0x26 */
	int16_t active_matrix[9];	/* 0x38: CCM in use */
	int16_t sat_matrix[9];		/* 0x4a: saturation matrix */
	uint8_t field_5c;
	uint8_t field_5d;
	union {
		uint8_t field_5e;
		uint8_t ccm_to;		/* 0x5e: transition target, see color_matrix_source */
	};
	union {
		uint8_t field_5f;
		uint8_t ccm_from;	/* 0x5f: transition start */
	};
	union {
		uint8_t field_60;
		uint8_t ccm_steps;	/* 0x60: transition length */
	};
	union {
		uint8_t field_61;	/* CCM transition countdown */
		uint8_t ccm_countdown;
	};
	int16_t ccm_mode1[9];		/* 0x62 */
	int16_t ccm_mode2[9];		/* 0x74 */
	int16_t ccm_mode3[9];		/* 0x86 */
	int16_t ccm_identity[9];	/* 0x98 */
	union {
		uint16_t cm_blend;	/* 0xaa: written to the bytes of 0x398 */
		uint8_t cm_blend_lo;
	};
	uint8_t field_ac;
	uint8_t cm_stage;		/* 0xad */
	uint8_t field_ae;		/* manual matrix enable */
	uint8_t pad_af;
	uint16_t manual_matrix[9];	/* 0xb0 */
	uint8_t pad_c2[2];
	/* 0xc4..0xe0: colour temperature thresholds of color_matrix_update */
	int32_t cm_mode0_below;		/* 0xc4 */
	int32_t cm_mode1_from;		/* 0xc8 */
	int32_t cm_mode1_to;		/* 0xcc */
	int32_t cm_mode2_above;		/* 0xd0 */
	int32_t cm_blend01_from;	/* 0xd4 */
	int32_t cm_blend01_to;		/* 0xd8 */
	int32_t cm_blend12_from;	/* 0xdc */
	int32_t cm_blend12_to;		/* 0xe0 */
};

/* Exposure control tuning entry, calibration table 0x92 (per WDR mode). */
struct t20_exposure_tuning {
	uint32_t word_00;
	uint32_t word_04;
	uint32_t word_08;
	uint32_t hist_ratio_scale;	/* 0x0c */
	uint32_t ratio_damping;		/* 0x10 */
	int32_t target_jump;		/* 0x14 */
	int32_t antiflicker_step;	/* 0x18 */
};

/* AWB preset gain pair, calibration entry 0xff indexed by get_awb_idx. */
struct t20_awb_preset_gain {
	uint16_t red;
	uint16_t blue;
};

/* Apical API command values handled by the *_mode command functions. */
enum t20_api_value {
	T20_API_OFF = 0x03,
	T20_API_AE_AUTO = 0x25,
	T20_API_AE_FULL_MANUAL = 0x26,
	T20_API_AE_MANUAL_GAIN = 0x27,
	T20_API_AE_MANUAL_EXPOSURE_TIME = 0x28,
	T20_API_ANTIFOG_OFF = 0x2d,
	T20_API_ANTIFOG_ON = 0x2e,
	T20_API_AWB_AUTO = 0x32,
	T20_API_AWB_MANUAL = 0x33,
	T20_API_AWB_PRESET_FIRST = 0x34,
	T20_API_AWB_PRESET_LAST = 0x3a,
	T20_API_IRIDIX_MANUAL = 0x3b,
	T20_API_IRIDIX_3C = 0x3c,
	T20_API_IRIDIX_3D = 0x3d,
	T20_API_IRIDIX_3E = 0x3e,
	T20_API_IRIDIX_3F = 0x3f,
	T20_API_IRIDIX_40 = 0x40,
};

struct t20_fsm_iridix {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[4];
	uint16_t field_10;
	uint8_t pad_12[2];
	uint32_t field_14;
	union {
		uint32_t field_18;
		int32_t api_mode;	/* 0x18: last IRIDIX API value */
	};
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
	uint8_t pad_0c[6];
	uint16_t scene_mode;		/* 0x12 */
	uint8_t pad_14[4];
	uint32_t field_18;
	uint16_t field_1c;
	uint16_t field_1e;
	uint16_t field_20;
	uint16_t field_22;
	uint8_t pad_24[24];
	uint8_t wdr_mode;		/* 0x3c (context 0x1524) */
	uint8_t field_3d;
	uint8_t antifog_on;		/* 0x3e (context 0x1526) */
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
	uint8_t pad_2c[0x1800];
	uint32_t out_x;			/* 0x182c */
	uint32_t out_y;			/* 0x1830 */
	uint32_t out_w;			/* 0x1834 */
	uint32_t out_h;			/* 0x1838 */
	uint32_t field_183c;		/* copy of field_1c */
	uint32_t field_1840;
};

/* Default settings block filled by dis_get_default_settings. */
struct t20_dis_settings {
	int32_t enable;		/* 0x00 */
	int32_t word_04;
	int32_t word_08;
	int32_t word_0c;
	int32_t word_10;
	int32_t word_14;
	int32_t word_18;
	int32_t word_1c;
};

/* DIS IIR filter state cleared by iir_filter_reset. */
struct t20_iir_filter {
	uint8_t pad_00[0x28];
	union {
		uint8_t clear[0xcc];	/* 0x28 */
		struct {
			uint8_t pad_28[0xb8];
			uint32_t limit_lo;	/* 0xe0 */
			uint32_t limit_hi;	/* 0xe4 */
		};
	};
};

/*
 * Crop/scaler FSM: one crop window for the full-resolution pipe and a
 * crop window plus down-scaler for each of the two DS pipes.  Written by
 * the image_resize_ / image_crop_ commands, applied by _update_fr /
 * _update_ds / _update_ds2.
 */
struct t20_crop_window {
	uint16_t width;
	uint16_t height;
	uint16_t x;
	uint16_t y;
};

struct t20_crop_fr {
	uint8_t crop_enable;
	struct t20_crop_window crop;
	uint16_t out_width;		/* size leaving the pipe */
	uint16_t out_height;
};

struct t20_crop_ds {
	uint8_t crop_enable;
	struct t20_crop_window crop;
	uint8_t scaler_enable;
	uint16_t scaled_width;		/* scaler target, never above its input */
	uint16_t scaled_height;
	uint16_t out_width;		/* size leaving the pipe */
	uint16_t out_height;
};

struct t20_fsm_crop {
	struct t20_fsm_hdr hdr;
	uint32_t irq_mask;		/* 0x0c: interrupts requested at init */
	struct t20_crop_fr fr;		/* 0x10 */
	struct t20_crop_ds ds1;		/* 0x1e */
	struct t20_crop_ds ds2;		/* 0x32 */
};
_Static_assert(__builtin_offsetof(struct t20_fsm_crop, fr.crop) == 0x12, "t20_fsm_crop.fr.crop");
_Static_assert(__builtin_offsetof(struct t20_fsm_crop, fr.out_width) == 0x1a, "t20_fsm_crop.fr.out_width");
_Static_assert(__builtin_offsetof(struct t20_fsm_crop, ds1) == 0x1e, "t20_fsm_crop.ds1");
_Static_assert(__builtin_offsetof(struct t20_fsm_crop, ds1.scaler_enable) == 0x28, "t20_fsm_crop.ds1.scaler_enable");
_Static_assert(__builtin_offsetof(struct t20_fsm_crop, ds1.out_width) == 0x2e, "t20_fsm_crop.ds1.out_width");
_Static_assert(__builtin_offsetof(struct t20_fsm_crop, ds2) == 0x32, "t20_fsm_crop.ds2");
_Static_assert(__builtin_offsetof(struct t20_fsm_crop, ds2.scaled_width) == 0x3e, "t20_fsm_crop.ds2.scaled_width");
_Static_assert(sizeof(struct t20_fsm_crop) == 0x48, "t20_fsm_crop size");

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
	uint8_t field_14;	/* (context 0x2fc) non-zero: cmos_set_exposure_target does nothing */
};

struct t20_fsm_noise_reduction {
	struct t20_fsm_hdr hdr;		/* body: struct tx_isp_t20_nr_state */
};

/* Event loop buffer header (apical_loop_buffer_init), 0x10 bytes. */
struct t20_loop_buffer {
	int32_t head;		/* 0x00 */
	int32_t tail;		/* 0x04 */
	int32_t data;		/* 0x08: storage */
	int32_t size;		/* 0x0c: capacity */
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
	uint8_t sw_state;		/* 0x10: software run/pause state */
	uint8_t pad_11;
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
T20_CTX_AT(general.scene_mode, 0x14fa);
T20_CTX_AT(color_matrix.field_5c, 0xf44);
T20_CTX_AT(color_matrix.field_ae, 0xf96);
T20_CTX_AT(color_matrix.manual_matrix, 0xf98);
T20_CTX_AT(sharpening.field_20, 0x1034);
T20_CTX_AT(sensor.ctrl.param.total.height, 0x50);
T20_CTX_AT(cmos.isp_dgain_log2, 0x2d4);
T20_CTX_AT(awb.field_18, 0x760);
T20_CTX_AT(awb.light_source, 0x764);
T20_CTX_AT(awb.field_40, 0x788);
T20_CTX_AT(awb.light_source_cand, 0xec5);
T20_CTX_AT(awb.field_798, 0xee0);
T20_CTX_AT(dis, 0x152c);
T20_CTX_AT(sensor.field_da, 0xe6);
T20_CTX_AT(sensor.field_f1, 0xfd);
T20_CTX_AT(cmos.field_10, 0x110);
T20_CTX_AT(awb.field_12, 0x75a);
T20_CTX_AT(awb.static_gain, 0xeb8);
T20_CTX_AT(awb.api_mode, 0xec8);
T20_CTX_AT(awb.field_784, 0xecc);
T20_CTX_AT(color_matrix.saturation, 0xefa);
T20_CTX_AT(color_matrix.out_matrix, 0xf0e);
T20_CTX_AT(color_matrix.cm_blend, 0xf92);
T20_CTX_AT(color_matrix.cm_mode0_below, 0xfac);
T20_CTX_AT(color_matrix.cm_blend12_to, 0xfc8);
T20_CTX_AT(iridix.field_18, 0xfec);
T20_CTX_AT(general.antifog_on, 0x1526);
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

/*
 * Layout of the exposure_partitions_balanced[] read-only block: the
 * balanced partition list and the two fixed matrices that follow it.
 */
struct t20_exposure_partition {
	uint8_t type;		/* 0 integration time, 1 gain, >= 2 unused */
	uint8_t limit;
};

#define T20_EXPOSURE_PARTITIONS 10

struct t20_partition_tables {
	struct t20_exposure_partition balanced[T20_EXPOSURE_PARTITIONS];
	uint8_t pad_14[8];
	int16_t ccm_identity[9];	/* 0x1c: Q8 unity matrix */
	uint8_t pad_2e[2];
	int16_t ccm_gray[9];		/* 0x30: Q8 luma weights 0x4c/0x96/0x1d */
};
_Static_assert(__builtin_offsetof(struct t20_partition_tables, ccm_identity) == 0x1c, "t20_partition_tables.ccm_identity");
_Static_assert(__builtin_offsetof(struct t20_partition_tables, ccm_gray) == 0x30, "t20_partition_tables.ccm_gray");
_Static_assert(sizeof(struct t20_partition_tables) == 66, "t20_partition_tables size");

/*
 * Pointer to a named member, computed as integer "object + offset" the way
 * the recovered code did.  A direct obj->member access (or pointer
 * arithmetic on obj) tells the compiler that the member lies too far
 * into the object to overlap a small global such as stab, and it then
 * reorders the stores; use this where the access order has to be kept.
 */
#define t20_member_ptr(obj, member)					\
	((__typeof__((obj)->member) *)((uintptr_t)(obj) +		\
		__builtin_offsetof(__typeof__(*(obj)), member)))
#define t20_member(obj, member) (*t20_member_ptr(obj, member))

/* Table of 32-bit entries with a bound, used by get_exp. */
struct t20_exp_lut {
	uint32_t *entries;	/* 0x00 */
	uint32_t count;		/* 0x04 */
};

#endif
