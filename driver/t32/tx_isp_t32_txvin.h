/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 VIN (sensor input) subdevice glue: vendor tx_isp_vin_init, _reset,
 * _activate_subdev, _slake_subdev, _prepare_module, _suspend_module,
 * _resume_module, _complete_module.
 *
 * VIN device (offsets the vendor uses):
 *   +152 notify (sd, event, msg), +288 sensor list head (next, prev; a
 *   sensor's list node is at +304, its id at +292), +296 + 4 i active
 *   sensor of input i, +312 mutex, +324 + 12 i per input mutex,
 *   +372 + 4 i input state (1 init, 2 active, 3 re-init, 4 streaming),
 *   +388 activation count, +392 slaked flag.
 * Control argument of init / reset: { +0 enable, +4 input }.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txvin_oracle): hook and callback calls
 * with their arguments and message words, prints, VIN / sensor bytes,
 * return values.
 */
#ifndef TX_ISP_T32_TXVIN_H
#define TX_ISP_T32_TXVIN_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txcore.h"

#define T32_VIN_NOTIFY		152U
#define T32_VIN_LIST		288U
#define T32_VIN_SENSOR(i)	(296U + 4U * (i))
#define T32_VIN_MUTEX		312U
#define T32_VIN_IN_MUTEX(i)	(324U + 12U * (i))
#define T32_VIN_STATE(i)	(372U + 4U * (i))
#define T32_VIN_INPUTS		4U
#define T32_VIN_ACTIVE		388U
#define T32_VIN_SLAKED		392U

#define T32_SNS_ID		292U
#define T32_SNS_NODE		304U	/* list node: +0 next, +4 prev */

#define T32_VIN_ST_INIT		1U
#define T32_VIN_ST_ACTIVE	2U
#define T32_VIN_ST_REINIT	3U
#define T32_VIN_ST_STREAM	4U

#define T32_CORE_INIT		4U	/* sensor core ops +4 init, +8 reset */
#define T32_CORE_RESET		8U

#define T32_VIN_EV_STATE	0x200001bU	/* notify: { id, state, 0 } */
#define T32_VIN_PM_SUSPEND	1U
#define T32_VIN_PM_RESUME	2U
#define T32_VIN_PM_COMPLETE	3U
#define T32_VIN_AOV_EVENT	5U		/* isp_core_event_push: 28 byte message, +8 = 5, +12 = vin */

typedef int (*t32_tx_ctl_fn)(u32 sd, u32 ctl);
typedef int (*t32_tx_vnotify_fn)(u32 sd, u32 event, void *msg);

/* provided by the driver glue (kernel) or by the oracle (host) */
void t32_tx_os_mutex_lock(u32 m);
void t32_tx_os_mutex_unlock(u32 m);
int t32_tx_os_core_event_push(u32 a0, void *msg);
int t32_tx_os_vin_set_stream(u32 vin, void *ctl, u32 on);
int t32_tx_os_sensor_set_input(u32 vin, void *ctl);
int t32_tx_os_sensor_release_all(u32 vin);

/* sensor core op (init / reset) under the input's mutex; -ENOIOCTLCMD counts as success */
static inline int t32_tx_vin_core_op(u32 vin, u32 ctl, u32 sns, u32 op)
{
	u32 core;
	int r = -(int)T32_ENOIOCTLCMD;

	t32_tx_os_mutex_lock(vin + T32_VIN_IN_MUTEX(t32_tx_ld(ctl, 4)));
	core = t32_tx_ld(t32_tx_ld(sns, T32_SD_OPS), T32_OPS_CORE);
	if (core && t32_tx_ld(core, op))
		r = ((t32_tx_ctl_fn)T32_TX_P(t32_tx_ld(core, op)))(sns, ctl);
	t32_tx_os_mutex_unlock(vin + T32_VIN_IN_MUTEX(t32_tx_ld(ctl, 4)));
	return r == -(int)T32_ENOIOCTLCMD ? 0 : r;
}

