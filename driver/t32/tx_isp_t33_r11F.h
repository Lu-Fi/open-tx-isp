/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 MSCA work state / address FIFO clear, library parameter refresh and
 * ISP core interrupt masks (vendor tisp_msca_state, tisp_msca_work_state,
 * tisp_msca_Shd_ctrl, tisp_msca_addr_fifo_clear_byindex,
 * lib_tisp_refresh_para, lib_tisp_intp_enable, lib_tisp_intp_disable).
 * Callees are reached through struct t33_r11f_ops.
 */
#ifndef TX_ISP_T33_R11F_H
#define TX_ISP_T33_R11F_H

#include "tx_isp_t33_blk.h"

struct t33_r11f_ops {
	u32 (*reg_read)(u32 reg);			/* system_reg_read */
	u32 (*reg_write)(u32 reg, u32 val);		/* system_reg_write */
	u32 (*bin_parse)(u32 ch, u32 size, u32 addr);	/* tisp_bin_data_parse */
	void (*pre[4])(u32 ch);			/* top, dpc, blc, ae */
	void (*awb)(u32 ch, u32 mode);			/* tisp_awb_dn_params_refresh */
	void (*post[12])(u32 ch);			/* lsc .. cdns */
	void (*defog)(u32 ch, u32 a, u32 b);		/* tisp_defog_switch_para */
	void (*tail[4])(u32 ch);			/* lce, rdns, af, csccr */
	void (*wake)(void *q, u32 mode, u32 nr, void *key);	/* __wake_up */
};

/* tisp_msca_state */
static inline u32 t33_msca_state(void)
{
	return 0;
}

/* tisp_msca_Shd_ctrl: empty */
static inline void t33_msca_shd_ctrl(void)
{
}

/* tisp_msca_work_state: bit n of the MSCA work status word */
static inline u32 t33_msca_work_state(const struct t33_r11f_ops *o, u32 n)
{
	return (o->reg_read(0xf0054) >> (n & 31)) & 1;
}

/* tisp_msca_addr_fifo_clear_byindex: four clear pulses of channel ch */
static inline u32 t33_msca_fifo_clear(const struct t33_r11f_ops *o, u32 ch)
{
	u32 b = 0xf0000 + ((ch & 0xff) << 8);

	o->reg_write(b + 0x144, 1);
	o->reg_write(b + 0x164, 1);
	o->reg_write(b + 0x148, 1);
	return o->reg_write(b + 0x168, 1);
}

/* lib_tisp_intp_enable / lib_tisp_intp_disable */
static const u32 t33_r11f_intp_regs[7] = {
	0x30020, 0x30030, 0x30060, 0x300a0, 0x300b0, 0x300c0, 0x300d0,
};

static inline u32 t33_r11f_intp_write(const struct t33_r11f_ops *o, u32 val)
{
	u32 r = 0;
	unsigned int i;

	for (i = 0; i < 7; i++)
		r = o->reg_write(t33_r11f_intp_regs[i], val);
	return r;
}

static inline u32 t33_lib_intp_enable(const struct t33_r11f_ops *o)
{
	return t33_r11f_intp_write(o, 0xffffffffu);
}

static inline u32 t33_lib_intp_disable(const struct t33_r11f_ops *o)
{
	return t33_r11f_intp_write(o, 0);
}

/*
 * lib_tisp_refresh_para: req = request (bin address, size, mode at 12, channel byte at 17).
 * bin_info = binInfo (20 bytes per channel), tparams = tparamsP, init_finish = ispInitFinish,
 * init_par = stIspInitPar (1008 bytes per channel), poll = tispPollValue, queue = dumpQueue.
 */
static inline u32 t33_lib_refresh_para(const struct t33_r11f_ops *o, const u8 *req, u8 *bin_info,
				       u8 **tparams, const u8 *init_finish, const u8 *init_par,
				       u8 *poll, void *queue)
{
	u32 ch = req[17], mode = t33_le32(req + 12) & 0xff;
	volatile u8 *dst = bin_info + ch * 20;
	const u8 *ip;
	unsigned int i;

	o->bin_parse(ch, t33_le32(req + 4), t33_le32(req) + 0x80000000u);
	if (mode == 1)
		tparams[ch][31] = 0;
	for (i = 0; i < 20; i++)
		dst[i] = req[i];
	if (init_finish[ch] != 1)
		return 0;
	for (i = 0; i < 4; i++)
		o->pre[i](ch);
	o->awb(ch, mode);
	for (i = 0; i < 12; i++)
		o->post[i](ch);
	ip = init_par + ch * 1008;
	o->defog(ch, t32_tp_le16(ip + 12), t32_tp_le16(ip + 14));
	for (i = 0; i < 4; i++)
		o->tail[i](ch);
	poll[0] = 1;
	poll[2] = (u8)mode;
	o->wake(queue, 1, 1, NULL);
	return 0;
}

#endif /* TX_ISP_T33_R11F_H */
