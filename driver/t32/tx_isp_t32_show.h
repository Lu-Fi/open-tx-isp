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
 *    than 255 channels (t32_data_dc608) the loop never ends;
 *  - lib_tisp_vic_debug_info: its result is 2 (the VIC loop bound);
 *  - tx_isp_ivdc_show does not check its proc data: NULL or an error
 *    pointer reads the IVDC device at 0 (+500); the max-line register
 *    (+236 of the IVDC registers) is read twice (UV half first);
 *  - isp_framesource_show prints the buffer lists under the channel's spin
 *    lock and compares the channel count signed;
 *  - lib_tisp_debug_info hands an 11th, unused word (+68 of the interrupt
 *    counters) to its 10-column line (ours does not); the sensor loop counts in a u8 against
 *    t32_data_dc284 +900 unbounded (the vendor's stack buffers hold 4
 *    sensors, its sensor state 2): callers keep that count <= 2.
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

/* ---- lib_tisp_vic_debug_info (0xaed08): VIC attributes and interrupt counters of both VICs ---- */
/* vic = t32_data_e3f20 (per VIC the attribute / counter block, NULL = skipped), frames = t32_data_e3f58 */
static inline int t32_lib_tisp_vic_debug_info(u8 *m, const u32 *vic, const u32 *frames)
{
	static const char *const err0[8] = { "errNum", "fifoOvf", "errHvf", "errDvpHcom", "dmaFifoOvf",
					     "errCtlLim", "errLineBuff", "mipiFidOvf" };
	static const char *const err1[8] = { "errCh0W", "errCh1W", "errCh2W", "errCh3W", "errCh0H",
					     "errCh1H", "errCh2H", "errCh3H" };
	static const char *const intp[8] = { "ch0 done", "ch1 done", "ch0 start", "ch1 start", "dma0 done",
					     "dma1 done", "dma2 done", "dma3 done" };
	static const char col8s[] = "%11s%11s%11s%11s%11s%11s%11s%11s\n";
	static const char col8d[] = "%11d%11d%11d%11d%11d%11d%11d%11d\n";
	const char *type, *fmt;
	const u8 *v;
	u32 i;

	for (i = 0; i < 2; i++) {
		if (!vic[i])
			continue;
		v = T32_PTR(vic[i]);
		/* interface (+0) and RAW width (+4) by name; the vendor copies them into stack buffers */
		type = t32_g_rd32(v, 0) == 1 ? "dvp" : t32_g_rd32(v, 0) == 2 ? "mipi" : "other";
		fmt = t32_g_rd32(v, 4) == 0 ? "raw8" : t32_g_rd32(v, 4) == 1 ? "raw10" : t32_g_rd32(v, 4) == 2 ? "raw12" : "other";
		T32_SHOW_PSEQ(m, "******************************** TISP VIC%d INFO *******************************************\n", i);
		T32_SHOW_PSEQ(m, "%8s%8s%8s%8s\n", "width", "height", "type", "format");
		T32_SHOW_PSEQ(m, "%8d%8d%8s%8s\n", t32_g_rd16(v, 116), t32_g_rd16(v, 118), type, fmt);
		T32_SHOW_PSEQ(m, "\n");
		T32_SHOW_PSEQ(m, "******************************** TISP VIC%d ERR0 INTP INFO *********************************\n", i);
		T32_SHOW_PSEQ(m, col8s, err0[0], err0[1], err0[2], err0[3], err0[4], err0[5], err0[6], err0[7]);
		T32_SHOW_PSEQ(m, col8d, frames[i], t32_g_rd32(v, 56), t32_g_rd32(v, 92), t32_g_rd32(v, 96),
			      t32_g_rd32(v, 100), t32_g_rd32(v, 104), t32_g_rd32(v, 108), t32_g_rd32(v, 112));
		T32_SHOW_PSEQ(m, "\n");
		T32_SHOW_PSEQ(m, "******************************** TISP VIC%d ERR1 INTP INFO *********************************\n", i);
		T32_SHOW_PSEQ(m, col8s, err1[0], err1[1], err1[2], err1[3], err1[4], err1[5], err1[6], err1[7]);
		T32_SHOW_PSEQ(m, col8d, t32_g_rd32(v, 60), t32_g_rd32(v, 64), t32_g_rd32(v, 68), t32_g_rd32(v, 72),
			      t32_g_rd32(v, 76), t32_g_rd32(v, 80), t32_g_rd32(v, 84), t32_g_rd32(v, 88));
		T32_SHOW_PSEQ(m, "\n");
		T32_SHOW_PSEQ(m, "******************************** TISP VIC%d INTP INFO **************************************\n", i);
		T32_SHOW_PSEQ(m, col8s, intp[0], intp[1], intp[2], intp[3], intp[4], intp[5], intp[6], intp[7]);
		T32_SHOW_PSEQ(m, col8d, t32_g_rd32(v, 8), t32_g_rd32(v, 12), t32_g_rd32(v, 24), t32_g_rd32(v, 28),
			      t32_g_rd32(v, 40), t32_g_rd32(v, 44), t32_g_rd32(v, 48), t32_g_rd32(v, 52));
		T32_SHOW_PSEQ(m, "\n");
	}
	return 2;
}