/* tx_isp_vin_init(vin, ctl): sensor init, then the input state 3 (enable) or 2 */
static inline int t32_tx_vin_init(u32 vin, u32 ctl)
{
	u32 sns = t32_tx_ld(vin, T32_VIN_SENSOR(t32_tx_ld(ctl, 4)));
	int r;

	if (sns) {
		r = t32_tx_vin_core_op(vin, ctl, sns, T32_CORE_INIT);
	} else {
		t32_tx_os_isp_printf(1, "[ %s:%d ] Don't have active sensor!\n", "tx_isp_vin_init", 537);
		r = -1;
	}
	t32_tx_st(vin, T32_VIN_STATE(t32_tx_ld(ctl, 4)),
		  t32_tx_ld(ctl, 0) ? T32_VIN_ST_REINIT : T32_VIN_ST_ACTIVE);
	return r;
}

/* tx_isp_vin_reset(vin, ctl).  Vendor: the "no sensor" print passes the line number 561 as its %s */
static inline int t32_tx_vin_reset(u32 vin, u32 ctl)
{
	u32 sns = t32_tx_ld(vin, T32_VIN_SENSOR(t32_tx_ld(ctl, 4)));

	if (!sns) {
		t32_tx_os_isp_printf(1, "[ %s:%d ] Don't have active sensor!\n", (const char *)T32_TX_P(561), 561);
		return -1;
	}
	return t32_tx_vin_core_op(vin, ctl, sns, T32_CORE_RESET);
}

/* tx_isp_vin_activate_subdev: inputs in state 1 become 2 */
static inline int t32_tx_vin_activate_subdev(u32 vin)
{
	u32 i;

	t32_tx_os_mutex_lock(vin + T32_VIN_MUTEX);
	for (i = 0; i < T32_VIN_INPUTS; i++)
		if (t32_tx_ld(vin, T32_VIN_STATE(i)) == T32_VIN_ST_INIT)
			t32_tx_st(vin, T32_VIN_STATE(i), T32_VIN_ST_ACTIVE);
	t32_tx_os_mutex_unlock(vin + T32_VIN_MUTEX);
	t32_tx_st(vin, T32_VIN_ACTIVE, t32_tx_ld(vin, T32_VIN_ACTIVE) + 1);
	return 0;
}

/* tx_isp_vin_slake_subdev: on the last release stop / re-init every input, drop the sensors */
static inline int t32_tx_vin_slake_subdev(u32 vin)
{
	u32 ctl[2], i, st;

	if (t32_tx_ld(vin, T32_VIN_ACTIVE))
		t32_tx_st(vin, T32_VIN_ACTIVE, t32_tx_ld(vin, T32_VIN_ACTIVE) - 1);
	if (!t32_tx_ld(vin, T32_VIN_ACTIVE)) {
		for (i = 0; i < T32_VIN_INPUTS; i++) {
			ctl[0] = 0;
			ctl[1] = i;
			if (t32_tx_ld(vin, T32_VIN_STATE(i)) == T32_VIN_ST_STREAM)
				t32_tx_os_vin_set_stream(vin, ctl, 1);
			if (t32_tx_ld(vin, T32_VIN_STATE(i)) == T32_VIN_ST_REINIT)
				t32_tx_vin_init(vin, T32_TX_A(ctl));
			if (t32_tx_ld(vin, T32_VIN_SENSOR(i))) {
				ctl[0] = 0;
				ctl[1] = i;
				t32_tx_os_sensor_set_input(vin, ctl);
			}
		}
		t32_tx_os_mutex_lock(vin + T32_VIN_MUTEX);
		if (t32_tx_ld(vin, T32_VIN_LIST) != vin + T32_VIN_LIST)
			t32_tx_os_sensor_release_all(vin);
		for (i = 0; i < T32_VIN_INPUTS; i++) {
			st = t32_tx_ld(vin, T32_VIN_STATE(i));
			if (st == T32_VIN_ST_ACTIVE)
				t32_tx_st(vin, T32_VIN_STATE(i), T32_VIN_ST_INIT);
		}
		t32_tx_os_mutex_unlock(vin + T32_VIN_MUTEX);
	}
	t32_tx_st(vin, T32_VIN_SLAKED, 1);
	return 0;
}

static inline int t32_tx_vin_ok(u32 vin)
{
	return vin && !T32_IS_ERR(vin);
}

