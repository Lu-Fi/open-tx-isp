/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 libisp entry wrappers and empty vendor entry points.
 *
 * Forwarders (the vendor tail-jumps to the callee; the lib_ ones pass only the low byte of the channel):
 *   t32_lib_remove                 lib_tisp_remove 0xafb10            -> tisp_lce_remove()
 *   t32_lib_func_stop_reset        lib_tisp_func_stop_reset 0xafb00   -> tisp_func_stop_reset()
 *   t32_lib_fw_process             lib_tisp_fw_process 0x282e4        -> tisp_event_process(ch & 0xff)
 *   t32_lib_fw_process_empty       lib_tisp_fw_process_empty 0x282f4  -> tisp_event_work_empty(ch & 0xff)
 *   t32_lib_get_csccr_output       lib_tisp_api_get_csccr_output 0xb3380 -> lib_tisp_csccr_dn_status(ch & 0xff)
 *   t32_api_ev_alloc_calc          tisp_api_ev_alloc_calc 0xb1994     -> tisp_ae_api_ev_alloc_calc(ch & 0xff)
 *   t32_lce_self_light_correct_global lce_self_light_correct_global 0x53d44 -> lce_self_light_correct_local
 *                                  (all five arguments: the tail jump keeps the caller's stack argument)
 * Readers:
 *   t32_lib_get_ipc_output_pos     lib_tisp_get_ipc_output_pos 0x2a1f4: system_reg_read(0x30200)
 *   t32_lib_get_running_mode       lib_tisp_api_g_runningMode 0xb3478: byte 44 of channel 0 state (t32_data_dc284)
 * Empty in the T32 vendor object (return 0): ldc_cmd_show 0xfd24, ldc_slake_module 0xfd1c, ldc_activate_module
 * 0x10390, tisp_msca_state 0xb16a0, tisp_ae_ir_update 0x24580, tisp_long_again_update 0x24e94,
 * tisp_lce_lut_write_curve 0x57250, tisp_dma_param_array_get/_set 0x90170/0x90178, tisp_func_get_dma_addr
 * 0x90180.  ldc_cmd_set 0xfd2c returns its third argument (the store size).
 *
 * Group 2 (state helpers):
 *   t32_lib_dma_cache_32           lib_tisp_dma_cache_32 0x1d170: x + 32 - (x & 31) (next 32-byte boundary,
 *                                  an aligned x moves by a full 32)
 *   t32_lib_set_running_mode       lib_tisp_api_s_runningMode 0xb346c: byte 44 of channel 0 state (t32_data_dc2b0)
 *   t32_lib_vic_get_par            lib_tisp_vic_get_par 0xae69c: the two VIC parameter words lib_tisp_vic_set_par
 *                                  stores (t32_data_e3f30, t32_data_e3f2c); returns the second
 *   t32_lib_get_3a_info            lib_tisp_api_get_3a_info 0xb334c: despite the name it sets the per-channel AE
 *                                  debug trace (t32_data_e1500[ch] = n != 0, t32_data_e14f0[ch] = n), returns 0;
 *                                  no channel check (ch & 0xff >= 4 writes past the 4-word arrays, not compared)
 *
 * Group 3 (register triggers):
 *   t32_lib_trig                   lib_tisp_trig 0x25bec: system_reg_write(0x1040, 1)
 *   t32_lib_vic_ctrl               lib_tisp_vic_ctrl 0xaf168: VIC (ch & 0xff) register 0x1a8 = val
 *   t32_lib_vic_stop_byindex       lib_tisp_vic_stop_byindex 0xad0b0: VIC idx register 0x10 = 0 for idx 0 / 1,
 *                                  else nothing (returns 0)
 *   t32_lib_tmo_strength_ctrl      lib_tisp_api_tmo_strength_ctrl 0xb3458 -> tisp_tmo_api_strength_ctrl(ch & 0xff,
 *                                  strength & 0xffff)
 * Group 4 (memory space):
 *   t32_lib_set_isp_memory_space   lib_tisp_set_isp_memory_space 0x282c8: sets the "memory configured" byte
 *                                  (t32_data_dd674) and forwards to tisp_mdns_set_mem_cfg(ch & 0xff, a1, a2, a3)
 *   t32_lib_get_isp_memory_space   lib_tisp_get_isp_memory_space 0x28144: copies cfg byte 4 into byte 11 of the
 *                                  76-byte MDNS channel block (t32_data_dc074) and returns
 *                                  tisp_mdns_get_mem_size(ch, width, height, cfg[4], state byte 43) with width /
 *                                  height the u16 at 0 / 4 of the channel state (t32_data_dc284)
 *   VIC n registers are at (0x80 + n) << 12 in the system register space.
 *
 * Hooks: struct t32_lib_hooks (the callees).  Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_libwrap_oracle, t32_libst_oracle, t32_libtr_oracle, t32_libms_oracle).
 */
