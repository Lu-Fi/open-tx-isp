/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 video-input proc command write of the vendor module:
 * video_input_cmd_set ("r sen_reg<id> <reg>" / "w sen_reg<id> <reg> <val>",
 * sensor register access through the sensor subdev's g_register /
 * s_register core ops, result text into the slot's line for
 * video_input_cmd_show).
 *
 * Objects (32-bit kernel layout): file +120 the seq file; seq +72 the
 * device; device +284 the video-input device vi; vi +296 + 4 * id the
 * sensor subdev of slot id (0..4, other digits use 0), vi +324 + 12 * id
 * the slot mutex; sensor +192 32-byte chip match, +264 ops (ops +0 core
 * ops: +16 g_register, +28 s_register).  The register request is
 * { 32-byte match, ..., +48 u64 reg, +56 u64 val } (a 64-byte struct).
 * Kernel calls go through struct t32_vicmd_ops.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK):
 *  - "r sen_reg" is matched on its first 8 characters only;
 *  - after a failed user copy the buffer leaks and -14 is returned when
 *    count < 129; for count >= 129 the buffer is freed and -14 returned;
 *  - a read whose g_register fails still reports "read ok" with the
 *    request's value word (uninitialised stack in the vendor, 0 in ours);
 *  - the reg / val numbers are truncated to 32 bits.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_vicmd_oracle).
 */
#ifndef TX_ISP_T32_VICMD_H
#define TX_ISP_T32_VICMD_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
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
#ifndef T32_HOST_U64
#define T32_HOST_U64
typedef uint64_t u64;
#endif
#endif

#define T32_VICMD_GFP		0x24000c0	/* GFP_KERNEL of the vendor build */

struct t32_vicmd_ops {
	void *(*kmalloc)(u32 size, u32 flags);
	void (*kfree)(void *p);
	/* bytes not copied; on a failed access check the vendor zero-fills and reports count */
	unsigned long (*copy_from_user)(void *to, const void *from, unsigned long n);
	u64 (*strtoull)(const char *cp, char **endp, u32 base);	/* private_simple_strtoull */
	void (*mutex_lock)(void *m);
	void (*mutex_unlock)(void *m);
	int (*seq_printf)(void *seq, const char *fmt, ...);
	int (*sprintf)(char *buf, const char *fmt, ...);
	int (*isp_printf)(int level, const char *fmt, ...);
};

typedef int (*t32_sensor_reg_fn)(void *sd, void *reg);

static inline u8 *t32_vicmd_p(const u8 *p, u32 off)	/* a 32-bit pointer field */
{
	return (u8 *)(uintptr_t)*(const u32 *)(p + off);
}

static inline int t32_vicmd_bad(const u8 *p)
{
	return !p || (u32)(uintptr_t)p >= (u32)-4095;
}

/* the sensor's core op (+16 g_register, +28 s_register), -515 when there is none */
static inline int t32_vicmd_sensor_op(u8 *sensor, u32 op, u8 *reg)
{
	u8 *core = t32_vicmd_p(t32_vicmd_p(sensor, 264), 0);
	t32_sensor_reg_fn fn;

	if (!core)
		return -515;
	fn = (t32_sensor_reg_fn)(uintptr_t)*(u32 *)(core + op);
	if (!fn)
		return -515;
	return fn(sensor, reg);
}

/* texts: d9460 (4 x 128 bytes; slot id 4 writes into the following object, as the vendor) */
static inline long t32_video_input_cmd_set(const struct t32_vicmd_ops *k, u8 *file, const void *ubuf, u32 count,
					   char *texts)
{
	u8 *seq = t32_vicmd_p(file, 120), *dev, *vi, *sensor, *lock;
	u8 reg[64];
	char *buf, *p, *end;
	u32 id, r, v;
	int ret;

	memset(reg, 0, sizeof(reg));	/* the vendor leaves the unset words uninitialised */
	dev = t32_vicmd_p(seq, 72);
	vi = t32_vicmd_bad(dev) ? NULL : t32_vicmd_p(dev, 284);
	if (t32_vicmd_bad(dev) || t32_vicmd_bad(vi) || t32_vicmd_bad(t32_vicmd_p(vi, 296))) {
		k->seq_printf(seq, "don't have active sensor, please set sensor firstly!\n");
		return 0;
	}
	buf = k->kmalloc(count + 1, T32_VICMD_GFP);
	if (!buf)
		return -12;
	memset(buf, 0, count + 1);
	if (k->copy_from_user(buf, ubuf, count)) {
		if (count < 129)
			return -14;
		k->kfree(buf);
		return -14;
	}
	if (!strncmp(buf, "r sen_reg", strlen("r sen_reg") - 1)) {
		p = buf + strlen("r sen_reg");
		id = (u8)(*p - '0');
		if (id >= 5)
			id = 0;
		sensor = t32_vicmd_p(vi, 296 + 4 * id);
		for (p++; *p == ' '; p++)
			;
		r = (u32)k->strtoull(p, NULL, 0);
		memcpy(reg, sensor + 192, 32);
		*(u32 *)(reg + 48) = r;
		*(u32 *)(reg + 52) = 0;
		lock = vi + 324 + 12 * id;
		k->mutex_lock(lock);
		ret = sensor ? t32_vicmd_sensor_op(sensor, 16, reg) : -19;
		k->mutex_unlock(lock);
		if (ret)
			k->isp_printf(2, "Error: [ %s:%d ]\n", "video_input_cmd_set", 1179);
		v = *(u32 *)(reg + 56);
		k->seq_printf(seq, "isp: sensor reg read 0x%x(0x%x)\n", r, v);
		k->sprintf(texts + 128 * id, "reg:0x%x, value:0x%x, state:read ok", r, v);
	} else if (!strncmp(buf, "w sen_reg", 9)) {
		p = buf + strlen("w sen_reg");
		id = (u8)(*p - '0');
		if (id >= 5)
			id = 0;
		sensor = t32_vicmd_p(vi, 296 + 4 * id);
		for (p++; *p == ' '; p++)
			;
		end = NULL;
		r = (u32)k->strtoull(p, &end, 0);
		v = (u32)k->strtoull(end + 1, NULL, 0);
		k->seq_printf(seq, "isp: sensor reg write 0x%x(0x%x)\n", r, v);
		memcpy(reg, sensor + 192, 32);
		*(u32 *)(reg + 48) = r;
		*(u32 *)(reg + 52) = 0;
		*(u32 *)(reg + 56) = v;
		*(u32 *)(reg + 60) = 0;
		lock = vi + 324 + 12 * id;
		k->mutex_lock(lock);
		ret = sensor ? t32_vicmd_sensor_op(sensor, 28, reg) : -19;
		k->mutex_unlock(lock);
		if (!ret)
			k->sprintf(texts + 128 * id, "reg:0x%x, value:0x%x, state:%s %s", r, v, "write", "ok");
		else
			k->sprintf(texts + 128 * id, "reg:0x%x, value:null, state:%s %s", r, "write", "failed");
	}
	k->kfree(buf);
	return count;
}

#endif /* TX_ISP_T32_VICMD_H */
