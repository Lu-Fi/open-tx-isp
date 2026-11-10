/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 small vendor entry points: the empty stubs, the one-call wrappers of the
 * libimp-facing API and the tiny state accessors of the T32 tx-isp module that
 * belong to no larger block port.
 *
 *  - Empty stubs: the vendor function only returns 0 (its T32 build left the
 *    body out).  Ported as such, so that the ops tables of the driver can name
 *    them.
 *  - Wrappers: the vendor function masks its arguments and calls (or tail
 *    calls) one block function.  The block function is reached through a
 *    hook (struct t32_wrap_ops): the driver fills it with the ported block
 *    function, the oracle with recording stubs.  A tail call returns the
 *    callee's result; a plain call returns the vendor's constant.
 *  - Accessors: per-channel flags and the LSC shadow image read back.
 *
 * Every function follows the vendor binary (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc); checked by tests/t32_oracle (t32_wrap_oracle).
 */
#ifndef TX_ISP_T32_WRAP_H
#define TX_ISP_T32_WRAP_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
typedef uint64_t u64;
#endif

/* ---- 1. empty stubs (vendor body: return 0) ---- */
static inline int t32_ae_ir_update(void) { return 0; }		/* 0x24580 */
static inline int t32_long_again_update(void) { return 0; }	/* 0x24e94 */
static inline int t32_lce_lut_write_curve(void) { return 0; }	/* 0x57250 */
static inline int t32_dma_param_array_get(void) { return 0; }	/* 0x90170 */
static inline int t32_dma_param_array_set(void) { return 0; }	/* 0x90178 */
static inline int t32_func_get_dma_addr(void) { return 0; }	/* 0x90180 */
static inline int t32_msca_state(void) { return 0; }		/* 0xb16a0 */
static inline int t32_msca_shd_ctrl(void) { return 0; }		/* 0xb1864 */

/* ---- 2. one-call wrappers ---- */
typedef u64 (*t32_wrap_fn)(u32 a0, u32 a1, u32 a2, u32 a3);

struct t32_wrap_ops {
	t32_wrap_fn ae_api_ev_alloc_calc;	/* tisp_ae_api_ev_alloc_calc */
	t32_wrap_fn ae_algo_deinit_internal;	/* tisp_ae_algo_deinit_internal */
	t32_wrap_fn awb_algo_deinit_internal;	/* tisp_awb_algo_deinit_internal */
	t32_wrap_fn ae_get_reg_state;		/* tisp_ae_get_reg_state */
	t32_wrap_fn ae_api_get_faceae;		/* tisp_ae_api_get_faceae */
	t32_wrap_fn ae_sepc_area;		/* tisp_ae_sepc_area */
	t32_wrap_fn awb_api_get_face;		/* tisp_awb_api_get_face */
	t32_wrap_fn awb_api_set_face;		/* tisp_awb_api_set_face */
	t32_wrap_fn msca_chx_cfg_load;		/* tisp_msca_chx_cfg_load */
	t32_wrap_fn msca_cfg_shd;		/* tisp_msca_cfg_shd */
};

/* tisp_api_ev_alloc_calc (0xb1994): tail call with the channel masked to 8 bits */
static inline u64 t32_api_ev_alloc_calc(const struct t32_wrap_ops *o, u32 ch, u32 a1, u32 a2, u32 a3)
{
	return o->ae_api_ev_alloc_calc(ch & 0xff, a1, a2, a3);
}

/* tisp_ae_algo_deinit / tisp_awb_algo_deinit (0xb230c / 0xb231c): plain tail calls */
static inline u64 t32_ae_algo_deinit(const struct t32_wrap_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	return o->ae_algo_deinit_internal(a0, a1, a2, a3);
}

static inline u64 t32_awb_algo_deinit(const struct t32_wrap_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	return o->awb_algo_deinit_internal(a0, a1, a2, a3);
}

/* tisp_api_ae_get_reg_state (0xb168c): tail call, both first arguments masked to 8 bits */
static inline u64 t32_api_ae_get_reg_state(const struct t32_wrap_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	return o->ae_get_reg_state(a0 & 0xff, a1 & 0xff, a2, a3);
}

