/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 sensor subdev: release of all registered sensors from the vendor module
 * (Matt Davis' recovered C, PR #15): subdev_sensor_ops_release_all_sensor
 * (0x5d28).
 *
 * Sensor subdev (vendor offsets, see tx_isp_t32_sensor_reg.h): sensor list
 * head +0x120, four mutexes +0x144 / +0x150 / +0x15c / +0x168 (taken in this
 * order, released in the same order), "devnode open" word +0x174 (1: busy).
 * Sensor list node: bus type +40 (1 I2C, 2 SPI-like: nothing to release),
 * the I2C client pointer at node - 24 (client +24: adapter).  Every node is
 * unlinked with the kernel list poison (0x100 / 0x200).  Mutex and I2C calls
 * are hooks of struct t32_srel_os.
 *
 * Vendor quirks kept: the busy check returns -1 (not an errno); a node of an
 * unknown bus type is unlinked first, then the call fails with -22 (the
 * nodes before it stay released); the adapter is put before the client is
 * unregistered.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_srel_oracle).
 */
#ifndef TX_ISP_T32_SENSOR_REL_H
#define TX_ISP_T32_SENSOR_REL_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_SRL_LIST		0x120U
#define T32_SRL_MUTEX(i)	(0x144U + (i) * 12U)
#define T32_SRL_OPEN		0x174U
#define T32_SRL_NODE_BUS	40U
#define T32_SRL_NODE_CLIENT	24U	/* below the node */
#define T32_I2C_CLIENT_ADAP	24U
#define T32_SBUS_I2C		1U
#define T32_SBUS_SPI		2U
#define T32_LIST_POISON1	0x100U
#define T32_LIST_POISON2	0x200U

struct t32_srel_os {
	void (*mutex_lock)(u8 *m);
	void (*mutex_unlock)(u8 *m);
	void (*i2c_put_adapter)(u32 adap);
	void (*i2c_unregister_device)(u32 client);
};

static inline void t32_srel_unlock_all(const struct t32_srel_os *os, u8 *sd)
{
	u32 i;

	for (i = 0; i < 4; i++)
		os->mutex_unlock(sd + T32_SRL_MUTEX(i));
}

static inline int t32_subdev_sensor_ops_release_all_sensor(const struct t32_srel_os *os, u8 *sd)
{
	static const char fn[] = "subdev_sensor_ops_release_all_sensor";
	u8 *head, *node;
	u32 i;

	if (!sd)
		return -22;
	if (t32_g_rd32(sd, T32_SRL_OPEN) == 1) {
		isp_printf(2, "[ %s:%d ] the devnode does't have been opened.\n", fn, 342);
		return -1;
	}
	for (i = 0; i < 4; i++)
		os->mutex_lock(sd + T32_SRL_MUTEX(i));
	head = sd + T32_SRL_LIST;
	while ((node = T32_PTR(t32_g_rd32(head, 0))) != head) {
		u8 *next = T32_PTR(t32_g_rd32(node, 0)), *prev = T32_PTR(t32_g_rd32(node, 4));
		u32 bus, client;

		t32_g_wr32(next, 4, T32_WORD(prev));	/* list_del */
		t32_g_wr32(prev, 0, T32_WORD(next));
		t32_g_wr32(node, 0, T32_LIST_POISON1);
		t32_g_wr32(node, 4, T32_LIST_POISON2);
		bus = t32_g_rd32(node, T32_SRL_NODE_BUS);
		if (bus == T32_SBUS_I2C) {
			client = t32_g_rd32(node - T32_SRL_NODE_CLIENT, 0);
			if (t32_g_rd32(T32_PTR(client), T32_I2C_CLIENT_ADAP))
				os->i2c_put_adapter(t32_g_rd32(T32_PTR(client), T32_I2C_CLIENT_ADAP));
			os->i2c_unregister_device(client);
		} else if (bus != T32_SBUS_SPI) {
			t32_srel_unlock_all(os, sd);
			isp_printf(2, "[ %s:%d ] the type of sensor SBUS hasn't been defined.\n", fn, 372);
			return -22;
		}
	}
	t32_srel_unlock_all(os, sd);
	return 0;
}

#endif
