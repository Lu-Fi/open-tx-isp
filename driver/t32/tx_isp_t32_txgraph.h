/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp module graph (vendor tx_isp_create_graph_and_nodes): sort the
 * platform devices of the ISP device (+156 count, +160 + 8 i device) into
 * the ISP's subdevice table (type 1: +84 + 4 * id), the type 2 children
 * into their parent's table, then register each subdevice's misc device
 * (+52 fops, +12 misc: minor 61 with fast start d9008, else dynamic; name
 * +8) and /proc entry (+56 fops, under the ISP's +564 directory).
 * Subdevice header bytes: +0 type, +2 parent id, +3 id.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txgraph_oracle).
 */
#ifndef TX_ISP_T32_TXGRAPH_H
#define TX_ISP_T32_TXGRAPH_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txirq.h"
#include "tx_isp_t32_txcore.h"

#define T32_SD_MISC		12U
#define T32_ISP_NPLAT		156U
#define T32_ISP_PLAT(i)		(160U + 8U * (i))
#define T32_ISP_PROCDIR		564U
#define T32_SD_SLOT(id)		(84U + 4U * ((id) & 0xfU))
#define T32_SD_MISC_FOPS	52U
#define T32_SD_PROC_FOPS	56U
#define T32_MISC_FAST_MINOR	61U
#define T32_ENOENT		2U

/* provided by the driver glue (kernel) or by the oracle (host) */
u32 t32_tx_os_get_drvdata(u32 pdev);
int t32_tx_os_misc_register(u32 misc);
void t32_tx_os_misc_deregister(u32 misc);
u32 t32_tx_os_proc_create_data_u(u32 name, u32 mode, u32 parent, u32 fops, u32 data);

static inline u8 t32_tx_sd_byte(u32 sd, u32 off)
{
	return *(volatile u8 *)T32_TX_P(sd + off);
}

/*
 * tx_isp_create_graph_and_nodes(isp).  Vendor: the "can't find" print
 * passes a stale stack word as the module number (ours: the index); the
 * second and third walks do not test the drvdata for NULL; a child without
 * parent ends the second walk only.
 */
static inline int t32_tx_create_graph_and_nodes(u32 isp)
{
	u32 i, k, sd, par;

	for (i = 0; i < t32_tx_ld(isp, T32_ISP_NPLAT); i++) {
		sd = t32_tx_os_get_drvdata(t32_tx_ld(isp, T32_ISP_PLAT(i)));
		if (!t32_tx_sd_ok(sd)) {
			t32_tx_os_isp_printf(1, "[ %s:%d ] Can't find the module(%d) driver!\n",
					     "tx_isp_create_graph_and_nodes", 3211, i);
			continue;
		}
		if (t32_tx_sd_byte(sd, 0) == 1)
			t32_tx_st(isp, T32_SD_SLOT(t32_tx_sd_byte(sd, 3)), sd);
	}
	for (i = 0; i < t32_tx_ld(isp, T32_ISP_NPLAT); i++) {
		sd = t32_tx_os_get_drvdata(t32_tx_ld(isp, T32_ISP_PLAT(i)));
		if (t32_tx_sd_byte(sd, 0) != 2)
			continue;
		par = t32_tx_ld(isp, T32_SD_SLOT(t32_tx_sd_byte(sd, 2)));
		if (!par) {
			t32_tx_os_isp_printf(2, "[ %s:%d ] the module(%d.%d) doesn't have parent!\n",
					     "tx_isp_create_graph_and_nodes", 3226, t32_tx_sd_byte(sd, 2), t32_tx_sd_byte(sd, 3));
			break;
		}
		t32_tx_st(par, T32_SD_SLOT(t32_tx_sd_byte(sd, 3)), sd);
	}
	for (i = 0; i < t32_tx_ld(isp, T32_ISP_NPLAT); i++) {
		sd = t32_tx_os_get_drvdata(t32_tx_ld(isp, T32_ISP_PLAT(i)));
		if (t32_tx_ld(sd, T32_SD_MISC_FOPS)) {
			t32_tx_st(sd, T32_SD_MISC, t32_tx_g_d9008 ? T32_MISC_FAST_MINOR : 255U);
			t32_tx_st(sd, T32_SD_MISC + 8, t32_tx_ld(sd, T32_SD_MISC_FOPS));
			t32_tx_st(sd, T32_SD_MISC + 4, t32_tx_ld(sd, T32_SD_NAME));
			if (t32_tx_os_misc_register(sd + T32_SD_MISC) < 0) {
				t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to register tx-isp miscdev(%d.%d)!\n",
						     "tx_isp_create_graph_and_nodes", 3249, t32_tx_sd_byte(sd, 2),
						     t32_tx_sd_byte(sd, 3));
				for (k = i; k-- > 0;) {
					u32 s = t32_tx_os_get_drvdata(t32_tx_ld(isp, T32_ISP_PLAT(k)));

					if (t32_tx_ld(s, T32_SD_MISC_FOPS))
						t32_tx_os_misc_deregister(s + T32_SD_MISC);
				}
				return -(int)T32_ENOENT;
			}
		}
		if (t32_tx_ld(sd, T32_SD_PROC_FOPS))
			t32_tx_os_proc_create_data_u(t32_tx_ld(sd, T32_SD_NAME), 0444, t32_tx_ld(isp, T32_ISP_PROCDIR),
						     t32_tx_ld(sd, T32_SD_PROC_FOPS), sd);
	}
	return 0;
}

#endif
