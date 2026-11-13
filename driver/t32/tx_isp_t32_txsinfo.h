/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 sensor-info registry of the module binary (vendor tx_isp_sinfo_init,
 * _exit, _driver_add, _driver_del, _sensor_bind, _sensor_unbind): four
 * 160 byte slots (vendor d9024) under one mutex (ce270), published under
 * /proc/jz/sensor (directory d9020, file "count").
 *
 * Slot: +0 used (u8), +4 sensor driver, +8 driver id, +12 u16 driver data,
 * +16 bound sensor, +20 proc entry, +24 entry name.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txsinfo_oracle).
 */
#ifndef TX_ISP_T32_TXSINFO_H
#define TX_ISP_T32_TXSINFO_H

#include "tx_isp_t32_txos.h"

#define T32_SINFO_SLOTS		4U
#define T32_SINFO_SLOT_BYTES	160U
#define T32_SINFO_USED		0U
#define T32_SINFO_DRV		4U
#define T32_SINFO_ID		8U
#define T32_SINFO_DATA		12U
#define T32_SINFO_SENSOR	16U
#define T32_SINFO_PROC		20U
#define T32_SINFO_NAME		24U
#define T32_SINFO_DRV_NAME	28U	/* sensor driver +28: name (char *) */
#define T32_ENOSPC		28U
#define T32_KERN_WARNING	"\001" "4"

struct t32_tx_sinfo {
	u32 slots;		/* vendor d9024: 4 x 160 bytes */
	u32 lock;		/* vendor ce270 */
	u32 dir;		/* vendor d9020: /proc/jz/sensor, 0 = none */
	u32 fops;		/* vendor bd58c: fops of "count" */
};

/* provided by the driver glue (kernel) or by the oracle (host) */
void t32_tx_os_mutex_lock(u32 m);
void t32_tx_os_mutex_unlock(u32 m);
void t32_tx_os_sinfo_slot_publish(u32 slot, u32 index);
void t32_tx_os_remove_proc_subtree(const char *name, u32 parent);
u32 t32_tx_os_proc_mkdir(const char *name, u32 parent);
u32 t32_tx_os_proc_create_data(const char *name, u32 mode, u32 parent, u32 fops, u32 data);

static inline u32 t32_tx_sinfo_slot(const struct t32_tx_sinfo *si, u32 i)
{
	return si->slots + i * T32_SINFO_SLOT_BYTES;
}

static inline u8 t32_tx_sinfo_used(u32 slot)
{
	return *(volatile u8 *)T32_TX_P(slot + T32_SINFO_USED);
}

static inline void t32_tx_sinfo_set_used(u32 slot, u8 v)
{
	*(volatile u8 *)T32_TX_P(slot + T32_SINFO_USED) = v;
}

/* tx_isp_sinfo_init */
static inline int t32_tx_sinfo_init(struct t32_tx_sinfo *si)
{
	si->dir = t32_tx_os_proc_mkdir("jz/sensor", 0);
	if (!si->dir) {
		t32_tx_os_printk(T32_KERN_WARNING "tx-isp-sinfo: cannot create /proc/jz/sensor\n");
		return 0;
	}
	t32_tx_os_proc_create_data("count", 0444, si->dir, si->fops, 0);
	return 0;
}

/* tx_isp_sinfo_exit */
static inline void t32_tx_sinfo_exit(struct t32_tx_sinfo *si)
{
	t32_tx_os_mutex_lock(si->lock);
	memset(T32_TX_P(si->slots), 0, T32_SINFO_SLOTS * T32_SINFO_SLOT_BYTES);
	t32_tx_os_mutex_unlock(si->lock);
	if (si->dir) {
		t32_tx_os_remove_proc_subtree("jz/sensor", 0);
		si->dir = 0;
	}
}

/* tx_isp_sinfo_driver_add(drv, data, id): first free slot */
static inline int t32_tx_sinfo_driver_add(struct t32_tx_sinfo *si, u32 drv, u32 data, u32 id)
{
	u32 i, s = 0;

	if (!drv || !si->dir)
		return -(int)T32_EINVAL;
	t32_tx_os_mutex_lock(si->lock);
	for (i = 0; i < T32_SINFO_SLOTS; i++) {
		s = t32_tx_sinfo_slot(si, i);
		if (!t32_tx_sinfo_used(s))
			break;
	}
	if (i < T32_SINFO_SLOTS) {
		t32_tx_sinfo_set_used(s, 1);
		*(volatile u16 *)T32_TX_P(s + T32_SINFO_DATA) = (u16)data;
		t32_tx_st(s, T32_SINFO_DRV, drv);
		t32_tx_st(s, T32_SINFO_ID, id);
		t32_tx_st(s, T32_SINFO_SENSOR, 0);
		t32_tx_os_sinfo_slot_publish(s, i);
	}
	t32_tx_os_mutex_unlock(si->lock);
	if (i == T32_SINFO_SLOTS) {
		t32_tx_os_printk(T32_KERN_WARNING "tx-isp-sinfo: no free sensor slot for %s\n",
				 (const char *)T32_TX_P(t32_tx_ld(drv, T32_SINFO_DRV_NAME)));
		return -(int)T32_ENOSPC;
	}
	return 0;
}

