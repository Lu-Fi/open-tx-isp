/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core frame-drop and event helpers of the vendor module:
 * __ispcore_get_frame_drop, __ispcore_single_cam_check_frame_drop,
 * ispcoer_debug_info_fs_trige, pm_core_event_process_thread and
 * tisp_process_deinit.
 *
 * The core object is a 32-bit kernel struct, handled as a byte image with
 * the vendor offsets: +116 channel number, +256 frame-drop enable, +260
 * frame-drop active, +264 pattern length - 1, +268 drop mask, +272 pattern
 * position, +744 debug mode, +752 debug period, +1048 per-channel debug
 * counters, +1064 per-channel completions (12 bytes).  Kernel and tisp
 * calls go through struct t32_corefd_ops.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK):
 * tisp_process_deinit polls tisp_event_work_empty up to 101 times (10 ms
 * apart) and prints its timeout message also when the queue drained on
 * exactly the 100th poll.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_corefd_oracle).
 */
#ifndef TX_ISP_T32_COREFD_H
#define TX_ISP_T32_COREFD_H

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
#ifndef T32_HOST_S64
#define T32_HOST_S64
typedef int64_t s64;
typedef int32_t s32;
#endif
#endif

struct t32_corefd_ops {
	int (*isp_printf)(int level, const char *fmt, ...);
	void (*get_frame_drop)(u32 sensor, u32 chn, u32 *attr);	/* tisp_get_frame_drop */
	void (*set_frame_drop)(u32 sensor, u32 chn, u32 *attr);	/* tisp_set_frame_drop */
	void (*complete)(void *done);				/* private_complete */
	int (*kthread_should_stop)(void);
	int (*core_event_process)(u32 arg);			/* isp_core_event_process */
	int (*event_exit)(void);				/* tisp_event_exit */
	int (*event_work_empty)(u32 ch);			/* tisp_event_work_empty */
	void (*msleep)(u32 ms);
};

#define T32_FD_ATTR_WORDS	4	/* the tisp frame-drop attribute the check hands on */

static inline u32 *t32_core_w(u8 *core, u32 off)
{
	return (u32 *)(core + off);
}

/* __ispcore_get_frame_drop: 1 = drop this frame (and mark it), 0 = keep */
static inline int t32_ispcore_get_frame_drop(const struct t32_corefd_ops *k, u8 *core)
{
	u32 bit = 1u << (*t32_core_w(core, 272) & 31);
	int drop = 0;

	if (!(*t32_core_w(core, 268) & bit)) {
		if (*t32_core_w(core, 256) == 1) {
			*t32_core_w(core, 268) |= bit;
			if (*t32_core_w(core, 268) == 0xffffffffu)
				*t32_core_w(core, 260) = 0;
		}
		k->isp_printf(0, "[%s %d] chn:%d fd_size:%d fd_mark:0x%x drop frame\n", "__ispcore_get_frame_drop", 945,
			      *t32_core_w(core, 116), *t32_core_w(core, 264), *t32_core_w(core, 268));
		drop = 1;
	}
	if (++*t32_core_w(core, 272) > *t32_core_w(core, 264))
		*t32_core_w(core, 272) = 0;
	return drop;
}

/* __ispcore_single_cam_check_frame_drop: once the mask is full, the tisp side's flag word is cleared */
static inline void t32_ispcore_single_cam_check_frame_drop(const struct t32_corefd_ops *k, u8 *core)
{
	u32 attr[T32_FD_ATTR_WORDS];
	s32 chn;

	if (!*t32_core_w(core, 260))
		return;
	t32_ispcore_get_frame_drop(k, core);
	if (*t32_core_w(core, 256) != 1 || *t32_core_w(core, 268) != 0xffffffffu)
		return;
	chn = (s32)*t32_core_w(core, 116);
	k->get_frame_drop((u32)(chn / 3), (u32)(chn % 3), attr);
	attr[0] = 0;
	k->set_frame_drop((u32)(chn / 3), (u32)(chn % 3), attr);
}

/* ispcoer_debug_info_fs_trige: every period frames, complete the channel's debug waiter */
static inline void t32_ispcore_debug_info_fs_trige(const struct t32_corefd_ops *k, u8 *core, u32 ch)
{
	u32 *cnt = t32_core_w(core, 1048 + 4 * ch);

	if (*t32_core_w(core, 744) != 2 || (s32)*cnt >= 0)
		return;
	if ((*cnt & 0xffff) < *t32_core_w(core, 752)) {
		++*cnt;
		return;
	}
	*cnt = 0;
	k->complete(core + 1064 + 12 * ch);
}

/* pm_core_event_process_thread */
static inline int t32_pm_core_event_process_thread(const struct t32_corefd_ops *k)
{
	while (!k->kthread_should_stop())
		if (k->core_event_process(0))
			k->isp_printf(1, "[%s][%d] process error\n", "pm_core_event_process_thread", 3399);
	return 0;
}

/* tisp_process_deinit */
static inline void t32_tisp_process_deinit(const struct t32_corefd_ops *k, u32 ch)
{
	u32 ret = (u32)k->event_exit() & 0xff, n = 0, empty;

	if (ret)
		k->isp_printf(2, "[[ %s:%d ]] tisp_event_exit error %d\n", "tisp_process_deinit", 610, ret);
	do {
		empty = (u32)k->event_work_empty(ch & 0xff) & 0xff;
		k->msleep(10);
		if (++n == 101)
			break;
	} while (!empty);
	if (n == 100 || n == 101)
		k->isp_printf(2, "[[ %s:%d ]] tisp_event_exit error %d\n", "tisp_process_deinit", 618, empty);
}

#endif /* TX_ISP_T32_COREFD_H */
