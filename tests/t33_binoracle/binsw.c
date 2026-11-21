/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "binsw": SET_TIZIANO_PAR's core side (ispcore_core_ops_ioctl 0x100001e, openimp #692,
 * driver/t32/tx_isp_t33_binsw.h) with the vendor calling convention, run against tx-isp-t33.ko by
 * tools/ko_emu/t33_binoracle.py (spec: binsw.conf).  sd + 0x114 = priv (+0x4a8 sensor attribute word, +0xfa8
 * streaming), sd + 0x94 = the sd op the vendor calls with 0x1000007, sd + 0x50 = the subdev list the vendor walks
 * after the switch (0 here).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_blk.h"

extern void lib_tisp_vic_ctrl(u32 vic, u32 val);
extern u32 lib_tisp_get_ipc_output_pos(void);
extern void private_msleep(u32 ms);
extern int isp_printf(u32 level, const char *fmt, ...);
extern int lib_tisp_refresh_para(const u8 *req);
extern void lib_tisp_set_csccr(u32 ch, u32 mode);

#include "tx_isp_t33_binsw.h"

static void o_vic_ctrl(u32 vic, u32 val) { lib_tisp_vic_ctrl(vic, val); }
static u32 o_ipc_pos(void) { return lib_tisp_get_ipc_output_pos(); }
static void o_msleep(u32 ms) { private_msleep(ms); }
static void o_print(u32 level, u32 line)
{
	isp_printf(level, "[%s %d] IPC Read timeout !!!\n", "tx_isp_tisp_refresh_para", line);
}
static void o_refresh(const u8 *req) { lib_tisp_refresh_para(req); }
static void o_csccr(u32 ch, u32 mode) { lib_tisp_set_csccr(ch, mode); }
static int o_event(u32 sd, u32 cmd, const u32 *ev)
{
	int (*op)(u32, u32, const u32 *) = (int (*)(u32, u32, const u32 *))(unsigned long)*(u32 *)(unsigned long)(sd + 0x94);

	return op ? op(sd, cmd, ev) : 0;
}

static const struct t33_binsw_ops ops = {
	o_vic_ctrl, o_ipc_pos, o_msleep, o_print, o_refresh, o_csccr, o_event,
};

int bo_core_binsw(u8 *sd, u32 cmd, const u8 *arg)
{
	const u8 *priv = (const u8 *)(unsigned long)*(u32 *)(sd + 0x114);

	(void)cmd;
	return t33_core_binsw(&ops, (u32)(unsigned long)sd, *(const u32 *)(priv + 0x4a8),
			      *(const u32 *)(priv + 0xfa8), arg);
}
