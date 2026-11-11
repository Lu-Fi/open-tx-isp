/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 LDC ioctls: the offline LDC misc device and the LDC sub-device core ioctl.
 *
 *   t32_ldc_offline_ioctl  ldc_offline_unlocked_ioctl 0xfdf8: command 2 = run one offline LDC frame: copy the
 *                          28-byte attributes from user space (failure: log), tisp_offline_ldc_set_attr, wait up
 *                          to 1000 ms for the frame-done completion (timeout: log); every command returns 0
 *   t32_ldc_core_ioctl     ldc_core_ops_ioctl 0x1015c:
 *     0x5000005  copy a word from user space into private + 320 (failure: log, -14 (-EFAULT)); a 0 word also
 *                sets the done flag (private + 420) = 1
 *     0x5000006  copy 24 bytes from user space (failure: log, -14): word 0 = LDC id; read its online attributes,
 *                replace size (u16 at 8 / 10 -> 0 / 2) and word 20 -> 8, write them back; returns the set result
 *     0x5000007  done flag (private + 420) -> the caller's word (memcpy, as the vendor); returns 0
 *     0x1000000  call the sub-device's core ioctl op (sd->ops->core->ioctl); NULL sub-device -19 (-ENODEV),
 *                missing op -515 (-ENOIOCTLCMD)
 *     other      0
 *     Every error except -515 is also logged.  The private pointer of 0x5000005 / 0x5000007 is NULL for a NULL
 *     or error sub-device and is then dereferenced (vendor behaviour, kept).
 *
 * The vendor keeps the LDC device in t32_data_d9948; here it is a field of the hook context.  OS calls are hooks.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_ldcioctl_oracle).
 */
#ifndef TX_ISP_T32_LDCIOCTL_H
#define TX_ISP_T32_LDCIOCTL_H

#ifndef __KERNEL__
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#define T32_IS_ERR_OR_NULL_IO(p)	(!(p) || (uintptr_t)(p) >= (uintptr_t)-4095)
#else
#include <linux/err.h>
#define T32_IS_ERR_OR_NULL_IO(p)	IS_ERR_OR_NULL(p)
#endif

#define T32_LDC_IOC_OFFLINE_RUN	2U
#define T32_LDC_IOC_ONLINE_EN	0x5000005U
#define T32_LDC_IOC_ONLINE_ATTR	0x5000006U
#define T32_LDC_IOC_DONE_GET	0x5000007U
#define T32_LDC_IOC_CORE	0x1000000U

#define T32_LDC_EFAULT		((u32)-14)
#define T32_LDC_ENODEV		((u32)-19)
#define T32_LDC_ENOIOCTLCMD	((u32)-515)

#define T32_LDC_OFFLINE_ATTR	28U
#define T32_LDC_DONE_OFF	40U	/* frame-done completion inside the LDC device */
#define T32_LDC_DONE_TIMEOUT	1000U
#define T32_LDC_PRIV_ONLINE	320U	/* u32 */
#define T32_LDC_PRIV_DONE	420U	/* u32 */

/* log messages (vendor format strings) */
enum { T32_LDC_MSG_OFF_COPY = 1, T32_LDC_MSG_OFF_TIMEOUT, T32_LDC_MSG_COPY, T32_LDC_MSG_ERR };

struct t32_sd_core_ops {
	void *op0;
	u32 (*ioctl)(void *sd, void *arg);	/* + 4 on the target */
};
struct t32_sd_ops {
	const struct t32_sd_core_ops *core;
};

struct t32_ldc_sdio {
	unsigned char hdr[264];
	const struct t32_sd_ops *ops;					/* sub-device + 264 */
	unsigned char pad[280 - 264 - sizeof(void *)];
	unsigned char *priv;						/* sub-device + 280 */
};