/* one state message to the VIN notify callback per sensor */
static inline int t32_tx_vin_state_msg(u32 vin, u32 sns, u32 state)
{
	u32 msg[3] = { t32_tx_ld(sns, T32_SNS_ID), state, 0 }, fn = t32_tx_ld(vin, T32_VIN_NOTIFY);

	if (!fn)
		return -1;
	return ((t32_tx_vnotify_fn)T32_TX_P(fn))(vin, T32_VIN_EV_STATE, msg);
}

/* tx_isp_vin_resume_module: every sensor (list order) gets state 2 */
static inline int t32_tx_vin_resume_module(u32 vin)
{
	u32 n;

	if (!t32_tx_vin_ok(vin)) {
		t32_tx_os_isp_printf(2, "\n [%s %d] Vin is null \n", "tx_isp_vin_resume_module", 907);
		return -(int)T32_EINVAL;
	}
	t32_tx_os_isp_printf(0, "[%s %d] Enter the resume ... \n", "tx_isp_vin_resume_module", 911);
	for (n = t32_tx_ld(vin, T32_VIN_LIST); n != vin + T32_VIN_LIST; n = t32_tx_ld(n, 0))
		if (t32_tx_vin_state_msg(vin, n - T32_SNS_NODE, T32_VIN_PM_RESUME)) {
			t32_tx_os_isp_printf(2, "[%s %d] Enter the resume not found\n", "tx_isp_vin_resume_module", 927);
			return -(int)T32_EINVAL;
		}
	return 0;
}

/* tx_isp_vin_suspend_module: every sensor (reverse list order) gets state 1 */
static inline int t32_tx_vin_suspend_module(u32 vin)
{
	u32 n;

	if (!t32_tx_vin_ok(vin)) {
		t32_tx_os_isp_printf(2, "\n [%s %d] Vin is null \n", "tx_isp_vin_suspend_module", 861);
		return -(int)T32_EINVAL;
	}
	for (n = t32_tx_ld(vin, T32_VIN_LIST + 4); n != vin + T32_VIN_LIST; n = t32_tx_ld(n, 4))
		if (t32_tx_vin_state_msg(vin, n - T32_SNS_NODE, T32_VIN_PM_SUSPEND)) {
			t32_tx_os_isp_printf(2, "[%s %d] Enter the suspend not found\n", "tx_isp_vin_suspend_module", 877);
			return -(int)T32_EINVAL;
		}
	return 0;
}

/* tx_isp_vin_complete_module: state 3 to every sensor, results ignored (the vendor returns no value) */
static inline void t32_tx_vin_complete_module(u32 vin)
{
	u32 n;

	if (!t32_tx_vin_ok(vin)) {
		t32_tx_os_isp_printf(2, "\n [%s %d] Vin NULL \n", "tx_isp_vin_complete_module", 956);
		return;
	}
	for (n = t32_tx_ld(vin, T32_VIN_LIST); n != vin + T32_VIN_LIST; n = t32_tx_ld(n, 0))
		if (t32_tx_ld(vin, T32_VIN_NOTIFY))
			t32_tx_vin_state_msg(vin, n - T32_SNS_NODE, T32_VIN_PM_COMPLETE);
}

/* tx_isp_vin_prepare_module: one AOV event per sensor */
static inline int t32_tx_vin_prepare_module(u32 vin)
{
	u32 n, msg[7];

	if (!t32_tx_vin_ok(vin)) {
		t32_tx_os_isp_printf(2, "\n [%s %d] Vin NULL \n", "tx_isp_vin_prepare_module", 828);
		return -(int)T32_EINVAL;
	}
	for (n = t32_tx_ld(vin, T32_VIN_LIST); n != vin + T32_VIN_LIST; n = t32_tx_ld(n, 0)) {
		memset(msg, 0, sizeof(msg));
		msg[2] = T32_VIN_AOV_EVENT;
		msg[3] = vin;
		if (t32_tx_os_core_event_push(0, msg)) {
			t32_tx_os_isp_printf(2, "[%s %d] AOV push event error\n", "tx_isp_vin_prepare_module", 843);
			return -(int)T32_EINVAL;
		}
	}
	t32_tx_os_isp_printf(0, "[%s %d] Enter the prepare ... \n", "tx_isp_vin_prepare_module", 848);
	return 0;
}

#endif