/* tisp_ae_face_get / _set, tisp_awb_face_get / _set (0xb21cc .. 0xb2244): call, return 0 */
static inline int t32_ae_face_get(const struct t32_wrap_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	o->ae_api_get_faceae(a0, a1, a2, a3);
	return 0;
}

static inline int t32_ae_face_set(const struct t32_wrap_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	o->ae_sepc_area(a0, a1, a2, a3);
	return 0;
}

static inline int t32_awb_face_get(const struct t32_wrap_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	o->awb_api_get_face(a0, a1, a2, a3);
	return 0;
}

static inline int t32_awb_face_set(const struct t32_wrap_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	o->awb_api_set_face(a0, a1, a2, a3);
	return 0;
}

/*
 * tisp_msca_channel_start (0x88098): the 8-bit MSCA channel number is
 * sensor * 3 + output; tail call chx_cfg_load(sensor, output, a2, a3).
 */
static inline u64 t32_msca_channel_start(const struct t32_wrap_ops *o, u32 msch, u32 a2, u32 a3)
{
	u32 c = msch & 0xff;

	return o->msca_chx_cfg_load((c / 3) & 0xff, (c % 3) & 0xff, a2, a3);
}

/* tisp_msca_irq_frame_done (0x880c0): msca_cfg_shd(ch, 0), return 1 */
static inline int t32_msca_irq_frame_done(const struct t32_wrap_ops *o, u32 ch, u32 a2, u32 a3)
{
	o->msca_cfg_shd(ch, 0, a2, a3);
	return 1;
}

/* ---- 3. accessors ---- */

/*
 * tisp_ae_algo_deinit_internal / tisp_awb_algo_deinit_internal (0x3659c /
 * 0x3fffc): clear the channel's "algorithm running" word when it is 1
 * (AE: u32 per channel at e15b8, AWB: 16 B per channel at e16c4).  Return 1.
 */
static inline int t32_ae_algo_deinit_internal(u32 *ae_run, u32 ch)
{
	if (ae_run[ch] == 1)
		ae_run[ch] = 0;
	return 1;
}

static inline int t32_awb_algo_deinit_internal(u32 *awb_run, u32 ch)
{
	if (awb_run[ch * 4] == 1)
		awb_run[ch * 4] = 0;
	return 1;
}

/* tisp_day_or_night_g_ctrl (0xb041c): stIspInitPar[ch].dn (904 B per channel, +37) */
#define T32_INITPAR_CH_SZ	904
#define T32_INITPAR_DN		37
static inline u32 t32_day_or_night_g_ctrl(const u8 *init_par, u32 ch)
{
	return init_par[(ch & 0xff) * T32_INITPAR_CH_SZ + T32_INITPAR_DN];
}

/*
 * tisp_single_lsc_reg_read / _reg_lut_read (0x1c7dc / 0x1c824): read back the
 * LSC shadow image (index -> value, see tx_isp_t32_lsc.h).  The vendor's
 * shadow root (first word of its register shadow) holds 256 B per channel;
 * the image pointer is at +204.
 */
#define T32_LSC_ROOT_CH_SZ	256
#define T32_LSC_ROOT_IMG	204
static inline const u32 *t32_single_lsc_img(const u8 *root, u32 ch)
{
	const u8 *slot = root + ch * T32_LSC_ROOT_CH_SZ + T32_LSC_ROOT_IMG;

	return (const u32 *)(uintptr_t)*(const u32 *)slot;
}

static inline u32 t32_single_lsc_reg_read(const u8 *root, u32 ch, u32 idx)
{
	return t32_single_lsc_img(root, ch)[idx];
}

static inline u32 t32_single_lsc_reg_lut_read(const u8 *root, u32 ch, u32 idx, u32 off)
{
	return t32_single_lsc_img(root, ch)[idx + off];
}

#endif /* TX_ISP_T32_WRAP_H */
