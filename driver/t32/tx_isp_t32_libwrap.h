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
 * Hooks: struct t32_lib_hooks (the callees).  Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_libwrap_oracle).
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
#define T32_CH_STATE_RUNMODE	44U	/* t32_data_dc284 byte: running mode (day / night) */

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

#endif