/* ---- lib_tisp_debug_info (0xac658): tisp version, interrupt counters, per sensor AE / AWB / bin paths ---- */
#define T32_DBGI_AWB_SIZE	76U	/* tisp_awb_get_attr record */
#define T32_DBGI_AE_SIZE	32U	/* tisp_ae_get_info record */
#define T32_DBGI_SENSORS	4U	/* the vendor's stack buffers */
#define T32_DBGI_SNS_STRIDE	904U	/* t32_data_dc284: per sensor state */
#define T32_DBGI_SNS_COUNT	900U	/* t32_data_dc284 +900: the sensor count */
#define T32_DBGI_PATH_STRIDE	336U	/* t32_data_da850: per sensor running bin path */

struct t32_dbgi_src {
	const u32 *err_num;	/* t32_data_e3ef8: error interrupts */
	const u8 *err;		/* t32_data_e3e78: error interrupt counters */
	const u8 *frame;	/* t32_data_e3df0: +0 ipc start, +4 frames */
	const u8 *intp;		/* t32_data_e3cf0: block interrupt counters */
	const u8 *done;		/* t32_data_e3d70: channel done counters */
	const u32 *ldc;		/* t32_data_e3610: ldc interrupts */
	const u8 *dma;		/* t32_data_e3cd0: +0 wdma, +4 rdma */
	const u8 *sns;		/* t32_data_dc284 */
	const u8 *path;		/* t32_data_da850 */
	const u8 *rcfg;		/* t32_data_e3df8: +64 all, +68 lsc */
	void (*awb_get_attr)(u32 ch, u8 *rec);	/* tisp_awb_get_attr */
	void (*ae_get_info)(u32 ch, u8 *rec);	/* tisp_ae_get_info */
};

