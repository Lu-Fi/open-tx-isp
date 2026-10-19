/* T41: I2C clients created for registered sensors (tx_isp_t41_sensor_client.c). */
#ifndef TX_ISP_T41_SENSOR_CLIENT_H
#define TX_ISP_T41_SENSOR_CLIENT_H

struct i2c_client;
struct i2c_adapter;

/* Drops a still-valid sensor object from the VIN list and the core/VIC copies. */
typedef void (*tx_isp_t41_sensor_detach_fn)(void *sensor_sd);

int tx_isp_t41_sensor_clients_init(tx_isp_t41_sensor_detach_fn detach);
void tx_isp_t41_sensor_clients_exit(void);
void tx_isp_t41_sensor_clients_release_all(void);
int tx_isp_t41_sensor_client_add(struct i2c_client *client,
				 struct i2c_adapter *adapter, void *sensor_sd);
void tx_isp_t41_sensor_client_forget(struct i2c_client *client);
int tx_isp_t41_sensor_client_evict(struct i2c_adapter *adapter,
				   unsigned short addr);

#endif