struct t32_ldcio_hooks {
	void *ctx;
	unsigned char *dev;		/* t32_data_d9948: the LDC device */
	u32 (*copy_from_user)(void *ctx, void *dst, const void *src, u32 n);	/* private_copy_from_user */
	void (*log)(void *ctx, u32 level, u32 msg, u32 line);			/* isp_printf */
	u32 (*offline_set_attr)(void *ctx, void *attr);				/* tisp_offline_ldc_set_attr */
	u32 (*wait_done)(void *ctx, void *done, u32 ms);	/* private_wait_for_completion_timeout */
	u32 (*online_get_attr)(void *ctx, u32 id, void *attr);			/* tisp_online_ldc_get_attr */
	u32 (*online_set_attr)(void *ctx, u32 id, void *attr);			/* tisp_online_ldc_set_attr */
};

static inline u32 t32_ldc_offline_ioctl(const struct t32_ldcio_hooks *h, u32 cmd, const void *arg)
{
	u8 attr[T32_LDC_OFFLINE_ATTR];

	if (cmd != T32_LDC_IOC_OFFLINE_RUN)
		return 0;
	if (h->copy_from_user(h->ctx, attr, arg, sizeof(attr))) {
		h->log(h->ctx, 2, T32_LDC_MSG_OFF_COPY, 0);
		return 0;
	}
	h->offline_set_attr(h->ctx, attr);
	if (!h->wait_done(h->ctx, h->dev + T32_LDC_DONE_OFF, T32_LDC_DONE_TIMEOUT))
		h->log(h->ctx, 2, T32_LDC_MSG_OFF_TIMEOUT, 0);
	return 0;
}

static inline u8 *t32_ldc_io_priv(struct t32_ldc_sdio *sd)
{
	return T32_IS_ERR_OR_NULL_IO(sd) ? NULL : sd->priv;
}

/* the online attribute block as tisp_online_ldc_get/set_attr use it */
struct t32_ldc_online_attr {
	u16 w, h;
	u32 w4;
	u32 mode;
	u32 w12;
};

static inline u32 t32_ldc_core_ioctl(const struct t32_ldcio_hooks *h, struct t32_ldc_sdio *sd, u32 cmd, void *arg)
{
	u32 r, in[6];
	struct t32_ldc_online_attr at;
	u8 *priv;

	switch (cmd) {
	case T32_LDC_IOC_ONLINE_EN:
		priv = t32_ldc_io_priv(sd);
		if (h->copy_from_user(h->ctx, in, arg, 4)) {
			h->log(h->ctx, 2, T32_LDC_MSG_COPY, 35);
			r = T32_LDC_EFAULT;
			break;
		}
		*(u32 *)(priv + T32_LDC_PRIV_ONLINE) = in[0];
		if (!in[0])
			*(u32 *)(priv + T32_LDC_PRIV_DONE) = 1;
		return 0;
	case T32_LDC_IOC_ONLINE_ATTR:
		if (h->copy_from_user(h->ctx, in, arg, 24)) {
			h->log(h->ctx, 2, T32_LDC_MSG_COPY, 68);
			r = T32_LDC_EFAULT;
			break;
		}
		h->online_get_attr(h->ctx, in[0], &at);
		at.mode = in[5];
		at.w = (u16)in[2];
		at.h = (u16)(in[2] >> 16);
		r = h->online_set_attr(h->ctx, in[0], &at);
		if (!r || r == T32_LDC_ENOIOCTLCMD)
			return r;
		break;
	case T32_LDC_IOC_DONE_GET:
		priv = t32_ldc_io_priv(sd);
		memcpy(arg, priv + T32_LDC_PRIV_DONE, 4);
		return 0;
	case T32_LDC_IOC_CORE:
		if (!sd) {
			r = T32_LDC_ENODEV;
			break;
		}
		if (!sd->ops->core || !sd->ops->core->ioctl)
			return T32_LDC_ENOIOCTLCMD;
		r = sd->ops->core->ioctl(sd, arg);
		if (!r || r == T32_LDC_ENOIOCTLCMD)
			return r;
		break;
	default:
		return 0;
	}
	h->log(h->ctx, 2, T32_LDC_MSG_ERR, 107);
	return r;
}

#endif
