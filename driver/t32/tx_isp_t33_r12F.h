/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 libisp entry points (openimp #99): firmware process forwarder, IPC output position, ISP / IPC memory
 * space get / set, sensor attribute sync and the MSCA channel wrappers of tx-isp-t33.ko.  The callees
 * (ported elsewhere or hardware) are reached through struct t33_r12f_ops.
 * Proven by tests/t33_binoracle block "r12F".
 */
#ifndef TX_ISP_T33_R12F_H
#define TX_ISP_T33_R12F_H

#include "tx_isp_t33_blk.h"

#define T33_IPC_OUTPUT_POS_REG	0x30200U
#define T33_MSCA_EN_REG		0xf0040U
#define T33_PAR_SIZE		1008U	/* stIspInitPar channel block */
#define T33_PAR_NSENS		904U
#define T33_PAR_LDC		44U
#define T33_MEM_SIZE		76U	/* stMemInfo channel block */
#define T33_SATTR_SIZE		232U	/* stSensorAttr channel block */

struct t33_r12f_ops {
	u32 (*event_process)(u32 ch);				/* tisp_event_process */
	u32 (*reg_read)(u32 reg);				/* system_reg_read */
	u32 (*reg_write)(u32 reg, u32 val);			/* system_reg_write */
	u32 (*top_ipc_mem_size)(u32 w, u32 cfg0);		/* tisp_top_ipc_get_mem_size */
	u32 (*ldc_mem_size)(u32 ch, u32 w0, u32 w1);		/* lib_tisp_ldc_get_mem_size */
	u32 (*mdns_t33_mem_size)(u32 ch, u32 w, u32 h, u32 mode, u32 pad);	/* tisp_mdns_t33_get_mem_size */
	u32 (*mdns_mem_size)(u32 ch, u32 w, u32 h, u32 mode, u32 pad);		/* tisp_mdns_get_mem_size */
	u32 (*top_ipc_set_cfg)(u32 base);			/* tisp_top_ipc_set_mem_cfg */
	u32 (*ldc_cfg_dma)(u32 ch, u32 attr);			/* tisp_ldc_cfg_dma */
	u32 (*mdns_t33_set_cfg)(u32 ch, u32 a1, u32 a2, u32 a3);	/* tisp_mdns_t33_set_mem_cfg */
	u32 (*mdns_set_cfg)(u32 ch, u32 a1, u32 a2, u32 a3);	/* tisp_mdns_set_mem_cfg */
	u32 (*ae_sensor_trig)(u32 ch);				/* tisp_ae_par_sensor_trig */
	u32 (*msca_attr_set)(u32 ch, u32 a1, u32 a2, u32 a3);	/* tisp_msca_t33_channel_attr_set */
	u32 (*msca_off)(u32 ch);				/* tisp_msca_t33_api_chx_stream_off */
	u32 (*msca_on)(u32 ch);					/* tisp_msca_t33_api_chx_stream_on */
};

static inline void t33_r12f_put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

static inline void t33_r12f_cp32(u8 *d, const u8 *s)
{
	d[0] = s[0];
	d[1] = s[1];
	d[2] = s[2];
	d[3] = s[3];
}

/* lib_tisp_fw_process */
static inline u32 t33_lib_fw_process(const struct t33_r12f_ops *o, u32 ch)
{
	return o->event_process(ch & 0xff);
}

/* lib_tisp_get_ipc_output_pos */
static inline u32 t33_lib_get_ipc_output_pos(const struct t33_r12f_ops *o)
{
	return o->reg_read(T33_IPC_OUTPUT_POS_REG);
}

/* lib_tisp_get_isp_ipc_memory_space: par = stIspInitPar, meminfo = stMemInfo, ldc = LdcInfo */
static inline u32 t33_lib_get_ipc_mem(const struct t33_r12f_ops *o, const u8 *par, const u8 *meminfo,
				      const u8 *ldc, u32 ch, const u8 *cfg)
{
	u32 last, size = 0;

	ch &= 0xff;
	last = t33_le32(par + T33_PAR_NSENS) - 1;
	if (ch == last)
		size = o->top_ipc_mem_size(t32_tp_le16(meminfo), t32_tp_le16(cfg));
	if (par[T33_PAR_SIZE * ch + T33_PAR_LDC] == 1 && ch == last && *ldc == 1)
		size += o->ldc_mem_size(ch, t33_le32(meminfo), t33_le32(meminfo + 4));
	return size;
}

/* lib_tisp_get_isp_memory_space */
static inline u32 t33_lib_get_isp_mem(const struct t33_r12f_ops *o, const u8 *par, u8 *meminfo, u32 ch,
				      const u8 *cfg)
{
	const u8 *p;

	ch &= 0xff;
	meminfo[T33_MEM_SIZE * ch + 11] = cfg[4];
	p = par + T33_PAR_SIZE * ch;
	if (t33_le32(p) == 1)
		return o->mdns_t33_mem_size(ch, t32_tp_le16(p + 12), t32_tp_le16(p + 14), cfg[4], p[47]);
	return o->mdns_mem_size(ch, t32_tp_le16(p + 12), t32_tp_le16(p + 14), cfg[4], p[47]);
}

/*
 * lib_tisp_set_isp_ipc_memory_space: returns what the vendor leaves in $v0 (1, the channel 0 sensor
 * count - 1, the LdcInfo byte, or tisp_ldc_cfg_dma's result)
 */
static inline u32 t33_lib_set_ipc_mem(const struct t33_r12f_ops *o, u8 *init_static, const u8 *par,
				      u8 *meminfo, const u8 *ldc, const u32 *ldcattr, u32 ch, u32 base)
{
	u32 last;
	u8 *m;

	*init_static = 1;
	ch &= 0xff;
	last = t33_le32(par + T33_PAR_SIZE * ch + T33_PAR_NSENS) - 1;
	if (ch == last) {
		o->top_ipc_set_cfg(base);
		base += t33_le32(meminfo + 52);
	}
	if (par[T33_PAR_LDC] != 1)
		return 1;
	last = t33_le32(par + T33_PAR_NSENS) - 1;
	if (ch != last)
		return last;
	if (*ldc != 1)
		return *ldc;
	m = meminfo + T33_MEM_SIZE * ch;
	t33_r12f_put32(m + 56, base);
	t33_r12f_put32(m + 60, base + t33_le32(m + 64));
	return o->ldc_cfg_dma(ch, ldcattr[ch]);
}

/* lib_tisp_set_isp_memory_space */
static inline u32 t33_lib_set_isp_mem(const struct t33_r12f_ops *o, u8 *init_static, const u8 *par, u32 ch,
				      u32 a1, u32 a2, u32 a3)
{
	*init_static = 1;
	ch &= 0xff;
	a2 = T33_PAR_SIZE * ch;	/* the vendor's index multiply leaves $a2 clobbered; the callee sees it */
	if (t33_le32(par + T33_PAR_SIZE * ch) == 1)
		return o->mdns_t33_set_cfg(ch, a1, a2, a3);
	return o->mdns_set_cfg(ch, a1, a2, a3);
}

/* lib_tisp_sync_sensor_attr: attr -> the channel copy in stSensorAttr */
static inline u32 t33_lib_sync_sensor_attr(const struct t33_r12f_ops *o, u8 *sattr, u32 ch, const u8 *attr)
{
	u8 *d;

	ch &= 0xff;
	d = sattr + T33_SATTR_SIZE * ch;
	t33_r12f_cp32(d + 36, attr + 36);
	t33_r12f_cp32(d + 40, attr + 40);
	d[116] = attr[116];
	d[117] = attr[117];
	d[118] = attr[118];
	t33_r12f_cp32(d + 68, attr + 68);
	t33_r12f_cp32(d + 72, attr + 72);
	t33_r12f_cp32(d + 88, attr + 88);
	t33_r12f_cp32(d + 92, attr + 92);
	t33_r12f_cp32(d + 96, attr + 96);
	t33_r12f_cp32(d + 100, attr + 100);
	d[119] = attr[119];
	d[120] = attr[120];
	d[121] = attr[121];
	d[0] = attr[0];
	d[1] = attr[1];
	d[2] = attr[2];
	d[3] = attr[3];
	t33_r12f_cp32(d + 56, attr + 56);
	t33_r12f_cp32(d + 60, attr + 60);
	t33_r12f_cp32(d + 64, attr + 64);
	d[123] = attr[123];
	return o->ae_sensor_trig(ch);
}

/* lib_tisp_msca_channel_attr_set */
static inline u32 t33_lib_msca_channel_attr_set(const struct t33_r12f_ops *o, u32 ch, u32 a1, u32 a2, u32 a3)
{
	return o->msca_attr_set(ch & 0xff, a1, a2, a3);
}

/* lib_tisp_msca_chx_reg_stream_off: clear the channel's enable bit */
static inline u32 t33_lib_msca_chx_reg_stream_off(const struct t33_r12f_ops *o, u32 ch)
{
	u32 v = o->reg_read(T33_MSCA_EN_REG);

	return o->reg_write(T33_MSCA_EN_REG, v & ~(1U << (ch & 31)));
}

/* lib_tisp_msca_chx_stream_off */
static inline u32 t33_lib_msca_chx_stream_off(const struct t33_r12f_ops *o, u32 ch)
{
	return o->msca_off(ch & 0xff);
}

/* lib_tisp_msca_chx_stream_on */
static inline u32 t33_lib_msca_chx_stream_on(const struct t33_r12f_ops *o, u32 ch)
{
	return o->msca_on(ch & 0xff);
}

#endif /* TX_ISP_T33_R12F_H */
