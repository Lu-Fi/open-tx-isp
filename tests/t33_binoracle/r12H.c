/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "r12H": lib_tisp_intp_process / intp_handler / vic_intp_handle / vic_deinit
 * with the vendor calling convention (spec: r12H.conf).
 */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_r12H.h"

extern u32 bo_ext_rd(u32 reg) __asm__("system_reg_read");
extern int bo_ext_wr(u32 reg, u32 val) __asm__("system_reg_write");
extern void bo_ext_complete(void *c) __asm__("private_complete");
extern void bo_ext_vfree(void *p) __asm__("private_vfree");
extern void bo_ext_tmo(void) __asm__("tisp_tmo_s0s1_overflow_process");

u32 (*bo_irq_func_cb[128])(u32);
u32 bo_stIspIntpInfo[7];
u32 bo_g_staticIntpCont[32];
u32 bo_g_ispErrorNum;
u32 bo_g_ispErrorvalue;
u32 bo_g_ispError[32];
u32 bo_g_intpRcfg0[32];
u32 bo_g_intpRcfg1[32];
u32 bo_g_intpBack0[32];
u32 bo_g_intpComm1[32];
u32 bo_g_frameNum;
u32 bo_g_dmaInfo[2];
u32 bo_stIspInitPar[227];
u8 bo_snapCompl[12];
u32 *bo_stVicCacheAttr[2];
u32 bo_vic_error_count[2];
u8 bo_saveRawNum;
u8 bo_saveRawIndex;
u32 bo_saveRawImageSize;
u32 bo_saveRawPaddr;
u8 bo_pSaveInfo[8];

static u32 h_rd(u32 r) { return bo_ext_rd(r); }
static void h_wr(u32 r, u32 v) { (void)bo_ext_wr(r, v); }
static void h_complete(void *c) { bo_ext_complete(c); }
static void h_vfree(void *p) { bo_ext_vfree(p); }
static void h_tmo(void) { bo_ext_tmo(); }

static const struct t33_irq_ops bo_ops = { h_rd, h_wr, h_tmo, h_complete, h_vfree };

u32 bo_lib_tisp_intp_process(u32 ch, u32 unused)
{
	(void)unused;
	return t33_lib_intp_process(bo_stIspIntpInfo, bo_irq_func_cb, bo_g_staticIntpCont, ch);
}

u32 *bo_lib_tisp_intp_handler(u32 *out)
{
	struct t33_intp_glob g = {
		bo_stIspIntpInfo, &bo_g_ispErrorNum, &bo_g_ispErrorvalue, bo_g_ispError, bo_g_intpRcfg0,
		bo_g_intpRcfg1, bo_g_intpBack0, bo_g_intpComm1, &bo_g_frameNum, bo_g_dmaInfo,
	};

	return t33_lib_intp_handler(&bo_ops, &g, bo_stIspInitPar[226], out);
}

u8 *bo_lib_tisp_vic_intp_handle(u8 *out)
{
	struct t33_vic_intp_glob g;

	g.cfg = bo_stVicCacheAttr;
	g.err_cnt = bo_vic_error_count;
	g.raw_num = &bo_saveRawNum;
	g.raw_idx = &bo_saveRawIndex;
	g.raw_size = bo_saveRawImageSize;
	g.raw_paddr = bo_saveRawPaddr;
	g.compl_ = bo_snapCompl;
	g.save_flag = bo_pSaveInfo[4];
	static u8 stale[4];	/* the vendor's stack bytes persist between calls */
	u8 *r;

	memcpy(out, stale, 4);
	r = (u8 *)t33_lib_vic_intp_handle(&bo_ops, &g, (struct t33_vic_intp_res *)out);
	memcpy(stale, out, 4);
	return r;
}

void bo_lib_tisp_vic_deinit(void)
{
	t33_lib_vic_deinit(&bo_ops, (void **)bo_stVicCacheAttr);
}