#ifndef TX_ISP_T32_LIBWRAP_H
#define TX_ISP_T32_LIBWRAP_H

#ifndef __KERNEL__
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_IPC_OUTPUT_POS_REG	0x30200U
#define T32_CH_STATE_RUNMODE	44U
#define T32_CH_STATE_SIZE_LW	904U	/* t32_data_dc284 channel block */	/* t32_data_dc284 byte: running mode (day / night) */

struct t32_lib_hooks {
	void *ctx;
	u32 (*lce_remove)(void *ctx);					/* tisp_lce_remove */
	u32 (*func_stop_reset)(void *ctx);				/* tisp_func_stop_reset */
	u32 (*event_process)(void *ctx, u32 ch);			/* tisp_event_process */
	u32 (*event_work_empty)(void *ctx, u32 ch);			/* tisp_event_work_empty */
	u32 (*csccr_dn_status)(void *ctx, u32 ch);			/* lib_tisp_csccr_dn_status */
	u32 (*ae_ev_alloc_calc)(void *ctx, u32 ch);			/* tisp_ae_api_ev_alloc_calc */
	u32 (*lce_correct_local)(void *ctx, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4); /* lce_self_light_correct_local */
	u32 (*sys_read)(void *ctx, u32 reg);				/* system_reg_read */
	u32 (*sys_write)(void *ctx, u32 reg, u32 val);			/* system_reg_write */
	u32 (*tmo_strength)(void *ctx, u32 ch, u32 strength);		/* tisp_tmo_api_strength_ctrl */
	u32 (*mdns_set_mem_cfg)(void *ctx, u32 ch, u32 a1, u32 a2, u32 a3);	/* tisp_mdns_set_mem_cfg */
	u32 (*mdns_get_mem_size)(void *ctx, u32 ch, u32 w, u32 h, u32 mode, u32 b43); /* tisp_mdns_get_mem_size */
};

static inline u32 t32_lib_remove(const struct t32_lib_hooks *h)
{
	return h->lce_remove(h->ctx);
}

static inline u32 t32_lib_func_stop_reset(const struct t32_lib_hooks *h)
{
	return h->func_stop_reset(h->ctx);
}

static inline u32 t32_lib_fw_process(const struct t32_lib_hooks *h, u32 ch)
{
	return h->event_process(h->ctx, ch & 0xff);
}

static inline u32 t32_lib_fw_process_empty(const struct t32_lib_hooks *h, u32 ch)
{
	return h->event_work_empty(h->ctx, ch & 0xff);
}

static inline u32 t32_lib_get_csccr_output(const struct t32_lib_hooks *h, u32 ch)
{
	return h->csccr_dn_status(h->ctx, ch & 0xff);
}

static inline u32 t32_api_ev_alloc_calc(const struct t32_lib_hooks *h, u32 ch)
{
	return h->ae_ev_alloc_calc(h->ctx, ch & 0xff);
}

static inline u32 t32_lce_self_light_correct_global(const struct t32_lib_hooks *h, u32 a0, u32 a1, u32 a2,
						    u32 a3, u32 a4)
{
	return h->lce_correct_local(h->ctx, a0, a1, a2, a3, a4);
}

static inline u32 t32_lib_get_ipc_output_pos(const struct t32_lib_hooks *h)
{
	return h->sys_read(h->ctx, T32_IPC_OUTPUT_POS_REG);
}

/* state = t32_data_dc284 */
static inline u32 t32_lib_get_running_mode(const unsigned char *state)
{
	return state[T32_CH_STATE_RUNMODE];
}

/* empty vendor entry points */
static inline u32 t32_lib_empty(void)
{
	return 0;
}

