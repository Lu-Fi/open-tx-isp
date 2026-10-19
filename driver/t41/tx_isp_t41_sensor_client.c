/*
 * T41: bookkeeping of the I2C clients tx-isp creates for registered sensors.
 *
 * The stock driver creates the sensor's i2c_client in TX_ISP_SENSOR_REGISTER
 * (isp_i2c_new_subdev_board) and only destroys it in TX_ISP_SENSOR_RELEASE,
 * which the application must call (IMP_ISP_DelSensor).  A streamer that is
 * killed, or an rmmod of the driver, leaves that client on the adapter:
 * the next TX_ISP_SENSOR_REGISTER fails in i2c_new_device() with -EBUSY
 * ("Failed to register i2c client sc5336 at 0x30 (-16)") until reboot.
 *
 * This file keeps a small table of those clients outside the sensor object
 * (which the sensor module frees in its i2c remove) so that
 *  - the module exit releases every still-registered sensor like the stock
 *    TX_ISP_SENSOR_RELEASE would (tx_isp_remove),
 *  - a sensor module unbound while registered (rmmod of the sensor before
 *    the ISP) is dropped from the VIN list before its object is freed
 *    (I2C bus notifier, BUS_NOTIFY_UNBIND_DRIVER), the orphaned client is
 *    released at the next register or at module exit,
 *  - a foreign client that still occupies the address (left by a previous
 *    driver instance) is evicted before i2c_new_device().
 *
 * Kept out of the recovered monolith so its layout-sensitive BSS stays
 * unchanged.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/printk.h>

#include "tx_isp_t41_sensor_client.h"

#define T41_SENSOR_CLIENT_MAX	4

struct t41_sensor_client {
	struct i2c_client *client;	/* NULL: slot free */
	struct i2c_adapter *adapter;	/* reference held by the register path */
	void *sensor_sd;		/* NULL: client orphaned (object freed) */
};

static struct t41_sensor_client t41_sensor_clients[T41_SENSOR_CLIENT_MAX];
static DEFINE_MUTEX(t41_sensor_client_lock);
static tx_isp_t41_sensor_detach_fn t41_sensor_detach;
static bool t41_sensor_client_nb_registered;

static struct t41_sensor_client *t41_sensor_client_find(struct i2c_client *c)
{
	unsigned int i;

	for (i = 0; i < T41_SENSOR_CLIENT_MAX; i++)
		if (t41_sensor_clients[i].client == c)
			return &t41_sensor_clients[i];
	return NULL;
}

int tx_isp_t41_sensor_client_add(struct i2c_client *client,
				 struct i2c_adapter *adapter, void *sensor_sd)
{
	struct t41_sensor_client *e;

	if (!client || !sensor_sd)
		return -EINVAL;
	mutex_lock(&t41_sensor_client_lock);
	e = t41_sensor_client_find(client);
	if (!e)
		e = t41_sensor_client_find(NULL);
	if (!e) {
		mutex_unlock(&t41_sensor_client_lock);
		pr_warn("t41: sensor client table full (%s 0x%02x)\n",
			client->name, client->addr);
		return -ENOSPC;
	}
	e->client = client;
	e->adapter = adapter;
	e->sensor_sd = sensor_sd;
	mutex_unlock(&t41_sensor_client_lock);
	return 0;
}

/* The caller is about to i2c_unregister_device(client) itself. */
void tx_isp_t41_sensor_client_forget(struct i2c_client *client)
{
	struct t41_sensor_client *e;

	mutex_lock(&t41_sensor_client_lock);
	e = t41_sensor_client_find(client);
	if (e)
		memset(e, 0, sizeof(*e));
	mutex_unlock(&t41_sensor_client_lock);
}

static int t41_sensor_client_match_addr(struct device *dev, void *data)
{
	struct i2c_client *c = i2c_verify_client(dev);
	unsigned short addr = *(unsigned short *)data;

	return c && c->addr == addr;
}

/*
 * Make <adapter, addr> free for i2c_new_device().  A client of ours is
 * unregistered normally (its sensor object was already detached: the slot is
 * an orphan, or the caller is re-registering the same address, which the
 * VIN state check already proved unused); a client we do not know is left
 * behind by a previous driver instance and evicted with a warning.
 */