static inline int t32_lib_tisp_debug_info(u8 *m, const struct t32_dbgi_src *s)
{
	u8 awb[T32_DBGI_SENSORS][T32_DBGI_AWB_SIZE], ae[T32_DBGI_SENSORS][T32_DBGI_AE_SIZE];
	const u8 *e = s->err, *c = s->intp, *sn;
	u8 ch;
	u32 k;

	memset(awb, 0, sizeof(awb));
	memset(ae, 0, sizeof(ae));
	T32_SHOW_PSEQ(m, "******************************** TISP INFO ***********************************************\n");
	T32_SHOW_PSEQ(m, "%14s \n", "version");
	T32_SHOW_PSEQ(m, "%14s \n", "isp_20260618a");
	T32_SHOW_PSEQ(m, "\n");
	T32_SHOW_PSEQ(m, "******************************** TISP ERR INTP INFO **************************************\n");
	T32_SHOW_PSEQ(m, "%9s%16s%16s%16s%16s\n", "errorNum", "errFmSize", "errOverflow", "errIpcComp", "errMdnsComp");
	T32_SHOW_PSEQ(m, "%9d%16d%16d%16d%16d\n", s->err_num[0], t32_g_rd32(e, 16), t32_g_rd32(e, 20),
		      t32_g_rd32(e, 44), t32_g_rd32(e, 60));
	T32_SHOW_PSEQ(m, "\n");
	T32_SHOW_PSEQ(m, "******************************** TISP INTP INFO ******************************************\n");
	T32_SHOW_PSEQ(m, "%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s\n", "frameNum", "AWB", "AE_S", "AE_H", "AF", "GSM", "LCE",
		      "TMO", "DEFOG", "LSF");
	T32_SHOW_PSEQ(m, "%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d\n", t32_g_rd32(s->frame, 4), t32_g_rd32(c, 12),
		      t32_g_rd32(c, 16), t32_g_rd32(c, 20), t32_g_rd32(c, 24), t32_g_rd32(c, 28), t32_g_rd32(c, 32),
		      t32_g_rd32(c, 36), t32_g_rd32(c, 40), t32_g_rd32(c, 120));
	T32_SHOW_PSEQ(m, "\n");
	T32_SHOW_PSEQ(m, "******************************** TISP INTP1 INFO ******************************************\n");
	T32_SHOW_PSEQ(m, "%9s%9s%9s\n", "ch0 Done", "ch1 Done", "ch2 Done");
	T32_SHOW_PSEQ(m, "%9d%9d%9d\n", t32_g_rd32(s->done, 0), t32_g_rd32(s->done, 4), t32_g_rd32(s->done, 8));
	T32_SHOW_PSEQ(m, "******************************** TISP LDC INFO *******************************************\n");
	T32_SHOW_PSEQ(m, "%9s\n", "ldcNum");
	T32_SHOW_PSEQ(m, "%9d\n", s->ldc[0]);
	T32_SHOW_PSEQ(m, "\n");
	T32_SHOW_PSEQ(m, "******************************** TISP INTP1 INFO *****************************************\n");
	T32_SHOW_PSEQ(m, "%9s%9s%9s\n", "ipcStart", "wdma", "rdma");
	T32_SHOW_PSEQ(m, "%9d%9d%9d\n", t32_g_rd32(s->frame, 0), t32_g_rd32(s->dma, 0), t32_g_rd32(s->dma, 4));
	T32_SHOW_PSEQ(m, "\n");

	for (ch = 0; ch < t32_g_rd32(s->sns, T32_DBGI_SNS_COUNT); ch++)
		s->awb_get_attr(ch, awb[ch]);
	for (ch = 0; ch < t32_g_rd32(s->sns, T32_DBGI_SNS_COUNT); ch++) {
		const u8 *a = ae[ch], *w = awb[ch];

		sn = s->sns + ch * T32_DBGI_SNS_STRIDE;
		T32_SHOW_PSEQ(m, "******************************** TISP SENSOR%d INFO ****************************************\n", ch);
		T32_SHOW_PSEQ(m, "%9s%9s\n", "width", "height");
		T32_SHOW_PSEQ(m, "%9d%9d\n", t32_g_rd32(sn, 0), t32_g_rd32(sn, 4));
		T32_SHOW_PSEQ(m, "\n");
		s->ae_get_info(ch, ae[ch]);
		T32_SHOW_PSEQ(m, "******************************** TISP S%d AE INFO ******************************************\n", ch);
		T32_SHOW_PSEQ(m, "%9s%9s%9s%9s%9s%9s%11s%11s\n", "ev", "exp", "ag", "ispDg", "luma", "target",
			      "ispDgErr", "expListEn");
		T32_SHOW_PSEQ(m, "%9lld%9d%9d%9d%9d%9d%11d%11d\n",
			      (long long)((u64)t32_g_rd32(a, 4) << 32 | t32_g_rd32(a, 0)), t32_g_rd32(a, 8),
			      t32_g_rd32(a, 12), t32_g_rd32(a, 16), t32_g_rd16(a, 24), t32_g_rd16(a, 26),
			      t32_g_rd16(a, 28), t32_g_rd32(a, 20));
		T32_SHOW_PSEQ(m, "\n");
		T32_SHOW_PSEQ(m, "******************************** TISP S%d AWB INFO *****************************************\n", ch);
		T32_SHOW_PSEQ(m, "%9s%9s%9s\n", "rGain", "bGain", "awbCt");
		T32_SHOW_PSEQ(m, "%9d%9d%9d\n", t32_g_rd32(w, 4), t32_g_rd32(w, 8), t32_g_rd32(w, 16));
		T32_SHOW_PSEQ(m, "\n");
		T32_SHOW_PSEQ(m, "******************************** TISP SENSOR%d BIN PATH *****************************************\n", ch);
		T32_SHOW_PSEQ(m, "RunningPath: %s\n", s->path + ch * T32_DBGI_PATH_STRIDE);
		for (k = 0; k < 6; k++)
			if (sn[126 + k] == 1)
				T32_SHOW_PSEQ(m, "DefaultPath%d: %s\n", k, sn + 132 + 128 * k);
	}
	T32_SHOW_PSEQ(m, "**** ISP RCFG INFO ****\n");
	T32_SHOW_PSEQ(m, "%9s%9s\n", "all", "lsc");
	T32_SHOW_PSEQ(m, "%9d%9d\n", t32_g_rd32(s->rcfg, 64), t32_g_rd32(s->rcfg, 68));
	T32_SHOW_PSEQ(m, "\n");
	return 0;
}

