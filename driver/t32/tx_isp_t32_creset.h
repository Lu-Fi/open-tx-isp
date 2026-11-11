/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core reset request of the vendor module: tisp_core_reset.
 *
 * mode 0 (single camera only, core +1196 != 1): under the core lock
 * (+288) the first request runs lib_tisp_core_reset and marks it running
 * (f335f), the request flag f335e is set; then the caller waits for the
 * reset completion.  mode != 0 (a second camera, +4012 set): when
 * lib_tisp_get_ipc_cache_data reports cached IPC data, the IPC flag f335d
 * is set and the caller waits.  Interrupted waits are reported.
 * The flags, lock, completion and library calls go through struct
 * t32_creset_ops / struct t32_creset_state.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK): mode 0
 * with a multi-camera core (+1196 = 1) is refused with a message, mode != 0
 * with +1196 = 1 silently.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_creset_oracle).
 */
#ifndef TX_ISP_T32_CRESET_H
#define TX_ISP_T32_CRESET_H

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
#endif

#define T32_ERESTARTSYS	512

struct t32_creset_ops {
	void (*lock)(void *lock, u32 *flags);		/* __private_spin_lock_irqsave */
	void (*unlock)(void *lock, u32 flags);
	void (*core_reset)(u8 *core);			/* lib_tisp_core_reset */
	int (*get_ipc_cache_data)(void);		/* lib_tisp_get_ipc_cache_data */
	int (*wait)(void *done);			/* private_wait_for_completion_interruptible */
	int (*isp_printf)(int level, const char *fmt, ...);
};

struct t32_creset_state {
	u8 ipc_pending;		/* f335d */
	u8 reset_req;		/* f335e */
	u8 reset_running;	/* f335f */
	void *done;		/* f331c: the reset completion */
};

static inline void t32_creset_wait(const struct t32_creset_ops *k, struct t32_creset_state *st, const char *intr)
{
	int r = k->wait(st->done);

	if (!r)
		return;
	if (r == -T32_ERESTARTSYS)
		k->isp_printf(1, intr);
	else
		k->isp_printf(1, "Unexpected error: %d\n", r);
}

static inline void t32_tisp_core_reset(const struct t32_creset_ops *k, struct t32_creset_state *st, u8 *core, u32 mode)
{
	u32 flags = 0, multi = *(u32 *)(core + 1196);

	if (!mode) {
		if (multi == 1) {
			k->isp_printf(1, "Multi-camera setup required for this interface. !!! \n");
			return;
		}
		k->lock(core + 288, &flags);
		if (st->reset_running != 1) {
			st->reset_running = 1;
			k->core_reset(core);
		}
		st->reset_req = 1;
		k->unlock(core + 288, flags);
		t32_creset_wait(k, st, "Wait interrupted by signal.\n");
		return;
	}
	if (multi == 1 || !*(u32 *)(core + 4012))
		return;
	k->lock(core + 288, &flags);
	if (!k->get_ipc_cache_data()) {
		k->unlock(core + 288, flags);
		return;
	}
	st->ipc_pending = 1;
	k->unlock(core + 288, flags);
	t32_creset_wait(k, st, "Wait interrupted by signal!!.\n");
}

#endif /* TX_ISP_T32_CRESET_H */
