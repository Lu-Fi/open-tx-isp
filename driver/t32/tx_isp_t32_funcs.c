// SPDX-License-Identifier: GPL-2.0
/*
 * T32: kernel-API wrappers exported to the prebuilt sensor modules.
 *
 * T32 sensor modules (built from the T32 4.4.94 SDK headers) import these
 * names instead of the kernel symbols.  They are thin wrappers; the set is
 * the one the T41 driver exports, minus the T41 tuning-math helpers (no T32
 * sensor module seen so far imports them).
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/capability.h>

#include "tx_isp_t32.h"

int t32_print_level = 1;
module_param_named(print_level, t32_print_level, int, 0644);
MODULE_PARM_DESC(print_level, "isp_printf threshold: 0 info, 1 warning, 2 error");

int isp_printf(unsigned int level, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int r = 0;

	if ((int)level < t32_print_level)
		return 0;
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	r = printk("%pV", &vaf);
	va_end(args);
	return r;
}
EXPORT_SYMBOL(isp_printf);

int private_capable(int cap)
{
	return capable(cap);
}
EXPORT_SYMBOL(private_capable);

void private_msleep(unsigned int msecs)
{
	msleep(msecs);
}
EXPORT_SYMBOL(private_msleep);

void private_mdelay(unsigned int msecs)
{
	mdelay(msecs);
}
EXPORT_SYMBOL(private_mdelay);

/* clocks */
struct clk *private_clk_get(struct device *dev, const char *id)
{
	return clk_get(dev, id);
}
EXPORT_SYMBOL(private_clk_get);

struct clk *private_devm_clk_get(struct device *dev, const char *id)
{
	return devm_clk_get(dev, id);
}
EXPORT_SYMBOL(private_devm_clk_get);

void private_clk_put(struct clk *clk)
{
	clk_put(clk);
}
EXPORT_SYMBOL(private_clk_put);

void private_devm_clk_put(struct device *dev, struct clk *clk)
{
	devm_clk_put(dev, clk);
}
EXPORT_SYMBOL(private_devm_clk_put);

int private_clk_prepare_enable(struct clk *clk)
{
	return clk_prepare_enable(clk);
}
EXPORT_SYMBOL(private_clk_prepare_enable);

void private_clk_disable_unprepare(struct clk *clk)
{
	clk_disable_unprepare(clk);
}
EXPORT_SYMBOL(private_clk_disable_unprepare);

unsigned long private_clk_get_rate(struct clk *clk)
{
	return clk_get_rate(clk);
}
EXPORT_SYMBOL(private_clk_get_rate);

int private_clk_set_rate(struct clk *clk, unsigned long rate)
{
	return clk_set_rate(clk, rate);
}
EXPORT_SYMBOL(private_clk_set_rate);

/* gpio */
int private_gpio_request(unsigned int gpio, const char *label)
{
	return gpio_request(gpio, label);
}
EXPORT_SYMBOL(private_gpio_request);

void private_gpio_free(unsigned int gpio)
{
	if (gpio_is_valid(gpio))
		gpio_free(gpio);
}
EXPORT_SYMBOL(private_gpio_free);

int private_gpio_direction_output(unsigned int gpio, int value)
{
	return gpio_direction_output(gpio, value);
}
EXPORT_SYMBOL(private_gpio_direction_output);

int private_gpio_direction_input(unsigned int gpio)
{
	return gpio_direction_input(gpio);
}
EXPORT_SYMBOL(private_gpio_direction_input);

int private_gpio_set_debounce(unsigned int gpio, unsigned int debounce)
{
	return gpio_set_debounce(gpio, debounce);
}
EXPORT_SYMBOL(private_gpio_set_debounce);

extern int jzgpio_set_func(int port, int func, unsigned long pins);

int private_jzgpio_set_func(int port, int func, unsigned long pins)
{
	return jzgpio_set_func(port, func, pins);
}
EXPORT_SYMBOL(private_jzgpio_set_func);

/* i2c */
int private_i2c_add_driver(struct i2c_driver *drv)
{
	/* keep the sensor module as the driver owner (not this module) */
	return i2c_register_driver(drv->driver.owner ? drv->driver.owner : THIS_MODULE, drv);
}
EXPORT_SYMBOL(private_i2c_add_driver);

void private_i2c_del_driver(struct i2c_driver *drv)
{
	t32_core_sensor_driver_gone(drv);
	i2c_del_driver(drv);
}
EXPORT_SYMBOL(private_i2c_del_driver);

int private_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	return i2c_transfer(adap, msgs, num);
}
EXPORT_SYMBOL(private_i2c_transfer);

void *private_i2c_get_clientdata(const struct i2c_client *client)
{
	return i2c_get_clientdata(client);
}
EXPORT_SYMBOL(private_i2c_get_clientdata);

void private_i2c_set_clientdata(struct i2c_client *client, void *data)
{
	i2c_set_clientdata(client, data);
}
EXPORT_SYMBOL(private_i2c_set_clientdata);