/* ---- tx_isp_ivdc_show (0xddd8): IVDC buffer and interrupt statistics ---- */
/* st = the IVDC statistics words from t32_data_d9898 (index = (symbol - 0xd9898) / 4) */
#define T32_IVDC_ST(sym)	(((sym) - 0xd9898U) / 4U)

static inline int t32_tx_isp_ivdc_show(u8 *m, const u32 *st, const u32 *aov_en, const u32 *direct_mode)
{
	static const char *const cnt[30] = {
		" SID Discard                 : %d\n",
		" VPU Cmd Fifo Overflow       : %d\n",
		" Check Sum Err               : %d\n",
		" Dout Addr Hit Err           : %d\n",
		" VPU Work Sid Err            : %d\n",
		" Check Point Err             : %d\n",
		" IVDC Work Overflow          : %d\n",
		" VPU Ack Timeout             : %d\n",
		" ISP Frm Lost                : %d\n",
		" Sync Hit Timeout            : %d\n",
		" Check Hit Timeout           : %d\n",
		" VPU1 Frm Done               : %d\n",
		" VPU0 Frm Done               : %d\n",
		" ISP Frm Start               : %d\n",
		" ISP Frm Done                : %d\n",
		" VPU1 Half Done              : %d\n",
		" VPU0 Half Done              : %d\n",
		" ISP Half Done               : %d\n",
		" DDR Half Full1              : %d\n",
		" DDR Half Full0              : %d\n",
		" VPU Work Timeout            : %d\n",
		" ISP Wait Timeout            : %d\n",
		" ISP Rst                     : %d\n",
		" ISP Frm Err                 : %d\n",
		" VPU Abandon Over            : %d\n",
		" ISP Discard Over            : %d\n",
		" FIFO Y Overflow             : %d\n",
		" FIFO UV Overflow            : %d\n",
		" DDR Y Overflow              : %d\n",
		" DDR UV Overflow             : %d\n",
	};
	u32 dev = t32_g_rd32(m, T32_SEQ_PRIVATE), ivdc = 0, y = 0, uv = 0, i;

	if (dev && !T32_SHOW_IS_ERR(dev))
		ivdc = t32_g_rd32(T32_PTR(dev), T32_CSI_SHOW_SD);
	T32_SHOW_PSEQ(m, "****************** IVDC INFO **********************\n");
	T32_SHOW_PSEQ(m, " IVDC DDR Addr               : 0x%08x\n", t32_g_rd32(T32_PTR(ivdc), 500));
	T32_SHOW_PSEQ(m, " IVDC DDR Buffer Size        : %d\n", t32_g_rd32(T32_PTR(ivdc), 504));
	for (i = 0; i < 30; i++)	/* d98bc .. d9930 */
		T32_SHOW_PSEQ(m, cnt[i], st[T32_IVDC_ST(0xd98bcU) + i]);
	if (st[T32_IVDC_ST(0xd9898U)]) {	/* registers mapped: max DDR lines, Y in 14..0, UV in 30..16 */
		const u8 *regs = T32_PTR(t32_g_rd32(T32_PTR(ivdc), 284));

		uv = (t32_g_rd32(regs, 236) >> 16) & 0x7fff;
		y = t32_g_rd32(regs, 236) & 0x7fff;
	}
	T32_SHOW_PSEQ(m, " IVDC Y Max ddr line         : %d\n", y);
	T32_SHOW_PSEQ(m, " IVDC UV Max ddr line        : %d\n", uv);
	T32_SHOW_PSEQ(m, " AOV enable %d,direct_mode %d: (%d, %d, %d, %d)\n", *aov_en, *direct_mode,
		      st[T32_IVDC_ST(0xd98f0U)] - st[T32_IVDC_ST(0xd98a8U)],
		      st[T32_IVDC_ST(0xd98f4U)] - st[T32_IVDC_ST(0xd98acU)],
		      st[T32_IVDC_ST(0xd98ecU)] - st[T32_IVDC_ST(0xd98a4U)],
		      st[T32_IVDC_ST(0xd98e8U)] - st[T32_IVDC_ST(0xd98a0U)]);
	T32_SHOW_PSEQ(m, " help:\n");
	T32_SHOW_PSEQ(m, " clean Max ddr line : set 0x131000ec to 0\n,such as:devmem 0x131000ec 32 0x0\n");
	return 0;
}

