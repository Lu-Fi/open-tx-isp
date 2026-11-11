/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 small control handlers of the vendor module: the /proc/tisp write and
 * show handlers (tisp_write_proc, tisp_show_debug_info), the CCM attribute
 * get control (tiziano_isp_ccm_attr_g_ctrl.isra.40) and the media bus code
 * to Bayer order mapping (mbus_to_bayer_write).
 *
 * /proc/tisp: "help" sets the help flag (the next show prints the mode
 * menu once); a number 0..4 selects the log level; the show prints the
 * module versions and the tisp debug info only at level 0.
 * Kernel / tisp calls go through struct t32_ctrl_ops.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK):
 * tisp_write_proc accepts any number (kstrtoint ok) but only stores 0..4,
 * and returns count also when it kept only the first 255 bytes.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_ctrl_oracle).
 */
#ifndef TX_ISP_T32_CTRL_H
#define TX_ISP_T32_CTRL_H

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
#endif

#define T32_CCM_ATTR_BYTES	40
#define T32_MBUS_BAYER_BASE	0x5200	/* MEDIA_BUS_FMT_SBGGR8_1X8 .. 20 codes */

struct t32_ctrl_ops {
	/* bytes not copied (copy_from_user incl. access_ok and zero fill) */
	unsigned long (*copy_from_user)(void *to, const void *from, unsigned long n);
	unsigned long (*copy_to_user)(void *to, const void *from, unsigned long n);
	void (*mutex_lock)(void *m);
	void (*mutex_unlock)(void *m);
	int (*kstrtoint)(const char *s, unsigned int base, int *res);
	int (*seq_printf)(void *seq, const char *fmt, ...);
	void (*module_version_debug)(void *seq);	/* tisp_module_version_debug */
	void (*debug_info)(void *seq);			/* lib_tisp_debug_info */
	void (*g_ccm_attr)(u32 arg, void *attr);	/* tisp_g_ccm_attr */
	int (*isp_printf)(int level, const char *fmt, ...);
	void (*set_bayer)(u32 ch, u32 order);		/* lib_tisp_api_set_bayer */
};

struct t32_ctrl_state {
	void *lock;		/* the /proc/tisp mutex */
	u8 help;		/* "help" written */
	u8 level;		/* log level 0..4 */
};

/* tisp_write_proc */
static inline long t32_tisp_write_proc(const struct t32_ctrl_ops *k, struct t32_ctrl_state *st, const void *ubuf,
				       unsigned long count)
{
	char buf[256];
	unsigned long n = count < 256 ? count : 255;
	int val;

	memset(buf, 0, sizeof(buf));
	if (k->copy_from_user(buf, ubuf, n))
		return -14;
	k->mutex_lock(st->lock);
	if (!strncmp(buf, "help", 4)) {
		st->help = 1;
	} else if (k->kstrtoint(buf, 10, &val)) {
		k->mutex_unlock(st->lock);
		return -22;
	} else if ((u32)val < 5) {
		st->level = (u8)val;
	}
	k->mutex_unlock(st->lock);
	return (long)count;
}

/* tisp_show_debug_info */
static inline int t32_tisp_show_debug_info(const struct t32_ctrl_ops *k, struct t32_ctrl_state *st, void *seq)
{
	k->mutex_lock(st->lock);
	if (st->help) {
		k->seq_printf(seq, "===== TISP log information level selection =====\n");
		k->seq_printf(seq, "0: ISP basic message\n");
		k->seq_printf(seq, "==== Current mode %d ====\n", st->level);
		k->seq_printf(seq, "Write 'echo n > /proc/tisp' to switch mode.\n");
		st->help = 0;
	} else if (!st->level) {
		k->module_version_debug(seq);
		k->debug_info(seq);
	}
	k->mutex_unlock(st->lock);
	return 0;
}

/* tiziano_isp_ccm_attr_g_ctrl (ctrl: +0 user pointer of the 40-byte attribute) */
static inline int t32_ccm_attr_g_ctrl(const struct t32_ctrl_ops *k, u32 arg, const u32 *ctrl)
{
	u8 attr[T32_CCM_ATTR_BYTES];

	k->g_ccm_attr(arg, attr);
	if (k->copy_to_user((void *)(uintptr_t)ctrl[0], attr, T32_CCM_ATTR_BYTES))
		k->isp_printf(2, "[ %s:%d ] copy error!!!\n", "tiziano_isp_ccm_attr_g_ctrl", 457);
	return 0;
}

/* mbus_to_bayer_write: the 20 Bayer media bus codes 0x5200.. cycle BGGR, GBRG, GRBG, RGGB */
static inline int t32_mbus_to_bayer_write(const struct t32_ctrl_ops *k, u32 ch, u32 code)
{
	static const u8 order[4] = { 1, 3, 2, 0 };
	u32 i = code - T32_MBUS_BAYER_BASE;

	if (i < 20)
		k->set_bayer(ch & 0xff, order[i & 3]);
	else
		k->isp_printf(2, "%s[%d] the format(0x%08x) of input couldn't be handled!\n", "mbus_to_bayer_write", 927,
			      code);
	return 0;
}

#endif /* TX_ISP_T32_CTRL_H */
