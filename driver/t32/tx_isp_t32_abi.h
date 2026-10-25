/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 (PRJ007) TX-ISP user and sensor-module ABI.
 *
 * Every value here was observed, not assumed from T41: the ioctl numbers and
 * payloads are the ones vendor libimp 2.2.0 issues in QEMU (-M ingenic-t32,
 * S0 trace, audit/T32_S0_1025.md); the object offsets were measured with the
 * target toolchain against the T32 4.4.94 SDK headers that the prebuilt
 * sensor modules are compiled with.  T32 numbers differ from T41: never reuse
 * TX_ISP_FRAME_IOCTL_T41_* or the T41 subdevice offsets for this SoC.
 *
 * MIPS ioctl encoding: dir bits 31:29 (1 none, 2 read, 4 write), size 28:16.
 */
#ifndef TX_ISP_T32_ABI_H
#define TX_ISP_T32_ABI_H

#include <linux/types.h>

/* ---- /dev/tx-isp (type 'T'; bases: DEVICE 0, TUNING 100, IVDC 110, FS 130,
 *      LDC 180, DEVICE1 200) ------------------------------------------------ */
#define T32_IOC_DRIVER_VERSION		0x80045401U	/* arg: 64 B buffer, soc[32] + version[32] */
#define T32_IOC_ENUMINPUT		0xc0045402U	/* RW {index, name[32]}; -EINVAL past the last */
#define T32_IOC_G_INPUT			0x80085403U	/* W {enable, vinum} */
#define T32_IOC_S_INPUT			0xc0085404U	/* RW {index, out: w << 16 | h} */
#define T32_IOC_REGISTER_SENSOR		0x80745405U	/* W struct t32_sensor_reginfo */
#define T32_IOC_RELEASE_SENSOR		0x80745406U
#define T32_IOC_STREAMON		0x80085407U	/* W {enable, vinum} */
#define T32_IOC_STREAMOFF		0x80085408U
#define T32_IOC_CREATE_SUBDEV_LINKS	0x80085409U
#define T32_IOC_DESTROY_SUBDEV_LINKS	0x8008540aU
#define T32_IOC_LINKS_STREAMON		0xc008540bU
#define T32_IOC_LINKS_STREAMOFF		0xc008540cU
#define T32_IOC_SET_SENSOR_REGISTER	0xc040540dU
#define T32_IOC_GET_SENSOR_REGISTER	0x8040540eU
#define T32_IOC_SET_ISP_MEM_INFO	0x800c540fU	/* W t32_buf_info {vinum, pa, size} */
#define T32_IOC_GET_ISP_MEM_INFO	0x800c5410U	/* W t32_buf_info; driver writes size */
#define T32_IOC_SET_ISP_IPC_MEM_INFO	0x800c5438U
#define T32_IOC_GET_ISP_IPC_MEM_INFO	0x800c5439U	/* driver writes size (0 = none) */
#define T32_IOC_MEMORY_MALLOC		0x800c543aU	/* W {.., .., size}; driver writes {kva, pa, size} */
#define T32_IOC_MEMORY_FREE		0x800c543bU	/* W {kva, 0, ..} */
#define T32_IOC_SET_TIZIANO_PAR		0xc00454c8U	/* RW int: pa of a MEMORY_MALLOC buffer (IQ bin) */
#define T32_IOC_DEFAULT_BIN		0xc00454c9U	/* arg: 880 B buffer, see core */

/* ---- /dev/isp-m0 ---------------------------------------------------------- */
#define T32_IOC_TUNING_DEFAULT		0xc0105467U	/* RW {dir, ?, cmd id, ptr/value} */
#define T32_TUNING_ID_VERSION		0x08000070U	/* libimp GET at EnableTuning; vendor answers 0x000f0001 */
#define T32_TUNING_VERSION_VALUE	0x000f0001U

/* ---- /dev/misc-ivdc ------------------------------------------------------- */
#define T32_IOC_IVDC_GET_DIRECT		0xc0045471U	/* RW int; 0 = not in direct mode */
#define T32_IOC_IVDC_SET_DIRECT		0xc0045472U

/* ---- /dev/framechanN (base 130) ------------------------------------------- */
#define T32_IOC_FS_SET_FORMAT		0xc0045483U	/* RW int (1); no format struct on T32 */
#define T32_IOC_FS_GET_FORMAT		0xc0045484U
#define T32_IOC_FS_REQBUFS		0xc0145485U	/* 20 B {count, type, memory, 0, 0} */
#define T32_IOC_FS_QUERYBUF		0xc0605486U
#define T32_IOC_FS_QBUF			0xc0605487U	/* 96 B tisp_buffer */
#define T32_IOC_FS_DQBUF		0xc0605488U
#define T32_IOC_FS_STREAMON		0xc0045489U
#define T32_IOC_FS_STREAMOFF		0xc004548aU
#define T32_IOC_FS_SET_BANKS		0xc004548bU
#define T32_IOC_FS_LISTEN_BUF		0xc004548cU	/* RW int: wait, returns ready count */