/* ---- isp_framesource_show (0x12628): state, format, scaler / crop and buffers of every frame channel ---- */
#define T32_FS_CHANS		288U	/* fs device: channel array */
#define T32_FS_NCHAN		292U	/* fs device: channel count (int) */
#define T32_FS_CHAN_SIZE	816U
#define T32_FS_BUFS		276U	/* channel: 64 buffer pointers */
#define T32_FS_QLIST		536U	/* channel: queued list head, node at buffer +124 */
#define T32_FS_QCOUNT		544U
#define T32_FS_DLIST		548U	/* channel: done list head, node at buffer +132 */
#define T32_FS_DCOUNT		556U
#define T32_FS_WIDTH		588U
#define T32_FS_HEIGHT		592U
#define T32_FS_FOURCC		596U
#define T32_FS_CROP_EN		636U
#define T32_FS_CROP		640U	/* top, left, width, height */
#define T32_FS_SCALER_EN	656U
#define T32_FS_SCALER		660U	/* width, height */
#define T32_FS_LOCK		724U
#define T32_FS_STATE		736U	/* 4 = running */
#define T32_FS_OUT_BUFS		764U
#define T32_FS_LOST_BUFS	768U
#define T32_FS_DEBUG		804U
#define T32_FS_BUF_ADDR		52U	/* buffer: DMA address */

struct t32_show_lock_ops {
	void (*lock_irqsave)(void *lock, unsigned long *flags);		/* __private_spin_lock_irqsave */
	void (*unlock_irqrestore)(void *lock, unsigned long flags);	/* private_spin_unlock_irqrestore */
};

static inline void t32_fs_show_list(u8 *m, const char *fmt, const u8 *head, u32 node)
{
	const u8 *e;

	for (e = T32_PTR(t32_g_rd32(head, 0)) - node; e + node != head; e = T32_PTR(t32_g_rd32(e, node)) - node)
		T32_SHOW_PSEQ(m, fmt, t32_g_rd32(e, T32_FS_BUF_ADDR));
}