static inline u32 t32_ldc_cmd_set(u32 a0, u32 a1, u32 size)
{
	(void)a0; (void)a1;
	return size;
}

/* ---- group 2: state helpers ---- */

static inline u32 t32_lib_dma_cache_32(u32 x)
{
	return x + 32 - (x & 0x1f);
}

/* state = t32_data_dc284 (dc2b0 = dc284 + 44) */
static inline void t32_lib_set_running_mode(unsigned char *state, u32 mode)
{
	state[T32_CH_STATE_RUNMODE] = (unsigned char)mode;
}

struct t32_vic_par {
	u32 par0;	/* t32_data_e3f30 */
	u32 par1;	/* t32_data_e3f2c */
};

static inline u32 t32_lib_vic_get_par(const struct t32_vic_par *vp, u32 *par0, u32 *par1)
{
	*par0 = vp->par0;
	*par1 = vp->par1;
	return vp->par1;
}

/* ae_dbg_on = t32_data_e1500, ae_dbg_max = t32_data_e14f0 */
static inline u32 t32_lib_get_3a_info(u32 *ae_dbg_on, u32 *ae_dbg_max, u32 ch, u32 n)
{
	ch &= 0xff;
	ae_dbg_on[ch] = n != 0;
	ae_dbg_max[ch] = n;
	return 0;
}

/* ---- group 3: register triggers ---- */

#define T32_ISP_TRIG_REG	0x1040U
#define T32_VIC_BASE(n)		((0x80U + (n)) << 12)
#define T32_VIC_CTRL		0x1a8U
#define T32_VIC_STOP		0x10U

static inline u32 t32_lib_trig(const struct t32_lib_hooks *h)
{
	return h->sys_write(h->ctx, T32_ISP_TRIG_REG, 1);
}

static inline u32 t32_lib_vic_ctrl(const struct t32_lib_hooks *h, u32 ch, u32 val)
{
	return h->sys_write(h->ctx, T32_VIC_BASE(ch & 0xff) + T32_VIC_CTRL, val);
}

static inline u32 t32_lib_vic_stop_byindex(const struct t32_lib_hooks *h, u32 idx)
{
	if (idx >= 2)
		return 0;
	return h->sys_write(h->ctx, T32_VIC_BASE(idx) + T32_VIC_STOP, 0);
}

static inline u32 t32_lib_tmo_strength_ctrl(const struct t32_lib_hooks *h, u32 ch, u32 strength)
{
	return h->tmo_strength(h->ctx, ch & 0xff, strength & 0xffff);
}

/* ---- group 4: memory space ---- */

#define T32_MDNS_CH_SIZE	76U	/* t32_data_dc074 channel block */
#define T32_MDNS_CH_MEMMODE	11U
#define T32_MEMSPACE_MODE	4U	/* byte of the caller's config */
#define T32_CH_STATE_W		0U	/* u16 */
#define T32_CH_STATE_H		4U	/* u16 */
#define T32_CH_STATE_B43	43U

/* mem_set = t32_data_dd674 */
static inline u32 t32_lib_set_isp_memory_space(const struct t32_lib_hooks *h, unsigned char *mem_set, u32 ch,
					       u32 a1, u32 a2, u32 a3)
{
	*mem_set = 1;
	return h->mdns_set_mem_cfg(h->ctx, ch & 0xff, a1, a2, a3);
}

/* mdns = t32_data_dc074, state = t32_data_dc284 */
static inline u32 t32_lib_get_isp_memory_space(const struct t32_lib_hooks *h, unsigned char *mdns,
					       const unsigned char *state, u32 ch, const unsigned char *cfg)
{
	const unsigned char *st;

	ch &= 0xff;
	st = state + ch * T32_CH_STATE_SIZE_LW;
	mdns[ch * T32_MDNS_CH_SIZE + T32_MDNS_CH_MEMMODE] = cfg[T32_MEMSPACE_MODE];
	return h->mdns_get_mem_size(h->ctx, ch, *(const unsigned short *)(st + T32_CH_STATE_W),
				    *(const unsigned short *)(st + T32_CH_STATE_H), cfg[T32_MEMSPACE_MODE],
				    st[T32_CH_STATE_B43]);
}

#endif
