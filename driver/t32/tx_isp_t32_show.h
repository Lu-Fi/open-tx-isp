/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32: the vendor's /proc seq_file show routines of tx-isp-t32.o (Matt Davis'
 * recovered C, PR #15) as readable C.  The format strings and the order of
 * their arguments are the vendor's (.rodata.str), the structures are read at
 * the vendor's offsets.
 *
 * Checked by tests/t32_oracle (t32_sh1_oracle ...): the full print stream
 * (seq_printf / private_seq_printf / isp_printf: kind, seq_file, level and
 * the rendered text) and the result against the recovered C on random states.
 *
 * Vendor behaviour kept on purpose, named here and not fixed:
 *  - t32_isp_csi_show returns the SUM of seq_file.count read after each
 *    print (not the bytes it printed): count1 [+ count2] + count3;
 *  - t32_isp_csi_show: the isp_printf of an invalid parameter gets the
 *    caller's a2 / a3 as unused arguments (the format has none), ours none;
 *  - t32_tisp_module_version_debug counts the channels in a u8: with more
 *    than 255 channels (t32_data_dc608) the loop never ends.
 */
#ifndef TX_ISP_T32_SHOW_H
#define TX_ISP_T32_SHOW_H

#include "tx_isp_t32_glue.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/seq_file.h>
#define T32_SHOW_SEQ(m, ...)	seq_printf((struct seq_file *)(m), __VA_ARGS__)
/* private_seq_printf (vendor 0xaf0) is seq_printf(m, "%pV", fmt + args): the same text */
#define T32_SHOW_PSEQ(m, ...)	seq_printf((struct seq_file *)(m), __VA_ARGS__)
#endif
/* host oracle: T32_SHOW_SEQ / T32_SHOW_PSEQ and isp_printf come from tests/t32_oracle/t32_show_rec.h */

#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif

int isp_printf(unsigned int level, const char *fmt, ...);

/* struct seq_file of the vendor's kernel (32 bit) */
#define T32_SEQ_COUNT		12U
#define T32_SEQ_PRIVATE		72U
#define T32_SHOW_IS_ERR(w)	((u32)(w) >= (u32)-4095)

/* ---- isp_csi_show (0x7540): CSI error words of /proc/jz/isp/isp-csi ---- */
#define T32_CSI_SHOW_SD		280U	/* proc data -> csi device */
#define T32_CSI_SHOW_REGS	244U	/* csi device -> register base */
#define T32_CSI_SHOW_PHY_STATE	0x14U
#define T32_CSI_SHOW_ERR1	0x20U
#define T32_CSI_SHOW_ERR2	0x24U

static inline int t32_isp_csi_show(u8 *m)
{
	u32 dev = t32_g_rd32(m, T32_SEQ_PRIVATE), csi = 0;
	const u8 *regs;
	u32 err1, err2;
	int len = 0;

	if (dev && !T32_SHOW_IS_ERR(dev))
		csi = t32_g_rd32(T32_PTR(dev), T32_CSI_SHOW_SD);
	if (!csi || T32_SHOW_IS_ERR(csi)) {
		isp_printf(2, "The parameter is invalid!\n");
		return 0;
	}
	regs = T32_PTR(t32_g_rd32(T32_PTR(csi), T32_CSI_SHOW_REGS));
	err1 = t32_g_rd32(regs, T32_CSI_SHOW_ERR1);
	err2 = t32_g_rd32(regs, T32_CSI_SHOW_ERR2);
	if (err1) {
		T32_SHOW_SEQ(m, "0x0020 is  0x%08x\n", err1);
		len = (int)t32_g_rd32(m, T32_SEQ_COUNT);
	}
	if (err2) {
		T32_SHOW_SEQ(m, "0x0024 is  0x%08x\n", err2);
		len += (int)t32_g_rd32(m, T32_SEQ_COUNT);
	}
	if (!err1 && !err2)
		return len;
	regs = T32_PTR(t32_g_rd32(T32_PTR(csi), T32_CSI_SHOW_REGS));
	T32_SHOW_SEQ(m, "0x0014 is  0x%08x\n", t32_g_rd32(regs, T32_CSI_SHOW_PHY_STATE));
	return len + (int)t32_g_rd32(m, T32_SEQ_COUNT);
}

/* ---- tisp_module_version_debug (0x287f8): driver vs IQ-bin block versions per channel ---- */
/* the section version words of the channel's tuning base (t32_data_dd0a4[ch]) against the driver's own */
static const struct { const char *fmt; u32 off, drv; } t32_modver[] __maybe_unused = {
	{ "BCSH Drivers: %d, Bin: %d\n",  0xd154, 0x0134dae2 },
	{ "MDNS Drivers: %d, Bin: %d\n",  0x875c, 0x0134d9b3 },
	{ "LCE Drivers: %d, Bin: %d\n",   0x8494, 0x0134dacd },
	{ "DEFOG Drivers: %d, Bin: %d\n", 0x8204, 0x0134da0f },
	{ "TMO Drivers: %d, Bin: %d\n",   0x2ec8, 0x0134da0f },
	{ "GAMMA Drivers: %d, Bin: %d\n", 0x81a8, 0x0134ff91 },
};

/* m = seq_file, tuning = t32_data_dd0a4, nch = t32_data_dc608 (channel count); result 0 */
static inline int t32_tisp_module_version_debug(u8 *m, const u32 *tuning, const u32 *nch)
{
	u8 ch;
	u32 i, bin;

	T32_SHOW_PSEQ(m, "========== ISP modules version ==========\n");
	for (ch = 0; ch < *nch; ch++) {
		T32_SHOW_PSEQ(m, "***** S%d BYPASS INFO *****\n", ch);
		for (i = 0; i < sizeof(t32_modver) / sizeof(t32_modver[0]); i++) {
			bin = t32_g_rd32(T32_PTR(tuning[ch]), t32_modver[i].off);
			if (bin != t32_modver[i].drv)
				T32_SHOW_PSEQ(m, t32_modver[i].fmt, t32_modver[i].drv, bin);
		}
	}
	return 0;
}

#endif