static inline int t32_isp_framesource_show(u8 *m, const struct t32_show_lock_ops *os)
{
	u32 dev = t32_g_rd32(m, T32_SEQ_PRIVATE), fs = 0, k;
	unsigned long flags = 0;
	const u8 *c, *b;
	int i;

	if (dev && !T32_SHOW_IS_ERR(dev))
		fs = t32_g_rd32(T32_PTR(dev), T32_CSI_SHOW_SD);
	if (!fs || T32_SHOW_IS_ERR(fs)) {
		isp_printf(2, "The parameter is invalid!\n");
		return 0;
	}
	for (i = 0; i < (int)t32_g_rd32(T32_PTR(fs), T32_FS_NCHAN); i++) {
		T32_SHOW_PSEQ(m, "############## framesource %d ###############\n", i);
		c = T32_PTR(t32_g_rd32(T32_PTR(fs), T32_FS_CHANS)) + i * T32_FS_CHAN_SIZE;
		T32_SHOW_PSEQ(m, "chan status: %s\n", t32_g_rd32(c, T32_FS_STATE) == 4 ? "running" : "stop");
		if (t32_g_rd32(c, T32_FS_STATE) != 4)
			continue;
		T32_SHOW_PSEQ(m, "output pixformat: %c%c%c%c\n", (int8_t)c[T32_FS_FOURCC], (int8_t)c[T32_FS_FOURCC + 1],
			      (int8_t)c[T32_FS_FOURCC + 2], (int8_t)c[T32_FS_FOURCC + 3]);
		T32_SHOW_PSEQ(m, "output resolution: %d * %d\n", t32_g_rd32(c, T32_FS_WIDTH), t32_g_rd32(c, T32_FS_HEIGHT));
		T32_SHOW_PSEQ(m, "scaler : %s\n", c[T32_FS_SCALER_EN] ? "enable" : "disable");
		if (c[T32_FS_SCALER_EN]) {
			T32_SHOW_PSEQ(m, "scaler width: %d\n", t32_g_rd32(c, T32_FS_SCALER));
			T32_SHOW_PSEQ(m, "scaler height: %d\n", t32_g_rd32(c, T32_FS_SCALER + 4));
		}
		T32_SHOW_PSEQ(m, "crop : %s\n", c[T32_FS_CROP_EN] ? "enable" : "disable");
		if (c[T32_FS_CROP_EN]) {
			T32_SHOW_PSEQ(m, "crop top: %d\n", t32_g_rd32(c, T32_FS_CROP));
			T32_SHOW_PSEQ(m, "crop left: %d\n", t32_g_rd32(c, T32_FS_CROP + 4));
			T32_SHOW_PSEQ(m, "crop width: %d\n", t32_g_rd32(c, T32_FS_CROP + 8));
			T32_SHOW_PSEQ(m, "crop height: %d\n", t32_g_rd32(c, T32_FS_CROP + 12));
		}
		T32_SHOW_PSEQ(m, "the state of buffers:\n");
		os->lock_irqsave((void *)(uintptr_t)(c + T32_FS_LOCK), &flags);
		T32_SHOW_PSEQ(m, "queue count: %d\n", t32_g_rd32(c, T32_FS_QCOUNT));
		t32_fs_show_list(m, "queue addr: 0x%08lx\n", c + T32_FS_QLIST, 124);
		T32_SHOW_PSEQ(m, "done count: %d\n", t32_g_rd32(c, T32_FS_DCOUNT));
		t32_fs_show_list(m, "done addr: 0x%08lx\n", c + T32_FS_DLIST, 132);
		os->unlock_irqrestore((void *)(uintptr_t)(c + T32_FS_LOCK), flags);
		T32_SHOW_PSEQ(m, "the output buffers is: %d\n", t32_g_rd32(c, T32_FS_OUT_BUFS));
		T32_SHOW_PSEQ(m, "the losted buffers is: %d\n", t32_g_rd32(c, T32_FS_LOST_BUFS));
		T32_SHOW_PSEQ(m, "---------------------------\n");
		for (k = 0; k < 64; k++) {
			b = T32_PTR(t32_g_rd32(c, T32_FS_BUFS + 4 * k));
			if (b)
				T32_SHOW_PSEQ(m, "buf:%d, state:%d, buf_state:%d, buf_qcnt:%d, buf_dqcnt:%d\n", k,
					      t32_g_rd32(b, 100), b[104], t32_g_rd32(b, 108), t32_g_rd32(b, 112));
		}
		T32_SHOW_PSEQ(m, "debug: %d\n", t32_g_rd32(c, T32_FS_DEBUG));
	}
	return 0;
}

#endif