/* tx_isp_sinfo_driver_del(drv): every slot of the driver */
static inline void t32_tx_sinfo_driver_del(struct t32_tx_sinfo *si, u32 drv)
{
	u32 i;

	t32_tx_os_mutex_lock(si->lock);
	for (i = 0; i < T32_SINFO_SLOTS; i++) {
		u32 s = t32_tx_sinfo_slot(si, i);

		if (!t32_tx_sinfo_used(s) || t32_tx_ld(s, T32_SINFO_DRV) != drv)
			continue;
		if (t32_tx_ld(s, T32_SINFO_PROC)) {
			t32_tx_os_remove_proc_subtree((const char *)T32_TX_P(s + T32_SINFO_NAME), si->dir);
			t32_tx_st(s, T32_SINFO_PROC, 0);
		}
		t32_tx_sinfo_set_used(s, 0);
		t32_tx_st(s, T32_SINFO_DRV, 0);
		t32_tx_st(s, T32_SINFO_ID, 0);
		t32_tx_st(s, T32_SINFO_SENSOR, 0);
	}
	t32_tx_os_mutex_unlock(si->lock);
}

/*
 * tx_isp_sinfo_sensor_bind(sensor, id): a free sensor place of a slot with
 * this driver id, else a new slot (copy of the driver / data of the first
 * slot with the id, or none).  No free slot: nothing, 0.
 */
static inline int t32_tx_sinfo_sensor_bind(struct t32_tx_sinfo *si, u32 sensor, u32 id)
{
	u32 i, k, s, src = 0;

	if (!sensor)
		return -(int)T32_EINVAL;
	t32_tx_os_mutex_lock(si->lock);
	for (i = 0; i < T32_SINFO_SLOTS; i++) {
		s = t32_tx_sinfo_slot(si, i);
		if (t32_tx_sinfo_used(s) && t32_tx_ld(s, T32_SINFO_ID) == id && !t32_tx_ld(s, T32_SINFO_SENSOR)) {
			t32_tx_st(s, T32_SINFO_SENSOR, sensor);
			goto out;
		}
	}
	for (i = 0; i < T32_SINFO_SLOTS; i++)
		if (!t32_tx_sinfo_used(t32_tx_sinfo_slot(si, i)))
			break;
	if (i == T32_SINFO_SLOTS)
		goto out;
	for (k = 0; k < T32_SINFO_SLOTS; k++) {
		u32 o = t32_tx_sinfo_slot(si, k);

		if (t32_tx_sinfo_used(o) && t32_tx_ld(o, T32_SINFO_ID) == id) {
			src = o;
			break;
		}
	}
	s = t32_tx_sinfo_slot(si, i);
	t32_tx_sinfo_set_used(s, 1);
	t32_tx_st(s, T32_SINFO_DRV, src ? t32_tx_ld(src, T32_SINFO_DRV) : 0);
	t32_tx_st(s, T32_SINFO_ID, id);
	*(volatile u16 *)T32_TX_P(s + T32_SINFO_DATA) = src ? *(volatile u16 *)T32_TX_P(src + T32_SINFO_DATA) : 0;
	t32_tx_st(s, T32_SINFO_SENSOR, sensor);
	t32_tx_os_sinfo_slot_publish(s, i);
out:
	t32_tx_os_mutex_unlock(si->lock);
	return 0;
}

/* tx_isp_sinfo_sensor_unbind(sensor) */
static inline void t32_tx_sinfo_sensor_unbind(struct t32_tx_sinfo *si, u32 sensor)
{
	u32 i;

	if (!sensor)
		return;
	t32_tx_os_mutex_lock(si->lock);
	for (i = 0; i < T32_SINFO_SLOTS; i++) {
		u32 s = t32_tx_sinfo_slot(si, i);

		if (t32_tx_sinfo_used(s) && t32_tx_ld(s, T32_SINFO_SENSOR) == sensor)
			t32_tx_st(s, T32_SINFO_SENSOR, 0);
	}
	t32_tx_os_mutex_unlock(si->lock);
}

#endif
