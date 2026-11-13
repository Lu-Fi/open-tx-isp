/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 sensor subdev: input selection from the vendor module (Matt Davis'
 * recovered C, PR #15): subdev_sensor_ops_set_input (0x4d24).
 *
 * Sensor subdev (vendor offsets): sensor list head +0x120, per-input slot
 * of the active sensor +0x128 + 4 * index, mutex +0x138, index mode +0x188,
 * subdev ioctl callback +0x98.  Sensor (also a subdev): ioctl callback
 * +0x98, index +0x124, list node +0x130, own index +0x194 (u16), name
 * pointer +0x1ac, attribute block +0x33c, packed size words +0x378 (u32,
 * low 15 bits) and +0x37c (u16).  Input: enable +0, index +4 (on success
 * replaced by the packed size when the module flag d9008 is set, or always
 * for a newly selected sensor).
 *
 * Flow: re-selecting the active sensor returns 0 at once.  An active sensor
 * in the slot is deinitialised by the subdev callback (cmd 0x1000000) and
 * then told 0x1000001 with no argument; while streaming (the word at
 * slot + 0x4c, i.e. subdev +0x174 + 4 * index, is 4) it refuses with -1.  With
 * the enable word set the list is searched for the index, the sensor is
 * stored in the slot of its own index, gets 0x1000001 with its attribute
 * block and 0x1000000 with the input.  A missing callback is -515.
 *
 * Vendor quirks kept: the deinit callback is the subdev's own (+0x98 of the
 * sensor subdev, called with the subdev), its failure message names the old
 * sensor; with index mode 1 the index check of the found entry is skipped,
 * so when the search runs off the list the cursor (list head minus 0x130,
 * i.e. subdev words) is installed as the sensor; the slot is written before
 * the NULL check of the sensor.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_sinp_oracle).
 */
#ifndef TX_ISP_T32_SENSOR_INPUT_H
#define TX_ISP_T32_SENSOR_INPUT_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_SIN_IOCTL		0x98U
#define T32_SIN_LIST		0x120U
#define T32_SIN_INDEX		0x124U
#define T32_SIN_SLOT		0x128U
#define T32_SIN_NODE		0x130U
#define T32_SIN_MUTEX		0x138U
#define T32_SIN_BUSY		0x4cU	/* slot + this word == 4: streaming */
#define T32_SIN_STREAMING	4U
#define T32_SIN_IDXMODE		0x188U
#define T32_SIN_OWNIDX		0x194U
#define T32_SIN_NAMEP		0x1acU
#define T32_SIN_ATTR		0x33cU
#define T32_SIN_SIZE_HI		0x378U
#define T32_SIN_SIZE_LO		0x37cU
#define T32_SIN_CMD_DEINIT	0x1000000U
#define T32_SIN_CMD_INIT	0x1000001U

/* the subdev ioctl callback at +0x98 */
typedef int (*t32_sin_ioctl_fn)(u8 *sd, u32 cmd, void *arg);

struct t32_sinp_os {
	void (*mutex_lock)(u8 *m);
	void (*mutex_unlock)(u8 *m);
	const u32 *d9008;		/* module flag: report the packed size */
};

static inline u8 *t32_sin_slot(u8 *sd, u32 index)
{
	return sd + T32_SIN_SLOT + index * 4U;
}

static inline u32 t32_sin_size(const u8 *sen)
{
	return (t32_g_rd32(sen, T32_SIN_SIZE_HI) & 0x7fffU) << 16 | t32_g_rd16(sen, T32_SIN_SIZE_LO);
}

static inline int t32_sin_call(u8 *who, u8 *sd, u32 cmd, void *arg)
{
	u32 fn = t32_g_rd32(who, T32_SIN_IOCTL);

	if (!fn)
		return -515;
	return ((t32_sin_ioctl_fn)T32_PTR(fn))(sd, cmd, arg);
}

static inline int t32_subdev_sensor_ops_set_input(const struct t32_sinp_os *os, u8 *sd, u8 *input)
{
	static const char fn[] = "subdev_sensor_ops_set_input";
	u8 *head, *sen, *old;
	u32 index;
	int ret;

	if (!sd || !input)
		return -22;
	index = t32_g_rd32(input, 4);
	old = T32_PTR(t32_g_rd32(t32_sin_slot(sd, index), 0));
	if (t32_g_rd32(input, 0) && old && t32_g_rd32(old, T32_SIN_INDEX) == index) {
		if (*os->d9008)
			t32_g_wr32(input, 4, t32_sin_size(old));
		return 0;
	}
	if (old) {
		if (t32_g_rd32(t32_sin_slot(sd, index), T32_SIN_BUSY) == T32_SIN_STREAMING) {
			isp_printf(1, "Please, streamoff sensor firstly!\n");
			return -1;
		}
		ret = t32_sin_call(sd, sd, T32_SIN_CMD_DEINIT, input);
		if (ret) {
			isp_printf(1, "[ %s:%d ] Failed to deinit the pipeline of %s.\n", fn, 452,
				   (char *)T32_PTR(t32_g_rd32(old, T32_SIN_NAMEP)));
			return ret;
		}
		t32_g_wr32(t32_sin_slot(sd, t32_g_rd32(input, 4)), 0, 0);
		ret = t32_sin_call(old, old, T32_SIN_CMD_INIT, NULL);
		if (ret)
			return ret;
	}
	if (!t32_g_rd32(input, 0))
		return 0;
	os->mutex_lock(sd + T32_SIN_MUTEX);
	head = sd + T32_SIN_LIST;
	for (sen = T32_PTR(t32_g_rd32(head, 0) - T32_SIN_NODE); sen + T32_SIN_NODE != head;
	     sen = T32_PTR(t32_g_rd32(sen, T32_SIN_NODE) - T32_SIN_NODE))
		if (t32_g_rd32(sen, T32_SIN_INDEX) == t32_g_rd32(input, 4))
			break;
	os->mutex_unlock(sd + T32_SIN_MUTEX);
	if (t32_g_rd32(sd, T32_SIN_IDXMODE) != 1 && t32_g_rd32(sen, T32_SIN_INDEX) != t32_g_rd32(input, 4)) {
		isp_printf(2, "[ %s:%d  ] Failed to the set input sensor(%d) that .\n", fn, 481, t32_g_rd32(input, 4));
		return -22;
	}
	t32_g_wr32(t32_sin_slot(sd, t32_g_rd16(sen, T32_SIN_OWNIDX)), 0, T32_WORD(sen));
	if (!sen)
		return -2;
	ret = t32_sin_call(sen, sen, T32_SIN_CMD_INIT, sen + T32_SIN_ATTR);
	if (ret)
		return ret;
	ret = t32_sin_call(sen, sen, T32_SIN_CMD_DEINIT, input);
	if (ret) {
		isp_printf(1, "[ %s:%d ] Failed to deinit the pipeline of %s.\n", fn, 495,
			   (char *)T32_PTR(t32_g_rd32(sen, T32_SIN_NAMEP)));
		return ret;
	}
	t32_g_wr32(input, 4, t32_sin_size(sen));
	return 0;
}

#endif