int tx_isp_t41_sensor_client_evict(struct i2c_adapter *adapter,
				   unsigned short addr)
{
	struct device *dev;
	struct i2c_client *c;
	struct t41_sensor_client *e;
	struct i2c_adapter *held = NULL;

	if (!adapter || !addr)
		return 0;
	dev = device_find_child(&adapter->dev, &addr,
				t41_sensor_client_match_addr);
	if (!dev)
		return 0;
	c = i2c_verify_client(dev);
	mutex_lock(&t41_sensor_client_lock);
	e = t41_sensor_client_find(c);
	if (e) {
		held = e->adapter;
		memset(e, 0, sizeof(*e));
	}
	mutex_unlock(&t41_sensor_client_lock);
	pr_warn("t41: evicting %s i2c client %s at %d-%04x before register\n",
		e ? "orphaned" : "stale", c->name, i2c_adapter_id(adapter),
		c->addr);
	put_device(dev);
	i2c_unregister_device(c);
	if (held)
		i2c_put_adapter(held);
	return 1;
}

/* Module exit: release every sensor still registered (stock SENSOR_RELEASE). */
void tx_isp_t41_sensor_clients_release_all(void)
{
	unsigned int i;

	for (i = 0; i < T41_SENSOR_CLIENT_MAX; i++) {
		struct t41_sensor_client e;

		mutex_lock(&t41_sensor_client_lock);
		e = t41_sensor_clients[i];
		memset(&t41_sensor_clients[i], 0, sizeof(e));
		mutex_unlock(&t41_sensor_client_lock);
		if (!e.client)
			continue;
		pr_info("t41: releasing %s sensor client %s at 0x%02x on unload\n",
			e.sensor_sd ? "registered" : "orphaned",
			e.client->name, e.client->addr);
		if (e.sensor_sd && t41_sensor_detach)
			t41_sensor_detach(e.sensor_sd);
		i2c_unregister_device(e.client);
		if (e.adapter)
			i2c_put_adapter(e.adapter);
	}
}

/*
 * I2C bus events.  UNBIND_DRIVER: the sensor driver is about to run its
 * remove (kfree of the sensor object incl. the subdev and the VIN list node)
 * while the device lock is held, so detach the object now and keep the
 * client as an orphan; it cannot be unregistered from here.  DEL_DEVICE:
 * someone else (sysfs delete_device, adapter removal) destroys the client.
 */
static int t41_sensor_client_bus_notify(struct notifier_block *nb,
					unsigned long action, void *data)
{
	struct i2c_client *c = i2c_verify_client(data);
	struct t41_sensor_client *e;
	void *sd = NULL;
	struct i2c_adapter *held = NULL;

	(void)nb;
	if (!c)
		return NOTIFY_DONE;
	if (action != BUS_NOTIFY_UNBIND_DRIVER && action != BUS_NOTIFY_DEL_DEVICE)
		return NOTIFY_DONE;
	mutex_lock(&t41_sensor_client_lock);
	e = t41_sensor_client_find(c);
	if (e) {
		sd = e->sensor_sd;
		e->sensor_sd = NULL;
		if (action == BUS_NOTIFY_DEL_DEVICE) {
			held = e->adapter;
			memset(e, 0, sizeof(*e));
		}
	}
	mutex_unlock(&t41_sensor_client_lock);
	if (!e)
		return NOTIFY_DONE;
	pr_warn("t41: sensor client %s at 0x%02x %s while registered%s\n",
		c->name, c->addr,
		action == BUS_NOTIFY_UNBIND_DRIVER ? "unbound" : "deleted",
		sd ? ", detaching it from the VIN" : "");
	if (sd && t41_sensor_detach)
		t41_sensor_detach(sd);
	if (held)
		i2c_put_adapter(held);
	return NOTIFY_OK;
}

static struct notifier_block t41_sensor_client_nb = {
	.notifier_call = t41_sensor_client_bus_notify,
};

int tx_isp_t41_sensor_clients_init(tx_isp_t41_sensor_detach_fn detach)
{
	int ret;

	t41_sensor_detach = detach;
	memset(t41_sensor_clients, 0, sizeof(t41_sensor_clients));
	ret = bus_register_notifier(&i2c_bus_type, &t41_sensor_client_nb);
	if (ret) {
		pr_warn("t41: i2c bus notifier not registered (%d)\n", ret);
		return ret;
	}
	t41_sensor_client_nb_registered = true;
	return 0;
}

void tx_isp_t41_sensor_clients_exit(void)
{
	tx_isp_t41_sensor_clients_release_all();
	if (t41_sensor_client_nb_registered)
		bus_unregister_notifier(&i2c_bus_type, &t41_sensor_client_nb);
	t41_sensor_client_nb_registered = false;
	t41_sensor_detach = NULL;
}
