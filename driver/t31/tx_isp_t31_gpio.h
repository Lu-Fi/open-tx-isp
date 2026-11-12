/* SPDX-License-Identifier: GPL-2.0 */
#ifndef TX_ISP_T31_GPIO_H
#define TX_ISP_T31_GPIO_H

/*
 * T31 second-sensor GPIO ioctls as in the stock tx-isp-t31 module
 * (Lu-Fi/openimp#408).  Vendor addresses (tx-isp-t31.ko, relocatable):
 *   tx_isp_unlocked_ioctl 0xdccc: case 0xc00456e8 (SET_GPIO_INIT_OR_FREE,
 *     0xecbc) and 0xc00456e9 (SET_GPIO_STA, 0xed10) copy 42 bytes from user
 *     (-EFAULT), then call the device notify hook (tx_isp_notify, 0xd264)
 *     with event 0x2000017 / 0x2000018.
 *   subdev_sensor_ops_ioctl (VIN, 0x421c) forwards events >= 0x2000013 to the
 *     attached sensor and answers -1 (EPERM) when no sensor is attached.
 *   vic_sensor_ops_ioctl (0x115c): 0x2000017 at 0x121c (record flag u16 at
 *     +40 == 0: request + direction_output the pins, != 0: free them),
 *     0x2000018 at 0x12e4 (gpio_switch_state = 1, memcpy(gpio_info, rec, 42)).
 *   vic_framedone_irq_function (0x23ac, GPIO part from 0x242c): when the state
 *     flag is set it clears it and direction_output()s every listed pin.
 * Record: u16 pin[10] (0xff ends the list), u16 value[10], u16 free_flag.
 * Only the pins the caller lists are touched; an empty list (0xff first)
 * touches none.  Plain data and an ops table so the host test can run it.
 */
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef uint16_t u16;
typedef uint32_t u32;
#endif

#define TX_ISP_SET_GPIO_INIT_OR_FREE_CMD 0xc00456e8U
#define TX_ISP_SET_GPIO_STA_CMD 0xc00456e9U
#define TX_ISP_EVENT_GPIO_INIT_OR_FREE 0x2000017U
#define TX_ISP_EVENT_GPIO_STATE 0x2000018U
#define T31_GPIO_MAX 10
#define T31_GPIO_END 0xffU
#define T31_GPIO_REC_SIZE 42

struct t31_gpio_rec {
	u16 pin[T31_GPIO_MAX];
	u16 value[T31_GPIO_MAX];
	u16 free_flag;
};

struct t31_gpio_ops {
	int (*request)(unsigned int pin, const char *label);
	void (*release)(unsigned int pin);
	int (*direction_output)(unsigned int pin, int value);
};

struct t31_gpio_state {
	int switch_state;
	struct t31_gpio_rec info;
};

static inline void t31_gpio_state_reset(struct t31_gpio_state *st)
{
	int i;

	st->switch_state = 0;
	memset(&st->info, 0, sizeof(st->info));
	for (i = 0; i < T31_GPIO_MAX; i++)
		st->info.pin[i] = T31_GPIO_END;
}

/* vic_sensor_ops_ioctl 0x2000017: free_flag 0 = init, else free.  Init: a
 * failed request skips the pin (stock goes on), a failed direction_output
 * stops with -1 (stock does not release the pins already taken). */
static inline int t31_gpio_init_or_free(const struct t31_gpio_rec *rec,
					const struct t31_gpio_ops *ops)
{
	char label[20];
	int i;

	if (rec->free_flag == 0) {
		for (i = 0; i < T31_GPIO_MAX; i++) {
			if (rec->pin[i] == T31_GPIO_END)
				return 0;
			snprintf(label, sizeof(label), "vic_done_gpio%d", i);
			if (ops->request(rec->pin[i], label) != 0)
				continue;
			if (ops->direction_output(rec->pin[i], rec->value[i]) < 0)
				return -1;
		}
		return 0;
	}
	for (i = 0; i < T31_GPIO_MAX; i++) {
		if (rec->pin[i] == T31_GPIO_END)
			return 0;
		ops->release(rec->pin[i]);
	}
	return 0;
}

/* vic_sensor_ops_ioctl 0x2000018: arm the frame-done GPIO switch. */
static inline int t31_gpio_set_state(struct t31_gpio_state *st,
				     const struct t31_gpio_rec *rec)
{
	st->switch_state = 1;
	memcpy(&st->info, rec, T31_GPIO_REC_SIZE);
	return 0;
}

/* VIC subdev sensor-ioctl events; 0 for the events it does not own. */
static inline int t31_gpio_vic_event(struct t31_gpio_state *st, unsigned int ev,
				     const struct t31_gpio_rec *rec,
				     const struct t31_gpio_ops *ops)
{
	if (!rec)
		return 0;
	if (ev == TX_ISP_EVENT_GPIO_INIT_OR_FREE)
		return t31_gpio_init_or_free(rec, ops);
	if (ev == TX_ISP_EVENT_GPIO_STATE)
		return t31_gpio_set_state(st, rec);
	return 0;
}

/* Frame-done part: returns the pin that failed (>= 0) or -1 when all fine. */
static inline int t31_gpio_frame_done(struct t31_gpio_state *st,
				      const struct t31_gpio_ops *ops)
{
	int i;

	if (!st->switch_state)
		return -1;
	st->switch_state = 0;
	for (i = 0; i < T31_GPIO_MAX; i++) {
		if (st->info.pin[i] == T31_GPIO_END)
			break;
		if (ops->direction_output(st->info.pin[i], st->info.value[i]) < 0)
			return st->info.pin[i];
	}
	return -1;
}

/* Device ioctl 0xc00456e8/e9 result before the notify walk: stock answers
 * -ENOENT without a device, -EPERM when no sensor is attached (VIN). */
static inline int t31_gpio_ioctl_gate(int have_dev, int have_sensor)
{
	if (!have_dev)
		return -ENOENT;
	if (!have_sensor)
		return -EPERM;
	return 0;
}

static inline unsigned int t31_gpio_cmd_event(unsigned int cmd)
{
	return cmd == TX_ISP_SET_GPIO_INIT_OR_FREE_CMD ?
		TX_ISP_EVENT_GPIO_INIT_OR_FREE : TX_ISP_EVENT_GPIO_STATE;
}

#endif
