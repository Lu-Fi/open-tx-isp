/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "aeint": normal AE interrupt / calc / init (driver/t32/tx_isp_t33_aeint.h). */
#include <stdint.h>
#include <string.h>
typedef unsigned long long u64;
#include "tx_isp_t33_aeint.h"

extern int tisp_ae_long_ev_alloc(u32 ch);
extern u32 system_reg_read(u32 reg);
extern int tisp_event_push(u32 ch, void *ev);
extern int tisp_ae_par_update_trig(u32 ch, u32 par);
extern int tisp_ae_algorithm(u32 ch);
extern int api_ae_long_process(u32 ch);
extern void private_complete(void *c);
extern u32 fix_point_mult2_32(u32 sh, u32 a, u32 b);
extern u32 fix_point_mult3_32(u32 sh, u32 a, u32 b, u32 c);
extern u32 tisp_log2_fixed_to_fixed(u32 v, u32 i, u32 o);
extern int tisp_ae_api_get_luma(u32 ch, u8 *luma);
extern int tisp_awb_get_attr(u32 ch, void *attr);
extern int tisp_flicker_init(u32 ch);
extern int tisp_ae_init_par_calc(u32 ch);
extern int tisp_ae_set_start_param(u32 ch, u32 par);
extern void private_spin_lock_init(void *l);

u32 fliker_info[4], fliker_para[4], stAeStatBuff[4], stAeHistBuff[4], stAeInfo[4], ae_cache[4], ae_contexts[4];
u32 ta_custom_en[4], aeFrameCount[4], gDebugPerFrame[4], gDebugFrameCount[4], stat_num[8], zeratul_open;
u8 ae_init_flag[4], ae_algo_comp[48], ae_cache_default[1744], stIspInitPar[4032];

static void *w_memcpy(void *d, const void *s, size_t n) __attribute__((noinline, noclone));
static void *w_memcpy(void *d, const void *s, size_t n)
{
	return memcpy(d, s, n);
}

static void *w_memset(void *d, int c, size_t n) __attribute__((noinline, noclone));
static void *w_memset(void *d, int c, size_t n)
{
	return memset(d, c, n);
}

static void fill_ops(struct t33_aeint_ops *o) __attribute__((noinline, noclone));
static void fill_ops(struct t33_aeint_ops *o)
{
	o->long_ev_alloc = tisp_ae_long_ev_alloc;
	o->reg_read = system_reg_read;
	o->event_push = tisp_event_push;
	o->par_update_trig = tisp_ae_par_update_trig;
	o->algorithm = tisp_ae_algorithm;
	o->long_process = api_ae_long_process;
	o->complete = private_complete;
	o->mult2_32 = fix_point_mult2_32;
	o->mult3_32 = fix_point_mult3_32;
	o->log2fx = tisp_log2_fixed_to_fixed;
	o->get_luma = tisp_ae_api_get_luma;
	o->get_attr = tisp_awb_get_attr;
	o->flicker_init = tisp_flicker_init;
	o->init_par_calc = tisp_ae_init_par_calc;
	o->set_start_param = tisp_ae_set_start_param;
	o->copy = w_memcpy;
	o->fill = w_memset;
	o->spin_init = private_spin_lock_init;
}

int bo_tisp_normal_ae_interrupt_hist(u32 ch)
{
	struct t33_aeint_ops o;

	fill_ops(&o);
	return t33_ae_int_hist(ch, t33_p(ae_cache[ch]), stat_num, &o);
}

int bo_tisp_normal_ae_interrupt_static(u32 ch)
{
	struct t33_aeint_ops o;

	fill_ops(&o);
	return t33_ae_int_static(ch, stat_num, &o);
}

int bo_tisp_normal_ae_calc_process(u32 ch)
{
	struct t33_aeint_ops o;
	u32 par = t33_le32(t33_p(ae_contexts[ch]) + 19952);

	fill_ops(&o);
	return t33_ae_calc_process(ch, t33_p(ae_cache[ch]), ta_custom_en[ch], par, &aeFrameCount[ch], zeratul_open,
				   gDebugPerFrame[ch], gDebugFrameCount[ch], ae_algo_comp + 12 * ch, &o);
}

int bo_tisp_normal_ae_init(u32 ch, u8 *p, u8 *a2, u8 *a3)
{
	struct t33_aeint_ops o;
	u32 c = ch & 0xff;
	struct t33_aeinit_io io = {
		&fliker_info[c], &fliker_para[c], &stAeStatBuff[c], &stAeHistBuff[c], &stAeInfo[c], &ae_cache[c],
		&ae_contexts[c], &ae_init_flag[c], ae_cache_default,
	};

	fill_ops(&o);
	return t33_normal_ae_init(ch, p, a2, a3, &io, &o);
}