/* ---- payloads ------------------------------------------------------------- */
struct t32_initarg {			/* G_INPUT, STREAMON, LINKS_* */
	__s32 enable;
	__s32 vinum;
};

struct t32_buf_info {			/* ISP/IPC mem info, MALLOC, FREE */
	__u32 word0;			/* vinum (mem info) / kva (malloc out) */
	__u32 word1;			/* pa */
	__u32 size;
};

/*
 * Kernel copy of the sensor registration (116 bytes).  libimp converts the
 * public 100-byte IMPSensorInfo into this and puts the VI number into
 * sensor_id.  Offsets match struct tx_isp_sensor_register_info of the SDK.
 */
struct t32_sensor_reginfo {
	char name[32];			/* 0x00 */
	__u32 cbus_type;		/* 0x20: 1 = I2C, 2 = SPI */
	union {				/* 0x24 */
		struct {
			char type[20];	/* 0x24 */
			__s32 addr;	/* 0x38 */
			__s32 i2c_adapter_id; /* 0x3c */
		} i2c;
		struct {
			char modalias[32];
			__s32 bus_num;
		} spi;
	};
	__s32 rst_gpio;			/* 0x48 */
	__s32 pwdn_gpio;		/* 0x4c */
	__s32 power_gpio;		/* 0x50 */
	__s32 switch_gpio;		/* 0x54 */
	__s32 switch_gpio_state;	/* 0x58 */
	__u16 sensor_id;		/* 0x5c */
	__u16 pad;
	__u32 video_interface;		/* 0x60 */
	__u32 mclk;			/* 0x64 */
	__s32 default_boot;		/* 0x68 */
	__u32 fps_num;			/* 0x6c */
	__u32 fps_den;			/* 0x70 */
};

/* ---- objects shared with the prebuilt T32 sensor modules ------------------ */
/* struct tx_isp_subdev (0x120 bytes) */
#define T32_SD_MODULE_DEV		0x004
#define T32_SD_MODULE_NAME		0x008
#define T32_SD_MODULE_PARENT		0x094
#define T32_SD_MODULE_NOTIFY		0x098	/* tx_isp_call_subdev_notify() target */
#define T32_SD_IRQDEV			0x09c
#define T32_SD_CHIP			0x0c0	/* struct tx_isp_chip_ident (0x28) */
#define T32_SD_RES			0x0e8
#define T32_SD_BASE			0x0f4
#define T32_SD_CLKS			0x100
#define T32_SD_CLK_NUM			0x104
#define T32_SD_OPS			0x108
#define T32_SD_NUM_OUTPADS		0x10c
#define T32_SD_NUM_INPADS		0x10e
#define T32_SD_OUTPADS			0x110
#define T32_SD_INPADS			0x114
#define T32_SD_DEV_PRIV			0x118	/* i2c client (tx_isp_get_subdevdata) */
#define T32_SD_HOST_PRIV		0x11c	/* struct tx_isp_sensor */
#define T32_SD_SIZE			0x120

/* struct tx_isp_sensor (0x3ac bytes); sd at 0 */
#define T32_SENSOR_DEV			0x120
#define T32_SENSOR_INFO			0x138	/* struct t32_sensor_reginfo */
#define T32_SENSOR_ATTR			0x1ac	/* struct tx_isp_sensor_attribute (0x190) */
#define T32_SENSOR_VIDEO		0x33c	/* struct tx_isp_video_in (0x68) */
#define T32_SENSOR_MCLK			0x3a4
#define T32_SENSOR_PRIV			0x3a8
#define T32_SENSOR_SIZE			0x3ac

/* struct tx_isp_video_in, relative to T32_SENSOR_VIDEO */
#define T32_VIDEO_MBUS_WIDTH		0x00
#define T32_VIDEO_MBUS_HEIGHT		0x04
#define T32_VIDEO_MBUS_CODE		0x08
#define T32_VIDEO_ATTR			0x34
#define T32_VIDEO_INFO			0x38
#define T32_VIDEO_MAX_WIDTH		0x3c
#define T32_VIDEO_FPS			0x44
#define T32_VIDEO_STATE			0x64

/* struct tx_isp_subdev_ops: core, video, pad, sensor, internal */
#define T32_OPS_CORE			0x00
#define T32_OPS_VIDEO			0x04
#define T32_OPS_PAD			0x08
#define T32_OPS_SENSOR			0x0c
/* core ops */
#define T32_CORE_G_CHIP_IDENT		0x00
#define T32_CORE_INIT			0x04
#define T32_CORE_RESET			0x08
#define T32_CORE_IOCTL			0x20
/* video ops */
#define T32_VIDEO_S_STREAM		0x00
/* sensor ops */
#define T32_SENSOR_OPS_IOCTL		0x08

/* struct tx_isp_chip_ident: name[32], revision ptr, ident */
#define T32_CHIP_IDENT_SIZE		0x28

/* tx_isp_call_subdev_notify() events seen from sensors */
#define T32_EVENT_SUBDEV_INIT		0x01000000U
#define T32_EVENT_SYNC_SENSOR_ATTR	0x01000001U

#endif /* TX_ISP_T32_ABI_H */
