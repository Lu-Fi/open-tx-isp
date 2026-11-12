/* Host test of driver/t31/tx_isp_t31_gpio.h (T31 second-sensor GPIO ioctls,
 * Lu-Fi/openimp#408).  Expected values are what the vendor tx-isp-t31.ko does
 * (see the header for the addresses) and what the vendor driver answered on
 * the T31 .26 run (audit/matrix1112/2ndsensor out-t31v-1534: EPERM before
 * AddSensor, 0 with the sensor running, empty lists touch no pin). */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t31/tx_isp_t31_gpio.h"

static int n_req, n_free, n_dir, req_fail_pin = -1, dir_fail_pin = -1;
static unsigned int last_req[T31_GPIO_MAX], last_free[T31_GPIO_MAX], last_dir[T31_GPIO_MAX];
static int last_val[T31_GPIO_MAX];
static char last_label[T31_GPIO_MAX][20];

static int t_req(unsigned int pin, const char *label)
{
	if (n_req < T31_GPIO_MAX) {
		last_req[n_req] = pin;
		snprintf(last_label[n_req], 20, "%s", label);
	}
	n_req++;
	return (int)pin == req_fail_pin ? -EBUSY : 0;
}
static void t_free(unsigned int pin) { if (n_free < T31_GPIO_MAX) last_free[n_free] = pin; n_free++; }
static int t_dir(unsigned int pin, int v)
{
	if (n_dir < T31_GPIO_MAX) { last_dir[n_dir] = pin; last_val[n_dir] = v; }
	n_dir++;
	return (int)pin == dir_fail_pin ? -EINVAL : 0;
}
static const struct t31_gpio_ops ops = { t_req, t_free, t_dir };

static void clear(void)
{
	n_req = n_free = n_dir = 0; req_fail_pin = dir_fail_pin = -1;
}

static void empty(struct t31_gpio_rec *r)
{
	int i;
	memset(r, 0, sizeof(*r));
	for (i = 0; i < T31_GPIO_MAX; i++) r->pin[i] = T31_GPIO_END;
}

int main(void)
{
	struct t31_gpio_rec r;
	struct t31_gpio_state st;

	assert(sizeof(r) == T31_GPIO_REC_SIZE && offsetof(struct t31_gpio_rec, free_flag) == 40);
	assert(TX_ISP_SET_GPIO_INIT_OR_FREE_CMD == 0xc00456e8U && TX_ISP_SET_GPIO_STA_CMD == 0xc00456e9U);
	assert(t31_gpio_cmd_event(TX_ISP_SET_GPIO_INIT_OR_FREE_CMD) == 0x2000017U);
	assert(t31_gpio_cmd_event(TX_ISP_SET_GPIO_STA_CMD) == 0x2000018U);

	/* gate: no device ENOENT, no sensor EPERM (vendor ph0), else go on (ph1) */
	assert(t31_gpio_ioctl_gate(0, 0) == -ENOENT);
	assert(t31_gpio_ioctl_gate(1, 0) == -EPERM);
	assert(t31_gpio_ioctl_gate(1, 1) == 0);

	/* empty lists (what the apitest sends): 0, no pin touched, init and free */
	t31_gpio_state_reset(&st); empty(&r); clear();
	assert(t31_gpio_vic_event(&st, 0x2000017, &r, &ops) == 0);
	r.free_flag = 1;
	assert(t31_gpio_vic_event(&st, 0x2000017, &r, &ops) == 0);
	assert(t31_gpio_vic_event(&st, 0x2000018, &r, &ops) == 0);
	assert(n_req == 0 && n_free == 0 && n_dir == 0);
	assert(st.switch_state == 1 && t31_gpio_frame_done(&st, &ops) == -1 && n_dir == 0 && st.switch_state == 0);

	/* init: request + direction_output only the listed pins, label per index */
	clear(); empty(&r);
	r.pin[0] = 18; r.value[0] = 1; r.pin[1] = 19; r.value[1] = 0;
	assert(t31_gpio_vic_event(&st, 0x2000017, &r, &ops) == 0);
	assert(n_req == 2 && n_dir == 2 && n_free == 0);
	assert(last_req[0] == 18 && last_req[1] == 19 && last_dir[1] == 19 && last_val[0] == 1 && last_val[1] == 0);
	assert(!strcmp(last_label[0], "vic_done_gpio0") && !strcmp(last_label[1], "vic_done_gpio1"));

	/* failed request: skip that pin, go on */
	clear(); req_fail_pin = 18;
	assert(t31_gpio_vic_event(&st, 0x2000017, &r, &ops) == 0);
	assert(n_req == 2 && n_dir == 1 && last_dir[0] == 19);

	/* failed direction_output: -1, later pins untouched */
	clear(); dir_fail_pin = 18;
	assert(t31_gpio_vic_event(&st, 0x2000017, &r, &ops) == -1);
	assert(n_req == 1 && n_dir == 1);

	/* all ten slots used: ten requests, no overrun */
	{ int i; clear(); empty(&r); for (i = 0; i < 10; i++) { r.pin[i] = 30 + i; r.value[i] = i & 1; }
	  assert(t31_gpio_vic_event(&st, 0x2000017, &r, &ops) == 0 && n_req == 10 && n_dir == 10); }

	/* free: release only the listed pins */
	clear(); empty(&r); r.pin[0] = 18; r.pin[1] = 19; r.free_flag = 1;
	assert(t31_gpio_vic_event(&st, 0x2000017, &r, &ops) == 0);
	assert(n_free == 2 && n_req == 0 && n_dir == 0 && last_free[0] == 18 && last_free[1] == 19);

	/* state: arms the switch and copies 42 bytes; frame done applies once */
	clear(); empty(&r); r.pin[0] = 18; r.value[0] = 1; r.pin[1] = 19; r.value[1] = 0;
	assert(t31_gpio_vic_event(&st, 0x2000018, &r, &ops) == 0 && st.switch_state == 1);
	assert(!memcmp(&st.info, &r, 42));
	assert(t31_gpio_frame_done(&st, &ops) == -1 && n_dir == 2 && last_val[0] == 1);
	assert(t31_gpio_frame_done(&st, &ops) == -1 && n_dir == 2);   /* flag cleared */
	/* a failing pin is reported, the flag is cleared anyway */
	clear(); dir_fail_pin = 19;
	t31_gpio_set_state(&st, &r);
	assert(t31_gpio_frame_done(&st, &ops) == 19 && st.switch_state == 0);

	/* other events and NULL are not ours */
	clear();
	assert(t31_gpio_vic_event(&st, 0x2000016, &r, &ops) == 0 && t31_gpio_vic_event(&st, 0x2000017, NULL, &ops) == 0);
	assert(n_req == 0 && n_dir == 0);

	puts("tx_isp_t31_gpio_test: ok");
	return 0;
}
