/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 /proc/tisp: the vendor tisp debug view (openimp #589).
 *
 * Vendor reference (tx-isp-t33.ko md5 3028d4ac, Ghidra t33): tisp_write_proc
 * (0x2ecf4: "help" or a level 0..4, other numbers ignored, no number -EINVAL),
 * tisp_show_debug_info (0x2eb30: level 0 tisp_isp_par_in_info, 1 VIC + ISP
 * error / interrupt tables + LDC, 2 nothing, 3 tisp_isp_top_debug_info,
 * 4 lib_tisp_vic_debug_info + lib_tisp_debug_info; default level 1).
 * The counters are the ones lib_tisp_intp_handler (0x104e24),
 * lib_tisp_intp_process (0x105114: g_staticIntpCont, bits < 32 with a
 * handler) and lib_tisp_vic_intp_handle (0x10710c) keep; the text layout is
 * the vendor's (QEMU capture of the vendor ko, audit issue589/q33/v1).
 */
#ifndef TX_ISP_T33_TISP_H
#define TX_ISP_T33_TISP_H

#include <linux/types.h>

struct t33_tisp_cnt {
	u32 err_num, err_value, err[32];	/* g_ispErrorNum, g_ispErrorvalue, g_ispError (0x30068) */
	u32 comm1[32];				/* g_intpComm1: 0x30038 (channel done) */
	u32 back0[32];				/* g_intpBack0: 0x300a8 (module done) */
	u32 rcfg0[32];				/* g_intpRcfg0: 0x300c8 */
	u32 rcfg1[32];				/* g_intpRcfg1: 0x300d8 */
	u32 stat[32];				/* g_staticIntpCont: 0x30028 bits with a handler */
	u32 frame_num;				/* g_frameNum: 0x30028 bit 0 */
	u32 dma[2];				/* g_dmaInfo: 0x300b8 bit 4 (wdma) / 5 (rdma) */
	u32 trig[4];				/* g_rcfg_trig_start_cnt (lib_tisp_rcfg_cfg_trig) */
	u32 vic_err[2];				/* vic_error_count: status bits 9..27 */
	u32 vic[2][29];				/* stVicCacheAttr counters (+8: bits 0..5, +0x28 dma, +0x38 bits 9..23) */
};

extern struct t33_tisp_cnt t33_tisp_cnt;

/* the 0x30028 bits our ISR handles (the vendor's registered system irqs 0, 3, 4, 5, 8, 30) */
#define T33_TISP_STAT_BITS	0x40000139U

static inline void t33_tisp_bits(u32 *c, u32 v)
{
	while (v) {
		unsigned int b = __builtin_ctz(v);

		c[b]++;
		v &= v - 1;
	}
}

/* lib_tisp_intp_handler + lib_tisp_intp_process counting, from the words our ISR read */
static inline void t33_tisp_count_isp(u32 sta, u32 done, u32 err, u32 msca, u32 s3, u32 rc0, u32 rc1)
{
	struct t33_tisp_cnt *c = &t33_tisp_cnt;

	if (err) {
		c->err_num++;
		c->err_value = err;
		t33_tisp_bits(c->err, err);
	}
	t33_tisp_bits(c->rcfg0, rc0);
	t33_tisp_bits(c->rcfg1, rc1);
	t33_tisp_bits(c->back0, msca);
	t33_tisp_bits(c->comm1, done);
	if (sta & 1)
		c->frame_num++;
	if (s3 & 0x10)
		c->dma[0]++;
	if (s3 & 0x20)
		c->dma[1]++;
	t33_tisp_bits(c->stat, sta & T33_TISP_STAT_BITS);
}

/* lib_tisp_vic_intp_handle counting of one VIC (status 0x1e0 / 0x1e4) */
static inline void t33_tisp_count_vic(unsigned int n, u32 s0, u32 s1)
{
	u32 *v = t33_tisp_cnt.vic[n];
	unsigned int i;

	if (s0 & 0x1ff)
		for (i = 0; i < 6; i++)
			if (s0 & (1U << i))
				v[i]++;			/* +8 + 4 i */
	if (s0 & 0xfffffe00U)
		for (i = 0; i < 15; i++)
			if (s0 & (1U << (9 + i)))
				v[12 + i]++;		/* +0x38 + 4 i */
	if (s0 & 0x0ffffe00U)
		t33_tisp_cnt.vic_err[n]++;
	for (i = 0; i < 4; i++)
		if (s1 & (1U << i))
			v[8 + i]++;			/* +0x28 + 4 i */
}

/* the picture data of the debug view, set at the front end init (tisp_isp_par_in_info / VIC info) */
void t33_tisp_set_par(u32 width, u32 height, u32 order, u32 depth, u32 direct, u32 dbus);
int t33_tisp_proc_init(void);
void t33_tisp_proc_exit(void);

/* providers in tx_isp_t32_hw.c / tx_isp_t32_snapldc.c */
const u8 *t33_tisp_top_par(void);	/* stTopPar of sensor 0 (bypass bytes at +0xc..+0x23), NULL before the bin */
void t33_tisp_ldc_info(u32 *done, u32 *err);	/* ldcIntpInfo */
u32 t33_core_sensor_dbus(void);			/* tx_isp_t32_core.c */

#endif
