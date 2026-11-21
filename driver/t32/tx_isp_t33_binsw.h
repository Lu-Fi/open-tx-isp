/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) IQ bin switch of a streaming sensor (openimp #688): ispcore_core_ops_ioctl command
 * 0x100001e (index 30) of the vendor tx-isp-t33.ko 3028d4ac (0x103d8c..0x103eac), the core side of
 * SET_TIZIANO_PAR.  Binary oracle block "binsw".
 *
 *   req      the 20-byte request { pa, size, wdr, night (+12), update_csccr (u8 +16), vinum (u8 +17) }
 *   ev       the core event block { 1, 0x100001d } (vendor stack +44 / +48)
 *   sattr    priv +1192 (sensor attribute word, 1 = one sensor); streaming = priv +4008
 *
 *   sattr != 1:  lib_tisp_refresh_para(req), then lib_tisp_set_csccr(vinum, night) when update_csccr.
 *   sattr == 1:  lib_tisp_vic_ctrl(0 / 1, 0x4444) (both VICs hold the frames); while streaming,
 *                lib_tisp_get_ipc_output_pos() != 0 -> private_msleep(2), at most 101 tries ("line 2622");
 *                refresh_para / set_csccr as above; while streaming, the subdev's sd +148 op
 *                (sd, 0x1000007, ev): vic_core_ops_ioctl arms a count of one VIC frame start, after which
 *                the VIC interrupt writes 0x4440 back (the restart latches the switched registers, e.g.
 *                the CSCCR UV clip 0x14054: without it the picture keeps the previous mode for seconds).
 *   Returns 0 (the vendor's following subdevice loop has no call for index 30).
 *
 * The includer provides the ops (driver: tx_isp_t32_hw.c; oracle: the vendor names, recorded).
 */
#ifndef TX_ISP_T33_BINSW_H
#define TX_ISP_T33_BINSW_H

#ifndef __KERNEL__
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T33_BINSW_VIC_HOLD	0x4444U
#define T33_BINSW_EV_CMD	0x01000007U	/* core event: VIC frame count */
#define T33_BINSW_EV_NOTIFY	0x0100001dU	/* notified after the count (broadcast without a receiver) */
#define T33_BINSW_IPC_TRIES	101
#define T33_BINSW_LINE		2622

struct t33_binsw_ops {
	void (*vic_ctrl)(u32 vic, u32 val);			/* lib_tisp_vic_ctrl */
	u32 (*ipc_pos)(void);					/* lib_tisp_get_ipc_output_pos */
	void (*msleep)(u32 ms);					/* private_msleep */
	void (*print)(u32 level, u32 line);			/* isp_printf(level, .., "ispcore_core_ops_ioctl", line) */
	void (*refresh_para)(const u8 *req);			/* lib_tisp_refresh_para */
	void (*set_csccr)(u32 ch, u32 mode);			/* lib_tisp_set_csccr */
	int (*event)(u32 sd, u32 cmd, const u32 *ev);		/* the sd +148 op, NULL: none */
};

static inline void t33_binsw_par(const struct t33_binsw_ops *o, const u8 *req)
{
	o->refresh_para(req);
	if (req[16])
		o->set_csccr(req[17], req[12]);
}

static inline int t33_core_binsw(const struct t33_binsw_ops *o, u32 sd, u32 sattr, u32 streaming, const u8 *arg)
{
	u8 req[20];
	u32 ev[2] = { 1, T33_BINSW_EV_NOTIFY };
	unsigned int i, k;

	for (i = 0; i < sizeof(req); i++)
		req[i] = arg[i];
	if (sattr != 1) {
		t33_binsw_par(o, req);
		return 0;
	}
	o->vic_ctrl(0, T33_BINSW_VIC_HOLD);
	o->vic_ctrl(1, T33_BINSW_VIC_HOLD);
	if (streaming)
		for (k = T33_BINSW_IPC_TRIES; o->ipc_pos(); ) {
			o->msleep(2);
			if (!--k) {
				o->print(1, T33_BINSW_LINE);
				break;
			}
		}
	t33_binsw_par(o, req);
	if (streaming && sd && o->event)
		o->event(sd, T33_BINSW_EV_CMD, ev);
	return 0;
}

/*
 * T32 (PRJ007, openimp #534): the same command of the vendor tx-isp-t32.ko (ispcore_core_ops_ioctl case
 * 0x100001e, WDR mode unchanged), with the T32 order: night (+12) 0: lib_tisp_refresh_para, then
 * lib_tisp_set_csccr(vinum, 0) when update_csccr; night 1: lib_tisp_set_csccr(vinum, 1) first when
 * update_csccr, then refresh_para; any other night value: neither.  IPC timeout message line 0xa71
 * ("tx_isp_tisp_refresh_para").  VIC hold, IPC wait and the frame-count event as on the T33.
 */
#define T32_BINSW_LINE		0xa71

static inline void t32_binsw_par(const struct t33_binsw_ops *o, const u8 *req)
{
	if (req[12] == 0) {
		o->refresh_para(req);
		if (req[16])
			o->set_csccr(req[17], 0);
	} else if (req[12] == 1) {
		if (req[16])
			o->set_csccr(req[17], 1);
		o->refresh_para(req);
	}
}

static inline int t32_core_binsw(const struct t33_binsw_ops *o, u32 sd, u32 sattr, u32 streaming, const u8 *arg)
{
	u8 req[20];
	u32 ev[2] = { 1, T33_BINSW_EV_NOTIFY };
	unsigned int i, k;

	for (i = 0; i < sizeof(req); i++)
		req[i] = arg[i];
	if (sattr != 1) {
		t32_binsw_par(o, req);
		return 0;
	}
	o->vic_ctrl(0, T33_BINSW_VIC_HOLD);
	o->vic_ctrl(1, T33_BINSW_VIC_HOLD);
	if (streaming)
		for (k = T33_BINSW_IPC_TRIES; o->ipc_pos(); ) {
			o->msleep(2);
			if (!--k) {
				o->print(1, T32_BINSW_LINE);
				break;
			}
		}
	t32_binsw_par(o, req);
	if (streaming && sd && o->event)
		o->event(sd, T33_BINSW_EV_CMD, ev);
	return 0;
}

#endif /* TX_ISP_T33_BINSW_H */
