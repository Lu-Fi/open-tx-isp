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

struct t20_fsm_sensor {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[0xc8];
	int32_t field_d4;
	uint8_t field_d8;
	uint8_t pad_d9[5];
	uint8_t calibrate_count_de;
	uint8_t pad_df;
	uint32_t field_e0;
	uint8_t field_e4;
	uint8_t pad_e5;
	uint16_t field_e6;
	uint8_t pad_e8[8];
	uint8_t field_f0;
	uint8_t pad_f1;
	uint8_t field_f2;
};

struct t20_fsm_cmos {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[4];
	uint8_t field_10;
	uint8_t pad_11[3];
	uint32_t field_14;
	uint32_t field_18;
	uint32_t field_1c;
	uint8_t pad_20[40];
	uint32_t field_48;
	uint8_t pad_4c[344];
	uint32_t field_1a4;
	uint8_t pad_1a8[12];
	uint16_t field_1b4;
	uint8_t pad_1b6[6];
	uint32_t field_1bc;
	uint8_t pad_1c0[4];
	uint16_t field_1c4;
	uint16_t field_1c6;
	uint16_t field_1c8;
	uint16_t field_1ca;
	uint8_t pad_1cc[8];
	uint32_t field_1d4;
	uint32_t field_1d8;
	uint8_t pad_1dc[8];
	uint32_t field_1e4;
};

struct t20_fsm_ae {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[4];
	uint8_t clear_10[0x18];
	uint32_t field_28;
	uint32_t field_2c;
	uint8_t pad_30[0x404];
	uint32_t field_434;
	uint8_t field_438;
};

struct t20_fsm_awb {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[4];
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
	uint8_t pad_40[1840];
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
	uint32_t field_794;
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
	uint8_t field_3c;
	uint8_t field_3d;
	uint8_t field_3e;
	uint8_t field_3f;
	uint8_t field_40;
	uint8_t field_41;
};

struct t20_fsm_dis {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[4];
	uint32_t field_10;
	uint32_t field_14;
	uint32_t field_18;
	uint32_t field_1c;
	uint32_t field_20;
	uint32_t field_24;
	uint32_t field_28;
};

struct t20_fsm_matrix_yuv {
	struct t20_fsm_hdr hdr;
	uint8_t pad_0c[0x94];
	uint16_t field_a0;
	uint8_t field_a2;
	uint8_t field_a3;
	uint8_t field_a4;
	uint8_t pad_a5[3];
	uint8_t field_a8;
	uint8_t pad_a9[0x19];
	uint8_t field_c2;
	uint8_t pad_c3[0x19];
	uint8_t field_dc;
};

#endif
